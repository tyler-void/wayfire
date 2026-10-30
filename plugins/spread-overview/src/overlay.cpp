#include "overlay.hpp"

#include <algorithm>
#include <cmath>

namespace wf
{
namespace spread
{
void border_node_t::set_content(wf::geometry_t og,
    std::vector<rectf> cluster_rects,
    int bs, wf::color_t bc)
{
    geometry     = og;
    clusters     = std::move(cluster_rects);
    border_size  = std::max(1, bs);
    border_color = bc;
    build_lines();
    wf::scene::damage_node(this->shared_from_this(), geometry);
}

// One rect per grid boundary. The grid is a uniform lattice, so the distinct cell edges in x
// and in y ARE the gridlines; drawing them directly means an interior boundary shared by two
// cells is painted exactly once (003 I0 / R-204).
void border_node_t::build_lines()
{
    lines.clear();
    if (clusters.empty())
    {
        return;
    }

    const auto uniq = [] (std::vector<double> v)
    {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end(),
            [] (double a, double b) { return std::abs(a - b) < 0.01; }), v.end());
        return v;
    };

    std::vector<double> xs, ys;
    double gx0 = clusters[0].x, gx1 = clusters[0].x + clusters[0].w;
    double gy0 = clusters[0].y, gy1 = clusters[0].y + clusters[0].h;
    for (const auto& c : clusters)
    {
        xs.push_back(c.x); xs.push_back(c.x + c.w);
        ys.push_back(c.y); ys.push_back(c.y + c.h);
        gx0 = std::min(gx0, c.x); gx1 = std::max(gx1, c.x + c.w);
        gy0 = std::min(gy0, c.y); gy1 = std::max(gy1, c.y + c.h);
    }

    xs = uniq(std::move(xs));
    ys = uniq(std::move(ys));

    // Centred ON the boundary, as the cairo strokes were, so the grid sits in the same pixels
    // as before. The extra `bs` of length squares off the corners.
    const double bs = border_size;
    for (double x : xs)
    {
        lines.push_back(rectf{x - bs / 2.0, gy0 - bs / 2.0, bs, (gy1 - gy0) + bs});
    }

    for (double y : ys)
    {
        lines.push_back(rectf{gx0 - bs / 2.0, y - bs / 2.0, (gx1 - gx0) + bs, bs});
    }
}

void border_node_t::render_lines(const wf::scene::render_instruction_t& data)
{
    if (alpha <= 0.001f)
    {
        return;
    }

    auto col = border_color;
    col.a *= alpha; // the exit dissolve
    for (const auto& l : lines)
    {
        const auto m = camera_rect(cam, l); // scales with the zoom, thickness included
        if ((m.w > 0) && (m.h > 0))
        {
            data.pass->add_rect(col, data.target,
                wf::geometry_t{m.x, m.y, m.w, m.h}, data.damage);
        }
    }
}

/* ------------------------------ dim_node_t ------------------------------- */
void dim_node_t::set_content(wf::geometry_t og,
    std::vector<rectf> cluster_rects,
    wf::color_t veil, double brightness, int active_index)
{
    geometry   = og;
    clusters   = std::move(cluster_rects);
    veil_color = veil;
    inactive_brightness = std::min(1.0, std::max(0.0, brightness));
    active      = active_index;
    prev_active = active_index; // no fade on a fresh build
    progress    = 1.0;
    build_strips();
    wf::scene::damage_node(this->shared_from_this(), geometry);
}

