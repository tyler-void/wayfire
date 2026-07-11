// spread-overview input (constitution Principle II). Grab interaction handlers +
// click-to-focus (T017). Click discrimination is structured so the T020 drag
// threshold split is additive, not a rewrite.

#include "overview.hpp"

#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/geometry.hpp>
#include <wayfire/util/log.hpp>

#include <linux/input-event-codes.h>
#include <cmath>

namespace wf
{
namespace spread
{
wayfire_toplevel_view spread_overview_t::thumb_at(wf::pointf_t local)
{
    for (auto& [v, r] : thumb_rects)
    {
        if ((local.x >= r.x) && (local.x < r.x + r.w) &&
            (local.y >= r.y) && (local.y < r.y + r.h))
        {
            return v;
        }
    }

    return nullptr;
}

void spread_overview_t::handle_pointer_button(const wlr_pointer_button_event& event)
{
    if (event.button != BTN_LEFT)
    {
        return;
    }

    // Global cursor -> output-local coords (the space thumb_rects live in).
    auto offset = wf::origin(output->get_layout_geometry());
    auto global = wf::get_core().get_cursor_position();
    wf::pointf_t local{global.x - offset.x, global.y - offset.y};
    auto hit = thumb_at(local);

    if (event.state == WL_POINTER_BUTTON_STATE_PRESSED)
    {
        pressed    = true;
        press_pos  = local;
        press_view = hit;
        return;
    }

    if (!pressed)
    {
        return;
    }
    pressed = false;

    const double dist = std::hypot(local.x - press_pos.x, local.y - press_pos.y);
    auto rel_view = hit;

    // Click = movement under threshold AND released over the same thumbnail. A gesture
    // >= threshold will become a drag in T020 (never a relocate here). A click focuses
    // and raises the view and closes the overview (scale parity).
    if (press_view && (press_view == rel_view) && (dist < (double)opt_drag_threshold))
    {
        auto target = press_view;
        LOGI("spread-overview: click -> focus + close");
        deactivate();
        if (target && target->is_mapped())
        {
            // allow_switch_ws=true: a clicked thumbnail may live on another workspace;
            // focusing it must switch to that workspace (scale parity). Without this,
            // a cross-workspace click can't focus and appears to "do nothing".
            wf::get_core().default_wm->focus_raise_view(target, true);
        }
    }
    else if (!press_view && (dist < (double)opt_drag_threshold) && opt_close_on_bg)
    {
        // Click on empty background (T019 close_on_bg_click).
        LOGI("spread-overview: background click -> close");
        deactivate();
    }

    press_view = nullptr;
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
