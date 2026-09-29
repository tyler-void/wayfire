#include "layout.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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

// ---------------------------------------------------------------------------------
// 002 within-cluster layout (contract: specs/002-spread-refine/contracts/layout.md §2;
// ADR-004). A port of GNOME Shell's UnalignedLayoutStrategy + _createBestLayout
// (js/ui/workspace.js, main @ 0062bde) with four documented deviations D1-D4. Windows
// are packed into rows of variable width, each window sized individually — this
// REPLACES 001's "equal slots + one uniform scale per cluster" (001 invariant #10).
// The executable oracle is specs/002-spread-refine/reference/layout_ref.py; the C++
// must match its golden fixture within 0.5 px (invariant #13).
//
// Sizes are clamped to >= 1 wherever they are used AS sizes (W/H below); the emphasis
// ratio and the window centers deliberately use the RAW values.
// ---------------------------------------------------------------------------------
namespace
{
// One packing row (GNOME's LayoutRow). Internal to layout() — never crosses the
// module boundary, so layout_result is unchanged.
struct pack_row
{
    double fw = 0; // sum of emphasis-weighted widths of the windows actually in it
    double fh = 0; // max emphasis-weighted height of ITS OWN windows (D2)
    std::vector<const layout_input_view*> wins;
    double w = 0, h = 0;  // scaled row size
    double add = 1.0;     // per-row fit factor a_row in (0, 1]
    double x = 0, y = 0;  // row origin
};

struct pack_candidate
{
    std::vector<pack_row> rows;
    int    maxcols = 0;   // windows in the widest row
    double gw = 0;        // widest row's weighted width
    double gh = 0;        // sum of row heights
};

inline double pw(const layout_input_view *v)
{
    return std::max(1.0, v->natural_size.w);
}

inline double ph(const layout_input_view *v)
{
    return std::max(1.0, v->natural_size.h);
}

inline double pcx(const layout_input_view *v)
{
    return v->natural_pos.x + v->natural_size.w / 2.0;
}

inline double pcy(const layout_input_view *v)
{
    return v->natural_pos.y + v->natural_size.h / 2.0;
}

// GNOME _computeWindowScale = lerp(beta, 1, h / output_h): the shorter the window, the
// bigger its emphasis, up to beta. D3 clamps the ratio to [0, 1] so a window TALLER
// than the output cannot be pushed below 1 (GNOME does not clamp).
inline double emphasis(const layout_input_view *v, double beta, double out_h)
{
    const double ratio = std::min(std::max(v->natural_size.h / std::max(1.0, out_h), 0.0), 1.0);
    return beta + (1.0 - beta) * ratio;
}

// GNOME computeLayout: fill k rows in (cy, id) order, keeping a window in the current
// row while that brings the row closer to the ideal width.
pack_candidate compute_candidate(const std::vector<const layout_input_view*>& order,
    int k, double beta, double out_h)
{
    const int n = (int)order.size();

    double total = 0;
    for (auto *v : order)
    {
        total += pw(v) * emphasis(v, beta, out_h);
    }

    const double ideal = total / k; // total >= n >= 1, so never zero

    pack_candidate cand;
    cand.rows.resize(k);

    int idx = 0;
    for (int i = 0; i < k; i++)
    {
        auto& row = cand.rows[i];
        while (idx < n)
        {
            auto *v = order[idx];
            const double b = emphasis(v, beta, out_h);
            const double w = pw(v) * b;
            const double h = ph(v) * b;

            const bool keep = ((row.fw + w) <= ideal) ||
                (std::abs(1.0 - (row.fw + w) / ideal) < std::abs(1.0 - row.fw / ideal));

            if (keep || (i == k - 1)) // the last row always takes the remainder
            {
                row.wins.push_back(v);
                row.fw += w;
                row.fh  = std::max(row.fh, h); // D2: own windows only
                idx++;
            } else
            {
                break;
            }
        }
    }

    // D1: a row can stay empty when one window is >= 2x the ideal width; GNOME would
    // still charge it row spacing, so drop it.
    cand.rows.erase(std::remove_if(cand.rows.begin(), cand.rows.end(),
        [] (const pack_row& r) { return r.wins.empty(); }), cand.rows.end());

    for (auto& r : cand.rows)
    {
        std::sort(r.wins.begin(), r.wins.end(),
            [] (const layout_input_view *a, const layout_input_view *b)
        {
            const double ca = pcx(a), cb = pcx(b);
            return (ca != cb) ? (ca < cb) : (a->id < b->id);
        });
    }

    // The widest row sets the grid width; strict > so the FIRST row wins ties.
    const pack_row *maxrow = &cand.rows.front();
    for (size_t i = 1; i < cand.rows.size(); i++)
    {
        if (cand.rows[i].fw > maxrow->fw)
        {
            maxrow = &cand.rows[i];
        }
    }

    cand.maxcols = (int)maxrow->wins.size();
    cand.gw = maxrow->fw;
    for (auto& r : cand.rows)
    {
        cand.gh += r.fh;
    }

    return cand;
}

// GNOME computeScaleAndSpace.
void scale_space(const pack_candidate& c, double sp, double aw, double ah,
    double max_scale, double& s, double& space)
{
    const double hs = (c.maxcols - 1) * sp;
    const double vs = ((int)c.rows.size() - 1) * sp;
    s = std::min(std::min((aw - hs) / c.gw, (ah - vs) / c.gh), max_scale);
    space = ((c.gw * s + hs) * (c.gh * s + vs)) / (aw * ah);
}

// GNOME _isBetterScaleAndSpace: LAYOUT_SCALE_WEIGHT = 1, LAYOUT_SPACE_WEIGHT = 0.1.
bool better(double s0, double sp0, double s, double sp)
{
    const double scale_power = (s - s0) * 1.0;
    const double space_power = (sp - sp0) * 0.1;

    if ((s > s0) && (sp > sp0))
    {
        return true;
    }

    if ((s > s0) && (sp <= sp0))
    {
        return scale_power > space_power;
    }

    if ((s <= s0) && (sp > sp0))
    {
        return space_power > scale_power;
    }

    return false;
}

// D4 (found in review): GNOME reserves horizontal spacing only for the WIDEST row, so a
// row holding more (narrower) windows can need more spacing than the area has — which
// yields zero or negative thumbnail sizes, and s = 0 (a division by zero in placement)
// in very dense clusters. Such a row count is skipped instead.
bool feasible(const pack_candidate& c, double s, double sp, double aw, double ah)
{
    if ((s <= 0.0) || (((int)c.rows.size() - 1) * sp >= ah))
    {
        return false;
    }

    for (auto& r : c.rows)
    {
        if (((int)r.wins.size() - 1) * sp >= aw)
        {
            return false;
        }
    }

    return true;
}

// Lay `order` (already sorted by (cy, id)) out inside `region` at spacing `sp`.
// Returns false when no row count is feasible (D4) — the caller retries at spacing 0,
// which always succeeds (with sp = 0 every spacing test passes and s > 0, so k = 1 is
// always feasible). On success appends one view_out per member, in row order, and
// reports the smallest APPLIED per-window scale for the over-dense flag.
bool pack_with_spacing(const rectf& region, ivec2 ws,
    const std::vector<const layout_input_view*>& order,
    double sp, const layout_options& o, double out_h,
    std::vector<view_out>& out, double& min_applied_scale)
{
    const int n = (int)order.size();
    const double ax = region.x + sp;
    const double ay = region.y + sp;
    const double aw = std::max(region.w - 2 * sp, 1.0);
    const double ah = std::max(region.h - 2 * sp, 1.0);
    const double beta = std::max(1.0, o.small_window_boost);

    // GNOME _createBestLayout: try increasing row counts while the (scale, space)
    // objective improves; stop when the column count stops changing.
    pack_candidate best;
    bool have_best  = false;
    int last_cols   = -1;
    double s_best   = 0;
    double sp_best  = 0;

    for (int k = 1; k <= n; k++) // D4: bounded by n
    {
        const int cols = (n + k - 1) / k; // ceil(n / k)
        if (cols == last_cols)
        {
            break;
        }

        pack_candidate cand = compute_candidate(order, k, beta, out_h);
        double s = 0, space = 0;
        scale_space(cand, sp, aw, ah, o.max_scale, s, space);

        if (!feasible(cand, s, sp, aw, ah))
        {
            continue; // skip; last_cols deliberately untouched
        }

        if (have_best && !better(s_best, sp_best, s, space))
        {
            break;
        }

        best      = std::move(cand);
        have_best = true;
        last_cols = cols;
        s_best    = s;
        sp_best   = space;
    }

    if (!have_best)
    {
        return false;
    }

    // GNOME computeWindowSlots.
    const double scale = s_best;
    for (auto& r : best.rows)
    {
        r.w = r.fw * scale + ((int)r.wins.size() - 1) * sp;
        r.h = r.fh * scale;
    }

    double hwo = 0;
    for (auto& r : best.rows)
    {
        hwo += r.h;
    }

    const double vsp   = ((int)best.rows.size() - 1) * sp;
    const double add_v = std::min(1.0, (ah - vsp) / hwo);

    double comp = 0, y = 0;
    for (auto& r : best.rows)
    {
        const double hsp = ((int)r.wins.size() - 1) * sp;
        const double wwo = r.w - hsp;
        const double add_h = std::min(1.0, (aw - hsp) / wwo);

        if (add_h < add_v)
        {
            r.add = add_h;
            comp += (add_v - add_h) * r.h;
        } else
        {
            r.add = add_v;
        }

        r.x = ax + std::max(aw - (wwo * r.add + hsp), 0.0) / 2.0;
        r.y = ay + std::max(ah - (hwo + vsp), 0.0) / 2.0 + y;
        y  += r.h * r.add + sp;
    }

    comp /= 2.0;

    min_applied_scale = std::numeric_limits<double>::max();
    for (auto& r : best.rows)
    {
        const double row_y = r.y + comp;
        const double row_h = r.h * r.add;
        double x = r.x;

        for (auto *v : r.wins)
        {
            const double s_v = scale * emphasis(v, beta, out_h) * r.add;
            const double cw  = pw(v) * s_v;
            const double ch  = ph(v) * s_v;
            const double s_c = std::min(s_v, o.max_scale); // invariant #5: never upscale past the cap
            const double tw  = pw(v) * s_c;
            const double th  = ph(v) * s_c;
            const double tx  = x + (cw - tw) / 2.0;
            const double ty  = (best.rows.size() == 1) ?
                row_y + (row_h - th) / 2.0 : row_y + row_h - ch;

            out.push_back(view_out{v->id, rectf{tx, ty, tw, th}, ws});
            min_applied_scale = std::min(min_applied_scale, s_c);
            x += cw + sp;
        }
    }

    return true;
}
} // namespace

