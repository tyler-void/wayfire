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
    int bs, wf::color_t bc, int hs, wf::color_t hc)
{
    geometry       = og;
    clusters       = std::move(cluster_rects);
    border_size    = std::max(1, bs);
    border_color   = bc;
    highlight_size = std::max(1, hs);
    highlight_color = hc;
    highlighted    = -1;
    rerender();
}

void border_node_t::set_highlight(int cluster_index)
{
    if (cluster_index == highlighted)
    {
        return;
    }

    highlighted = cluster_index;
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
    for (size_t i = 0; i < clusters.size(); i++)
    {
        if ((int)i != highlighted)
        {
            stroke(clusters[i], border_size, border_color);
        }
    }

    // The highlighted cluster (US2 drop target) drawn last, in the highlight style.
    if ((highlighted >= 0) && (highlighted < (int)clusters.size()))
    {
        stroke(clusters[highlighted], highlight_size, highlight_color);
    }

    cairo_surface_flush(surface);
    tex = wf::owned_texture_t{surface};
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    wf::scene::damage_node(this->shared_from_this(), geometry);
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
