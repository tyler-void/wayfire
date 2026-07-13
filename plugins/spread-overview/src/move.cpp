// spread-overview relocate wrapper (constitution Principle II — the only view-move
// contact). Same-output workspace move, exactly the call vswitch uses.
// Fullscreen re-request (R5) lands as a separate checkpoint (sub-step B checkpoint 4).

#include "overview.hpp"

#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/toplevel-view.hpp>

#include <algorithm>
#include <cmath>

namespace wf
{
namespace spread
{
void spread_overview_t::relocate(wayfire_toplevel_view view, wf::point_t target_ws)
{
    if (!view || !view->is_mapped())
    {
        return;
    }

    // Sticky / on-all-workspaces views live on EVERY workspace at once, so "move to a
    // workspace" is meaningless (core's move_to_workspace itself forces ws=current for
    // them). No-op the drop — end_drag's reflow then snaps the thumbnail back to its slot.
    // (Edge case #7 from the straddle research.)
    if (view->sticky)
    {
        return;
    }

    // A maximized/tiled or fullscreen view has its geometry PINNED to a workspace by that
    // state. A plain move_to_workspace() only calls view->move(), which the maximize/tile
    // constraint immediately reverts — so the view never actually leaves its source
    // workspace. (Observed bug: a maximized window logs "relocate to (X,Y)" on every drop
    // yet stays put, because get_view_main_workspace keeps reporting the old workspace, so
    // each drop re-detects the same move as needed.) Mirror core's adjust_view_on_output
    // (plugins/common/move-drag-interface.cpp): re-issue the fullscreen/tile request
    // TARGETING the destination workspace, which re-pins the constrained geometry there,
    // then move_to_workspace as a visibility guarantee. Capture the state BEFORE any move,
    // since the request itself changes pending_*.
    const bool was_fullscreen  = view->pending_fullscreen();
    const uint32_t tiled_edges = view->pending_tiled_edges();

    if (was_fullscreen || tiled_edges)
    {
        if (was_fullscreen)
        {
            // Checkpoint 4 (R5): fullscreen must re-fullscreen on the TARGET, not the source.
            wf::get_core().default_wm->fullscreen_request(view, output, true, target_ws);
        }
        else
        {
            // Maximized == tiled on all edges: re-tile on the target so the maximize geometry
            // is pinned to the destination workspace instead of reverted to the source.
            wf::get_core().default_wm->tile_request(view, tiled_edges, target_ws);
        }

        // A tiled/fullscreen view fills exactly one workspace, so it never straddles; this
        // guarantees visibility on the target (no-op if the request already placed it there).
        output->wset()->move_to_workspace(view, target_ws);
        return;
    }

    // Floating view: snap it FULLY INTO the target cell (the discrete "drop = it's here now"
    // guarantee). We do NOT call wset->move_to_workspace() here — its overlap guard is
    // exactly what prevented dropping a straddling window onto a workspace it already
    // partially overlaps. snap_into_workspace() re-centers guard-free and clamps to fit.
    snap_into_workspace(view, target_ws);
}

void spread_overview_t::snap_into_workspace(wayfire_toplevel_view view, wf::point_t target_ws)
{
    auto og = output->get_relative_geometry();      // current viewport: W x H at (0,0)
    const double W = std::max(1.0, (double)og.width);
    const double H = std::max(1.0, (double)og.height);
    auto cv  = output->wset()->get_current_workspace();
    auto box = view->get_pending_geometry();

    // Target cell origin in output-local coords (the current viewport sits at 0,0, so a
    // workspace N cells away is at N*size). Same basis hit_test_cluster / the layout use.
    const double cell_x = (target_ws.x - cv.x) * W;
    const double cell_y = (target_ws.y - cv.y) * H;

    // The window's offset WITHIN a workspace cell, via modular arithmetic — the same idea as
    // core move_to_workspace's `cx % width`, so we preserve where the window sits in its cell
    // (a left-of-viewport window has box.x < 0; fmod is normalized back into [0, size)).
    const auto in_cell = [] (double v, double size)
    {
        double r = std::fmod(v, size);
        return (r < 0.0) ? r + size : r;
    };
    double off_x = in_cell((double)box.x, W);
    double off_y = in_cell((double)box.y, H);

    // Clamp so a window that FITS lands entirely inside the cell (true de-straddle). A window
    // larger than a workspace cannot fit, so center the overflow (best effort — its center
    // still lands on the target, so it reads as belonging there).
    off_x = (box.width  <= W) ? std::clamp(off_x, 0.0, W - box.width)  : (W - box.width)  / 2.0;
    off_y = (box.height <= H) ? std::clamp(off_y, 0.0, H - box.height) : (H - box.height) / 2.0;

    view->move((int)std::lround(cell_x + off_x), (int)std::lround(cell_y + off_y));
}

void spread_overview_t::switch_workspace(wf::point_t target_ws)
{
    // Empty-cell click -> go to that workspace (expo behavior). Called after deactivate(),
    // so the overview is already closing when the viewport change (with vswitch animation)
    // fires.
    output->wset()->request_workspace(target_ws);
}
} // namespace spread
} // namespace wf
