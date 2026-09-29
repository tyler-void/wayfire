// spread-overview input (constitution Principle II). Grab interaction handlers +
// click-to-focus (T017). Click discrimination is structured so the T020 drag
// threshold split is additive, not a rewrite.

#include "overview.hpp"

#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/workspace-set.hpp>
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
    // 002 (T027/FR-009): hit-test where the thumbnails actually ARE, walking the captured
    // stacking order so the TOP-MOST match wins. Mid-animation the live rects overlap (the
    // entry starts every thumbnail at its real desktop position, where a maximized window
    // covers its neighbours) and the old code iterated thumb_rects — a pointer-keyed map, so
    // "first hit" was an arbitrary window, not the one under the user's eyes. Once the
    // animation settles the live rect equals the slot, so behaviour is unchanged.
    for (auto& v : hit_order)
    {
        auto r = live_thumb_rect(v);
        if (!r)
        {
            continue;
        }

        if ((local.x >= r->x) && (local.x < r->x + r->w) &&
            (local.y >= r->y) && (local.y < r->y + r->h))
        {
            return v;
        }
    }

    return nullptr;
}

wf::pointf_t spread_overview_t::dragged_thumb_center(wf::pointf_t cursor_local)
{
    // 002 (T027): read the centre straight off the transformer. The drag already wrote the
    // cursor delta into the translation, so the live centre IS "slot centre + delta" in the
    // settled case, and stays correct when the drag started mid-flight — where the old
    // slot-plus-delta arithmetic would have pointed at a cell the thumbnail is not over.
    if (auto r = live_thumb_rect(press_view))
    {
        return {r->x + r->w / 2.0, r->y + r->h / 2.0};
    }

    return cursor_local; // no dragged thumbnail (shouldn't happen mid-drag) — fall back
}

void spread_overview_t::handle_pointer_button(const wlr_pointer_button_event& event)
{
    // Ignore input while the exit animation plays (the session is already closing).
    if (state == session_state::DEACTIVATING)
    {
        return;
    }

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

        // 002 (T025/FR-009): freeze ONLY this thumbnail, exactly where it is drawn. Its
        // transformer already holds the current interpolated values (animate_step writes
        // them every frame), so dropping its clock stops it in place with no jump; every
        // other thumbnail keeps flying to its slot. Nothing is frozen if it was not moving.
        if (press_view)
        {
            anim_state.erase(press_view);
        }

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
        deactivate(false); // navigating (focus/raise, maybe switch ws) -> close immediately
        if (target && target->is_mapped())
        {
            // allow_switch_ws=true: a clicked thumbnail may live on another workspace;
            // focusing it must switch to that workspace (scale parity). Without this,
            // a cross-workspace click can't focus and appears to "do nothing".
            wf::get_core().default_wm->focus_raise_view(target, true);
        }
    }
    else if (!press_view && (dist < (double)opt_drag_threshold))
    {
        // Empty-cell click (no thumbnail hit) -> switch to that cell's workspace + close
        // (expo behavior). Gated by the SAME threshold as thumbnail clicks, so a drag that
        // began on empty space (dist >= threshold) never switches. Esc/toggle never reach
        // here, so they still close without switching. For a bare click there is no drag,
        // so the cursor == the click point.
        if (auto tgt = hit_test_cluster(current_layout, local.x, local.y))
        {
            LOGI("spread-overview: empty click -> switch to ws (", tgt->x, ",", tgt->y, ")");
            deactivate(false); // switching workspace -> close immediately, then switch
            switch_workspace(wf::point_t{tgt->x, tgt->y});
        }
        else
        {
            // Outside all cells -> just close, no switch.
            LOGI("spread-overview: empty click (outside all cells) -> close");
            deactivate();
        }
    }
    else if (press_view)
    {
        // 002 (T029): every other release. A sub-threshold release away from the pressed
        // thumbnail (over empty space or over a DIFFERENT thumbnail), or a flick that passed
        // the threshold without ever producing a motion event, relocates nothing and closes
        // nothing — so a thumbnail frozen by this press would sit parked mid-flight until the
        // next reflow or close. Return it to its slot with the snap-back motion.
        LOGI("spread-overview: release without click or drag -> return to slot");
        animate_thumb_to_slot(press_view);
    }

    press_view = nullptr;
}

