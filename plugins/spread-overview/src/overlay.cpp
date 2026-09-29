#include "overlay.hpp"

#include <cairo/cairo.h>
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
    rerender();
}

void border_node_t::rerender()
{
    const int W = std::max(1, (int)geometry.width);
    const int H = std::max(1, (int)geometry.height);

    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    auto cr = cairo_create(surface);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    const auto stroke = [&] (const rectf& r, double lw, const wf::color_t& col)
    {
        cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
        cairo_set_line_width(cr, lw);
        // Stroke centered ON the region edges (no inset). At cluster_gap=0 adjacent cells
        // share an edge, so their strokes coincide into ONE gridline — single thickness,
        // no doubling. hit_test_cluster is unaffected: regions are half-open [x, x+w), so
        // a boundary pixel belongs to exactly one cluster.
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        cairo_stroke(cr);
    };

    // Normal border around every workspace cluster (the expo-style separating grid).
    // 002: uniformly — the drop-target highlight is highlight_node_t's job now, drawn
    // above this one, so this texture no longer changes while dragging.
    for (size_t i = 0; i < clusters.size(); i++)
    {
        stroke(clusters[i], border_size, border_color);
    }

    cairo_surface_flush(surface);
    tex = wf::owned_texture_t{surface};
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    wf::scene::damage_node(this->shared_from_this(), geometry);
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
    const auto& c = clusters[index];
    const double x = c.x, y = c.y, w = c.w, h = c.h;
    const double s = std::min((double)size, std::min(w, h) / 2.0);

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

/* ------------------------------ dim_node_t ------------------------------- */
void dim_node_t::set_content(wf::geometry_t og,
    std::vector<rectf> cluster_rects,
    wf::color_t veil, double brightness, int active_index)
{
    geometry     = og;
    clusters     = std::move(cluster_rects);
    veil_color   = veil;
    inactive_brightness = std::clamp(brightness, 0.0, 1.0);
    active       = active_index;
    rerender();
}

void dim_node_t::set_active(int cluster_index)
{
    if (cluster_index == active)
    {
        return;
    }

    active = cluster_index;
    rerender();
}

void dim_node_t::rerender()
{
    const int W = std::max(1, (int)geometry.width);
    const int H = std::max(1, (int)geometry.height);

    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    auto cr = cairo_create(surface);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);

    // Inactive-cell veil strength follows expo's model: alpha = veil.a * (1 - brightness).
    // Fill the WHOLE output first (so inter-cluster gaps at cluster_gap>0 are dimmed too),
    // then punch the active cell fully transparent below.
    const double a = veil_color.a * (1.0 - inactive_brightness);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, veil_color.r, veil_color.g, veil_color.b, a);
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_fill(cr);

    // The active cluster (current workspace when idle, drop-target while dragging) reads
    // fully bright: clear the veil over exactly its region. CLEAR removes coverage, which
    // OVER cannot — this is why we fill-then-punch rather than fill-inactive-cells.
    if ((active >= 0) && (active < (int)clusters.size()))
    {
        const auto& r = clusters[active];
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        cairo_fill(cr);
    }

    cairo_surface_flush(surface);
    tex = wf::owned_texture_t{surface};
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    wf::scene::damage_node(this->shared_from_this(), geometry);
}

/* --------------------------- wallpaper_node_t ---------------------------- */
void wallpaper_node_t::set_content(wf::geometry_t og,
    std::vector<rectf> cell_rects,
    std::shared_ptr<wf::texture_t> texture, wf::dimensions_t tex_size)
{
    geometry = og;
    tex      = std::move(texture);

    // Destination cells: the layout's per-workspace regions. wf::geometry_t is floating
    // point on master (= geometryf_t), so the rectf fields pass straight through with no
    // rounding — the SAME exact regions the veil/border draw and hit_test_cluster() resolves
    // drops to (Principle I).
    cells.clear();
    for (const auto& r : cell_rects)
    {
        cells.push_back(wf::geometry_t{r.x, r.y, r.w, r.h});
    }

    // Cover-crop the source: fit the wallpaper into a cell preserving aspect and
    // center-cropping the overflow — exactly what a wallpaper daemon does to fill a screen,
    // so each cell matches the user's real desktop instead of a stretched/squished copy.
    // The expo grid's cells are uniform, so ONE source box (from cell[0]) serves them all;
    // it is set on the shared texture_t once and reused for every add_texture. If the size
    // is unknown we skip the box entirely -> the renderer uses the full texture (a plain
    // stretch), still a valid fallback (never a crash).
    if (tex && !cells.empty() && (tex_size.width > 0) && (tex_size.height > 0))
    {
        const double tw = tex_size.width;
        const double th = tex_size.height;
        const double cw = std::max(1.0, cells[0].width);
        const double ch = std::max(1.0, cells[0].height);
        const double tex_aspect  = tw / th;
        const double cell_aspect = cw / ch;

        wlr_fbox box;
        if (tex_aspect > cell_aspect)
        {
            // Wallpaper is wider than the cell -> keep full height, crop left/right.
            box.height = th;
            box.width  = th * cell_aspect;
            box.x = (tw - box.width) / 2.0;
            box.y = 0.0;
        }
        else
        {
            // Wallpaper is taller than the cell -> keep full width, crop top/bottom.
            box.width  = tw;
            box.height = tw / cell_aspect;
            box.x = 0.0;
            box.y = (th - box.height) / 2.0;
        }

        tex->set_source_box(box);
    }

    wf::scene::damage_node(this->shared_from_this(), geometry);
}
} // namespace spread
} // namespace wf