// The complement of the cells inside `geometry`, as non-overlapping rects: full-width strips
// for the row gaps and the top/bottom margins, then per-row-band strips for the column gaps
// and the left/right margins. Splitting the vertical strips by row band is what stops a
// horizontal and a vertical gap painting the same pixel twice at a crossing.
void dim_node_t::build_strips()
{
    strips.clear();
    if (clusters.empty())
    {
        return;
    }

    const auto spans = [] (std::vector<std::pair<double, double>> v)
    {
        std::sort(v.begin(), v.end());
        std::vector<std::pair<double, double>> out;
        for (auto& s : v)
        {
            if (!out.empty() && (s.first <= out.back().second + 0.01))
            {
                out.back().second = std::max(out.back().second, s.second);
            } else
            {
                out.push_back(s);
            }
        }

        return out;
    };

    std::vector<std::pair<double, double>> cols, rows;
    for (const auto& c : clusters)
    {
        cols.emplace_back(c.x, c.x + c.w);
        rows.emplace_back(c.y, c.y + c.h);
    }

    cols = spans(std::move(cols));
    rows = spans(std::move(rows));

    const double x0 = geometry.x, x1 = geometry.x + geometry.width;
    const double y0 = geometry.y, y1 = geometry.y + geometry.height;

    // Row gaps + top/bottom margins: full width.
    double cursor = y0;
    for (const auto& r : rows)
    {
        if (r.first > cursor + 0.01)
        {
            strips.push_back(rectf{x0, cursor, x1 - x0, r.first - cursor});
        }

        cursor = std::max(cursor, r.second);
    }

    if (y1 > cursor + 0.01)
    {
        strips.push_back(rectf{x0, cursor, x1 - x0, y1 - cursor});
    }

    // Column gaps + left/right margins, clipped to each row band so they cannot overlap the
    // strips above.
    for (const auto& rb : rows)
    {
        double cx = x0;
        for (const auto& c : cols)
        {
            if (c.first > cx + 0.01)
            {
                strips.push_back(rectf{cx, rb.first, c.first - cx, rb.second - rb.first});
            }

            cx = std::max(cx, c.second);
        }

        if (x1 > cx + 0.01)
        {
            strips.push_back(rectf{cx, rb.first, x1 - cx, rb.second - rb.first});
        }
    }
}

bool dim_node_t::set_active(int cluster_index)
{
    if (cluster_index == active)
    {
        return false;
    }

    // The cell we were showing bright becomes the outgoing one — unless a previous fade is
    // still mid-flight, in which case it has already been drawn away.
    prev_active = (progress >= 1.0) ? active : -1;
    active      = cluster_index;
    progress    = 0.0;
    wf::scene::damage_node(this->shared_from_this(), geometry);
    return true;
}

void dim_node_t::set_progress(double p)
{
    const double clamped = std::min(1.0, std::max(0.0, p));
    if (clamped == progress)
    {
        return; // nothing moved -> do NOT damage the whole output
    }

    progress = clamped;
    wf::scene::damage_node(this->shared_from_this(), geometry);
}

double dim_node_t::cell_veil(int index) const
{
    const bool is_cur  = (index == active);
    const bool was_prev = (index == prev_active);

    if (is_cur && was_prev)
    {
        return 0.0; // stayed bright
    }

    if (is_cur)
    {
        return 1.0 - progress; // fading to bright
    }

    if (was_prev)
    {
        return progress; // fading back to dim
    }

    return 1.0; // always dim
}

void dim_node_t::render_veil(const wf::scene::render_instruction_t& data)
{
    if (alpha <= 0.001f)
    {
        return;
    }

    // expo's model: an inactive cell sits at `inactive_brightness`, so its veil alpha is
    // veil.a * (1 - brightness). Identical arithmetic to the cairo version it replaces.
    const double base = veil_color.a * (1.0 - inactive_brightness);

    const auto put = [&] (const rectf& r, double strength)
    {
        const double a = base * strength * alpha;
        if ((a <= 0.001) || (r.w <= 0) || (r.h <= 0))
        {
            return;
        }

        auto col = veil_color;
        col.a = a;
        const auto m = camera_rect(cam, r);
        data.pass->add_rect(col, data.target,
            wf::geometry_t{m.x, m.y, m.w, m.h}, data.damage);
    };

    for (size_t i = 0; i < clusters.size(); i++)
    {
        put(clusters[i], cell_veil((int)i));
    }

    // Gaps and margins are never "the active cell", so they stay at full veil strength.
    for (const auto& st : strips)
    {
        put(st, 1.0);
    }
}