void spread_overview_t::handle_pointer_motion(wf::pointf_t position, uint32_t time_ms)
{
    (void)position; // use the shared cursor source, matching the button handler's space
    (void)time_ms;

    if ((state == session_state::DEACTIVATING) || !pressed || !press_view)
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

        // 002 (T030): the pressed thumbnail was already frozen on press, and every other
        // clock is left alone — so there is nothing to finalize here. (001 called
        // finalize_entry_anim(), which snapped EVERY thumbnail to its slot at once: the
        // jump FR-009 is about. That helper is gone.)
        auto tr = thumbnails[press_view];
        drag_orig_tx = tr->translation_x;
        drag_orig_ty = tr->translation_y;
        dragging     = true;

        // SC-005 evidence: press point, the thumbnail's position at the press, and the
        // pointer displacement so far. The thumbnail's displacement must match the
        // pointer's within 1 px from here on.
        auto lr = live_thumb_rect(press_view);
        LOGI("spread-overview: drag start press=(", press_pos.x, ",", press_pos.y,
            ") thumb=(", lr ? lr->x : 0.0, ",", lr ? lr->y : 0.0,
            ") pointer_delta=(", local.x - press_pos.x, ",", local.y - press_pos.y, ")");
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

    // Drop-target highlight: resolve from the dragged thumbnail's CENTER (not the cursor)
    // so what lights up is exactly what a drop would receive (Principle I). set_highlight
    // only re-renders on a cluster change, so this is cheap.
    if (border_node)
    {
        auto center = dragged_thumb_center(local);
        int hl = -1;
        if (auto tgt = hit_test_cluster(current_layout, center.x, center.y))
        {
            for (size_t i = 0; i < current_layout.clusters.size(); i++)
            {
                if ((current_layout.clusters[i].ws.x == tgt->x) &&
                    (current_layout.clusters[i].ws.y == tgt->y))
                {
                    hl = (int)i;
                    break;
                }
            }
        }

        border_node->set_highlight(hl);

        // Brighten the drop-target cell as the drag moves over it (expo's focus cue follows
        // the drag), reusing the SAME resolved cluster as the border highlight (Principle
        // I). Off any cell -> keep the current workspace bright.
        if (dim_node)
        {
            dim_node->set_active(hl >= 0 ? hl : current_ws_index);
        }
    }
}

void spread_overview_t::end_drag(wf::pointf_t release_local)
{
    if (border_node)
    {
        border_node->set_highlight(-1); // clear the drop-target highlight
    }

    // Restore the idle focus cue (current workspace bright). On a real relocate, reflow()
    // rebuilds the veil anyway; on a snap-back this is what un-brightens the drop target.
    if (dim_node)
    {
        dim_node->set_active(current_ws_index);
    }

    // Principle VI: the view vanished mid-drag -> nothing to move or snap back.
    if (!press_view || !press_view->is_mapped() || !thumbnails.count(press_view))
    {
        return;
    }

    // Resolve the drop from the dragged thumbnail's CENTER — the SAME value the highlight
    // used — so the window lands on exactly the cell that was highlighted (Principle I).
    auto center = dragged_thumb_center(release_local);
    auto target = hit_test_cluster(current_layout, center.x, center.y);
    auto source = output->wset()->get_view_main_workspace(press_view);

    if (target && ((target->x != source.x) || (target->y != source.y)))
    {
        // Dropped over a different workspace -> relocate for real, then stay-open reflow
        // so the moved thumbnail lands in its new cluster (FR-008/FR-010). Snapshot the
        // thumbnails' on-screen rects FIRST (before relocate moves the dragged view's
        // geometry) so the reflow can animate each one from where it is to its new slot.
        LOGI("spread-overview: drop -> relocate to ws (", target->x, ",", target->y, ")");
        snapshot_thumb_screen_rects();
        relocate(press_view, wf::point_t{target->x, target->y});
        reflow();
    }
    else
    {
        // Same workspace, or released outside all clusters -> glide back (002 FR-008; 001
        // teleported by assigning drag_orig_t*). The target is the layout SLOT, not the grab
        // position: after the press-freeze the grab can be a mid-flight spot, and the
        // thumbnail must finish where the layout wants it (spec US2 scenario 3).
        LOGI("spread-overview: drop -> snap back (same ws / outside)");
        animate_thumb_to_slot(press_view);
    }
}

void spread_overview_t::handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event event)
{
    if (state == session_state::DEACTIVATING)
    {
        return; // already animating out
    }

    if ((event.state == WL_KEYBOARD_KEY_STATE_PRESSED) && (event.keycode == KEY_ESC))
    {
        LOGI("spread-overview: Esc - deactivating");
        deactivate(true); // animate the thumbnails back out
    }
}
} // namespace spread
} // namespace wf
