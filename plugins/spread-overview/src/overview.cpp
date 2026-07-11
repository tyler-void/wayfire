#include "overview.hpp"

#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/util/log.hpp>

#include <linux/input-event-codes.h>

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

    // ACTIVATING -> ACTIVE collapses for now (entry animation arrives in T027).
    state = session_state::ACTIVE;
    grab->grab_input(wf::scene::layer::OVERLAY);
    LOGI("spread-overview: ACTIVE - input grabbed");
    return true;
}

void spread_overview_t::deactivate()
{
    if (state == session_state::IDLE)
    {
        return;
    }

    state = session_state::DEACTIVATING;
    grab->ungrab_input();
    output->deactivate_plugin(&grab_interface);
    state = session_state::IDLE;
    LOGI("spread-overview: IDLE - input released");
}

void spread_overview_t::handle_pointer_button(const wlr_pointer_button_event& event)
{
    // Click-vs-drag disambiguation + select/relocate arrive in T017-T024.
    LOGI("spread-overview: pointer button=", event.button, " state=", (int)event.state);
}

void spread_overview_t::handle_pointer_motion(wf::pointf_t position, uint32_t time_ms)
{
    // Hover highlight + drag-follow arrive in T020.
    (void)position;
    (void)time_ms;
}

void spread_overview_t::handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event event)
{
    if ((event.state == WL_KEYBOARD_KEY_STATE_PRESSED) && (event.keycode == KEY_ESC))
    {
        LOGI("spread-overview: Esc - deactivating");
        deactivate();
    }
}
} // namespace spread
} // namespace wf
