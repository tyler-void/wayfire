// spread-overview input (constitution Principle II). Grab interaction handlers +
// click-to-focus (T017). Click discrimination is structured so the T020 drag
// threshold split is additive, not a rewrite.

#include "overview.hpp"

#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/render-manager.hpp>
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
        dragging   = false;
        press_pos  = local;
        press_view = hit;
        return;
    }

    if (!pressed)
    {
        return;
    }
    pressed = false;

    // A gesture that crossed the drag threshold is a DRAG, never a click: it does not
    // focus and does not close. (Sub-step A: snap back. Sub-step B: relocate-or-snap-back.)
    if (dragging)
    {
        end_drag(local);
        dragging   = false;
        press_view = nullptr;
        return;
    }

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
    (void)position; // use the shared cursor source, matching the button handler's space
    (void)time_ms;

    if (!pressed || !press_view)
    {
        return;
    }

    auto offset = wf::origin(output->get_layout_geometry());
    auto global = wf::get_core().get_cursor_position();
    wf::pointf_t local{global.x - offset.x, global.y - offset.y};

    // Enter DRAGGING once movement crosses the threshold (T024 — the ONLY thing that
    // separates a drag from a click).
    if (!dragging)
    {
        const double dist = std::hypot(local.x - press_pos.x, local.y - press_pos.y);
        if (dist < (double)opt_drag_threshold)
        {
            return;
        }

        // Principle VI: bail cleanly if the pressed view vanished before the drag started.
        if (!press_view->is_mapped() || !thumbnails.count(press_view))
        {
            press_view = nullptr;
            return;
        }

        auto tr = thumbnails[press_view];
        drag_orig_tx = tr->translation_x;
        drag_orig_ty = tr->translation_y;
        dragging     = true;
        LOGI("spread-overview: drag start");
    }

    // Principle VI: if the dragged view unmapped mid-drag, end cleanly (no crash, no
    // dangling transformer access) and reset the drag state.
    if (!press_view->is_mapped() || !thumbnails.count(press_view))
    {
        dragging   = false;
        pressed    = false;
        press_view = nullptr;
        return;
    }

    // Follow the cursor by the delta since press (self-managed drag, ADR-001 — no
    // core_drag_t). The delta on the layout translation keeps the grabbed point under
    // the cursor without snapping the corner to it.
    auto tr = thumbnails[press_view];
    tr->translation_x = (float)(drag_orig_tx + (local.x - press_pos.x));
    tr->translation_y = (float)(drag_orig_ty + (local.y - press_pos.y));
    output->render->damage_whole();
}

void spread_overview_t::end_drag(wf::pointf_t release_local)
{
    (void)release_local; // sub-step B will hit_test_cluster(release_local) -> relocate

    // Sub-step A: snap the dragged thumbnail back to its layout position.
    if (press_view && press_view->is_mapped() && thumbnails.count(press_view))
    {
        auto tr = thumbnails[press_view];
        tr->translation_x = (float)drag_orig_tx;
        tr->translation_y = (float)drag_orig_ty;
        output->render->damage_whole();
    }

    LOGI("spread-overview: drag end -> snap back");
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