// Pack `members` into `region` with per-window scales (invariant #10 as replaced by
// 002). Zero-overlap (inv #3) holds within the cluster by construction, and cluster
// regions are disjoint, so it holds across clusters. Returns whether the cluster is
// over-dense — now "the SMALLEST per-window scale fell below min_scale".
static bool pack_cluster(const rectf& region, ivec2 ws,
    const std::vector<const layout_input_view*>& members,
    const layout_options& o, double out_h, std::vector<view_out>& out)
{
    if (members.empty())
    {
        return false;
    }

    // Row membership follows the windows' vertical position, ties by id (invariant #11).
    std::vector<const layout_input_view*> order = members;
    std::sort(order.begin(), order.end(),
        [] (const layout_input_view *a, const layout_input_view *b)
    {
        const double ca = pcy(a), cb = pcy(b);
        return (ca != cb) ? (ca < cb) : (a->id < b->id);
    });

    std::vector<view_out> packed;
    double min_applied_scale = 0;

    if (!pack_with_spacing(region, ws, order, o.spacing, o, out_h, packed, min_applied_scale))
    {
        // D4 fallback: spacing collapses to zero rather than producing zero-size or
        // overlapping thumbnails. Always feasible.
        packed.clear();
        pack_with_spacing(region, ws, order, 0.0, o, out_h, packed, min_applied_scale);
    }

    out.insert(out.end(), packed.begin(), packed.end());
    return min_applied_scale < o.min_scale;
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
        cluster.over_dense = pack_cluster(cluster.region, cluster.ws, members, opts,
            output_size.h, res.views);
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
