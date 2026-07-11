// spread-overview relocate wrapper (constitution Principle II — the only view-move
// contact). Same-output workspace move, exactly the call vswitch uses.
// Fullscreen re-request (R5) lands as a separate checkpoint (sub-step B checkpoint 4).

#include "overview.hpp"

#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/toplevel-view.hpp>

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

    const bool was_fullscreen = view->pending_fullscreen();
    output->wset()->move_to_workspace(view, target_ws);

    // Checkpoint 4 (R5): a fullscreen window must re-fullscreen on the TARGET workspace,
    // not the source. A distinct step — not folded into the ordinary move.
    if (was_fullscreen)
    {
        wf::get_core().default_wm->fullscreen_request(view, output, true, target_ws);
    }
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
