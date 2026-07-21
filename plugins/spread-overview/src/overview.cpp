#include "overview.hpp"

#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/util/log.hpp>

namespace wf
{
namespace spread
{
void spread_overview_t::init()
{
    grab = std::make_unique<wf::input_grab_t>("spread-overview", output, this, this, nullptr);
    output->add_activator(opt_toggle, &toggle_cb);
    LOGI("spread-overview: init on ", output->to_string());
}

void spread_overview_t::fini()
{
    if (state != session_state::IDLE)
    {
        // Force a synchronous teardown — no exit animation while the plugin is being
        // destroyed (the deferred idle would fire after we're gone).
        finish_idle.disconnect();
        if (anim_hook_active)
        {
            output->render->rem_effect(&anim_hook);
            anim_hook_active = false;
        }
        finish_deactivate();
    }

    output->rem_binding(&toggle_cb);
    LOGI("spread-overview: fini on ", output->to_string());
}

bool spread_overview_t::toggle()
{
    if (state == session_state::IDLE)
    {
        return activate();
    }

    deactivate(true); // Esc / toggle-off: animate the thumbnails back out
    return true;
}

bool spread_overview_t::activate()
{
    if (!output->activate_plugin(&grab_interface))
    {
        LOGI("spread-overview: activation refused (another plugin holds the grab)");
        return false;
    }

    // Load the per-cell wallpaper ONCE, lazily on first open (B1: upload + report, no
    // draw). Isolated in wallpaper.cpp and fully fail-soft, so even a load/upload failure
    // here cannot abort activation — worst case is wallpaper_ok=false (today's look).
    if (!wallpaper_load_attempted)
    {
        wallpaper_load_attempted = true;
        load_wallpaper();
    }

    // ACTIVATING -> ACTIVE collapses for now (a distinct entry state can come with exit
    // animation, A2). ENTRY animates the thumbnails in from their real positions.
    state = session_state::ACTIVE;
    grab->grab_input(wf::scene::layer::OVERLAY);
    build_spread(spread_anim::ENTRY);
    LOGI("spread-overview: ACTIVE - input grabbed, ", session_views.size(), " views spread");
    return true;
}

void spread_overview_t::deactivate(bool animate)
{
    // Ignore if already closed, or an exit animation is already playing.
    if ((state == session_state::IDLE) || (state == session_state::DEACTIVATING))
    {
        return;
    }

    if (!animate)
    {
        // Immediate close (click-to-focus, empty-cell switch, grab cancel): the caller is
        // about to navigate, so tear down now rather than animate over the transition.
        finish_deactivate();
        return;
    }

    // Animate every thumbnail back to its real position; finish_deactivate runs once the
    // clocks settle (deferred to idle in animate_step), keeping the grab + scene up meanwhile.
    state = session_state::DEACTIVATING;
    start_exit_anim();
    if (!anim_hook_active)
    {
        finish_deactivate(); // nothing to animate (no thumbnails) -> tear down immediately
    }
}

void spread_overview_t::finish_deactivate()
{
    clear_spread();
    grab->ungrab_input();
    output->deactivate_plugin(&grab_interface);
    state = session_state::IDLE;
    LOGI("spread-overview: IDLE - input released");
}

} // namespace spread
} // namespace wf
