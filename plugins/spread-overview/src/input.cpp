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

wf::pointf_t spread_overview_t::dragged_thumb_center(wf::pointf_t cursor_local)
{
    auto it = thumb_rects.find(press_view);
    if (it == thumb_rects.end())
    {
        return cursor_local; // no dragged thumbnail (shouldn't happen mid-drag) — fall back
    }

    // The thumbnail's layout-center plus the drag delta since press == its current
    // on-screen center (the transformer translation moved it by exactly that delta).
    const auto& r = it->second;
    return {
        r.x + r.w / 2.0 + (cursor_local.x - press_pos.x),
        r.y + r.h / 2.0 + (cursor_local.y - press_pos.y)};
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
            deactivate();
            switch_workspace(wf::point_t{tgt->x, tgt->y});
        }
        else
        {
            // Outside all cells -> just close, no switch.
            LOGI("spread-overview: empty click (outside all cells) -> close");
            deactivate();
        }
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
        // so the moved thumbnail lands in its new cluster (FR-008/FR-010).
        LOGI("spread-overview: drop -> relocate to ws (", target->x, ",", target->y, ")");
        relocate(press_view, wf::point_t{target->x, target->y});
        reflow();
    }
    else
    {
        // Same workspace, or released outside all clusters -> snap back (FR-009).
        auto tr = thumbnails[press_view];
        tr->translation_x = (float)drag_orig_tx;
        tr->translation_y = (float)drag_orig_ty;
        output->render->damage_whole();
        LOGI("spread-overview: drop -> snap back (same ws / outside)");
    }
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