/* --------------------------- highlight_node_t ---------------------------- */
void highlight_node_t::set_content(wf::geometry_t og, std::vector<rectf> cluster_rects,
    int hs, wf::color_t hc)
{
    geometry   = og;
    clusters   = std::move(cluster_rects);
    size       = std::max(1, hs);
    color      = hc;
    cur_index  = -1;
    prev_index = -1;
    progress   = 1.0;
}

bool highlight_node_t::set_highlight(int cluster_index)
{
    if (cluster_index == cur_index)
    {
        return false;
    }

    // The cell we were showing becomes the outgoing one — unless a previous cross-fade is
    // still mid-flight, in which case it has already been drawn away and keeping it would
    // leave three cells lit.
    prev_index = (progress >= 1.0) ? cur_index : -1;
    cur_index  = cluster_index;
    progress   = 0.0;
    wf::scene::damage_node(this->shared_from_this(), geometry);
    return true;
}

void highlight_node_t::set_progress(double p)
{
    const double clamped = std::min(1.0, std::max(0.0, p));
    if (clamped == progress)
    {
        return; // nothing moved -> do NOT damage the whole output
    }

    progress = clamped;
    wf::scene::damage_node(this->shared_from_this(), geometry);
}

void highlight_node_t::render_cell(const wf::scene::render_instruction_t& data,
    int index, double a)
{
    if ((index < 0) || (index >= (int)clusters.size()) || (a <= 0.001) || (alpha <= 0.001f))
    {
        return;
    }

    auto col = color;
    col.a *= a * alpha; // the cross-fade AND the overview's exit dissolve

    // wf::geometry_t is floating point on 0.12 (upstream "standardize floating-point
    // rendering helpers"), so the rects stay in double — no rounding needed.
    const auto mc = camera_rect(cam, clusters[index]);
    const double x = mc.x, y = mc.y, w = mc.w, h = mc.h;
    // the stroke scales with the zoom too, so the highlight stays proportional to its cell
    const double s = std::min((double)size * camera_scale(cam), std::min(w, h) / 2.0);

    // Four strokes INSIDE the cell edges, so the highlight never bleeds into a neighbour
    // (the grid strokes sit centred on the shared edge; this one must not paint over it).
    const wf::geometry_t top{x, y, w, s};
    const wf::geometry_t bottom{x, y + h - s, w, s};
    const wf::geometry_t left{x, y + s, s, h - 2 * s};
    const wf::geometry_t right{x + w - s, y + s, s, h - 2 * s};

    for (const auto& box : {top, bottom, left, right})
    {
        if ((box.width > 0) && (box.height > 0))
        {
            data.pass->add_rect(col, data.target, box, data.damage);
        }
    }
}

