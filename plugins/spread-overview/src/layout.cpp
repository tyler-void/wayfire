#include "layout.hpp"
#include <algorithm>
#include <cmath>

namespace wf
{
namespace spread
{
// Fixed-equal region sizing (R14 / invariant #9): the working area (output minus
// outer_margin) is divided into grid.x x grid.y equal cells separated by cluster_gap.
// Cluster (cx,cy) is exactly cell (cx,cy) — independent of content density.
static rectf cluster_region(ivec2 ws, ivec2 grid, dimf out, const layout_options& o)
{
    const double work_x = o.outer_margin;
    const double work_y = o.outer_margin;
    const double work_w = out.w - 2 * o.outer_margin;
    const double work_h = out.h - 2 * o.outer_margin;

    double cell_w = (work_w - (grid.x - 1) * o.cluster_gap) / grid.x;
    double cell_h = (work_h - (grid.y - 1) * o.cluster_gap) / grid.y;
    cell_w = std::max(cell_w, 1.0);
    cell_h = std::max(cell_h, 1.0);

    return rectf{
        work_x + ws.x * (cell_w + o.cluster_gap),
        work_y + ws.y * (cell_h + o.cluster_gap),
        cell_w, cell_h};
}

// Pack `members` into `region` at a single uniform scale (invariant #10). Every
// thumbnail sits centered in its own non-overlapping slot, so zero-overlap (inv #3)
// holds within the cluster; cluster regions are disjoint, so it holds across clusters.
// Returns whether the cluster is over-dense (uniform scale fell below min_scale).
static bool pack_cluster(const rectf& region, ivec2 ws,
    const std::vector<const layout_input_view*>& members,
    const layout_options& o, std::vector<view_out>& out)
{
    const int n = (int)members.size();
    if (n == 0)
    {
        return false;
    }

    // Column/row count from the region aspect, clamped to [1, n].
    const double aspect = region.w / std::max(1.0, region.h);
    int cols = (int)std::round(std::sqrt((double)n * aspect));
    cols = std::max(1, std::min(cols, n));
    const int rows = (n + cols - 1) / cols;

    double slot_w = (region.w - (cols + 1) * o.spacing) / cols;
    double slot_h = (region.h - (rows + 1) * o.spacing) / rows;
    slot_w = std::max(slot_w, 1.0);
    slot_h = std::max(slot_h, 1.0);

    // Uniform scale: the largest S <= max_scale at which every member fits its slot.
    double scale = o.max_scale;
    for (auto* v : members)
    {
        const double sw = slot_w / std::max(1.0, v->natural_size.w);
        const double sh = slot_h / std::max(1.0, v->natural_size.h);
        scale = std::min(scale, std::min(sw, sh));
    }

    const bool over_dense = scale < o.min_scale;

    for (int i = 0; i < n; i++)
    {
        const int r = i / cols;
        const int c = i % cols;
        const double slot_x = region.x + o.spacing + c * (slot_w + o.spacing);
        const double slot_y = region.y + o.spacing + r * (slot_h + o.spacing);
        const double tw = members[i]->natural_size.w * scale;
        const double th = members[i]->natural_size.h * scale;
        out.push_back(view_out{
            members[i]->id,
            rectf{slot_x + (slot_w - tw) / 2.0, slot_y + (slot_h - th) / 2.0, tw, th},
            ws});
    }
    return over_dense;
}

layout_result layout(const std::vector<layout_input_view>& views,
    ivec2 grid, dimf output_size, const layout_options& opts)
{
    grid.x = std::max(1, grid.x);
    grid.y = std::max(1, grid.y);

    layout_result res;

    // Cluster regions first, from geometry only — so they never depend on content
    // density (invariant #9). Row-major: (0,0),(1,0),... then next row.
    for (int cy = 0; cy < grid.y; cy++)
    {
        for (int cx = 0; cx < grid.x; cx++)
        {
            res.clusters.push_back(cluster_out{
                ivec2{cx, cy}, cluster_region(ivec2{cx, cy}, grid, output_size, opts), false});
        }
    }

    // Group views by source workspace and pack each cluster.
    for (auto& cluster : res.clusters)
    {
        std::vector<const layout_input_view*> members;
        for (auto& v : views)
        {
            if ((v.source_ws.x == cluster.ws.x) && (v.source_ws.y == cluster.ws.y))
            {
                members.push_back(&v);
            }
        }
        // Stable order by id → deterministic output (invariant #8).
        std::sort(members.begin(), members.end(),
            [] (const layout_input_view* a, const layout_input_view* b)
        {
            return a->id < b->id;
        });
        cluster.over_dense = pack_cluster(cluster.region, cluster.ws, members, opts, res.views);
    }

    return res;
}

std::optional<ivec2> hit_test_cluster(const layout_result& r, double px, double py)
{
    for (auto& c : r.clusters)
    {
        if ((px >= c.region.x) && (px < c.region.x + c.region.w) &&
            (py >= c.region.y) && (py < c.region.y + c.region.h))
        {
            return c.ws;
        }
    }
    return std::nullopt;
}
} // namespace spread
} // namespace wf
