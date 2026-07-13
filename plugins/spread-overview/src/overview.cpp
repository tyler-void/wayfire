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
        deactivate();
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

    deactivate();
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

    // ACTIVATING -> ACTIVE collapses for now (entry animation arrives in T027).
    state = session_state::ACTIVE;
    grab->grab_input(wf::scene::layer::OVERLAY);
    build_spread();
    LOGI("spread-overview: ACTIVE - input grabbed, ", session_views.size(), " views spread");
    return true;
}

void spread_overview_t::deactivate()
{
    if (state == session_state::IDLE)
    {
        return;
    }

    state = session_state::DEACTIVATING;
    clear_spread();
    grab->ungrab_input();
    output->deactivate_plugin(&grab_interface);
    state = session_state::IDLE;
    LOGI("spread-overview: IDLE - input released");
}

} // namespace spread
} // namespace wf