/* --------------------------- wallpaper_node_t ---------------------------- */
void wallpaper_node_t::set_content(wf::geometry_t og,
    std::vector<rectf> cell_rects,
    std::shared_ptr<wf::texture_t> texture, wf::dimensions_t tex_size)
{
    geometry = og;
    tex      = std::move(texture);

    // 003: each tile is the largest OUTPUT-ASPECT rect centred in its cell — NOT the whole
    // cell. This is what makes the hand-off to the real desktop seamless, and it is exact
    // rather than close enough.
    //
    // The desktop wallpaper is drawn as cover-crop(image, output aspect) -> the output rect.
    // For the zoom to be invisible, the overview must arrive at that same image-pixel-to-
    // screen-pixel mapping at t = 1. Taking the cell's own aspect instead (which is what this
    // did before) framed the picture differently, because a cell is not proportional to the
    // output once outer_margin / cluster_gap are involved: on 3440x1440, 3x3, gap 0 a cell is
    // 2.4286:1 against the screen's 2.3889:1. The zoom therefore ended on a differently
    // cropped copy of the same image, which is the ghosted "silhouette" seen as the chrome
    // faded out.
    //
    // Proof that this lands exactly, with cell (cw, ch), output (ow, oh), A = ow/oh, and the
    // camera's cover scale at t = 1 being k = max(ow/cw, oh/ch) (#C2):
    //   - cell wider than A: the tile is height-limited, tile = (ch*A, ch). Then
    //     ow/cw < ow/tile.w = oh/ch, so k = oh/ch and tile*k = (ch*A*oh/ch, oh) = (ow, oh).
    //   - cell narrower than A: the tile is width-limited, tile = (cw, cw/A). Then
    //     oh/ch < oh/tile.h = ow/cw, so k = ow/cw and tile*k = (ow, cw/A*ow/cw) = (ow, oh).
    // The tile is centred in its cell, the cell's centre maps to the output's centre at t = 1
    // (#C2), and the camera is a uniform scale about that centre — so the mapped tile is
    // EXACTLY the output rect, for any grid and any output. Verified numerically for gap 0 and
    // gap 40 on 3440x1440: tile*k = 3440.0 x 1440.0 in both.
    //
    // The visible cost at rest is a slim letterbox inside each cell (9.3 px per side with the
    // default gap of 0; 27.8 px at gap 40), which is what expo's cells look like. The veil
    // covers it on every inactive cell.
    //
    // wf::geometry_t is floating point on master (= geometryf_t), so these pass through with
    // no rounding. NOTE: the veil, border and hit_test_cluster still use the FULL cell regions
    // (Principle I) — only the wallpaper is inset, because only the wallpaper has to agree
    // with something drawn outside the overview.
    const double out_aspect = (og.height > 0) ?
        ((double)og.width / (double)og.height) : 1.0;

    cells.clear();
    for (const auto& r : cell_rects)
    {
        double w = r.w, h = r.h;
        if ((r.w > 0.0) && (r.h > 0.0))
        {
            if ((r.w / r.h) > out_aspect)
            {
                w = r.h * out_aspect; // cell is wider: keep its height
                h = r.h;
            } else
            {
                w = r.w;              // cell is narrower: keep its width
                h = r.w / out_aspect;
            }
        }

        cells.push_back(wf::geometry_t{
            r.x + (r.w - w) / 2.0, r.y + (r.h - h) / 2.0, w, h});
    }

    // Cover-crop the source against the OUTPUT's aspect — the same crop a wallpaper daemon
    // computes to fill the screen. Together with the output-aspect tile above, this makes the
    // overview's image-pixel-to-screen-pixel mapping at t = 1 identical to the real desktop's,
    // which is the whole point: cropping to the CELL's aspect (as this did) is what produced
    // the visible silhouette. Every tile is output-aspect now, so ONE source box still serves
    // them all; it is set on the shared texture_t once and reused for every add_texture. If the
    // texture size is unknown we skip the box entirely -> the renderer uses the full texture (a
    // plain stretch), still a valid fallback (never a crash).
    if (tex && (tex_size.width > 0) && (tex_size.height > 0))
    {
        const double tw = tex_size.width;
        const double th = tex_size.height;
        const double tex_aspect = tw / th;

        wlr_fbox box;
        if (tex_aspect > out_aspect)
        {
            // Wallpaper is wider than the screen -> keep full height, crop left/right.
            box.height = th;
            box.width  = th * out_aspect;
            box.x = (tw - box.width) / 2.0;
            box.y = 0.0;
        }
        else
        {
            // Wallpaper is taller than the screen -> keep full width, crop top/bottom.
            box.width  = tw;
            box.height = tw / out_aspect;
            box.x = 0.0;
            box.y = (th - box.height) / 2.0;
        }

        tex->set_source_box(box);
    }

    wf::scene::damage_node(this->shared_from_this(), geometry);
}
} // namespace spread
} // namespace wf
