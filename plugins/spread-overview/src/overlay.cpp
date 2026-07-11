#include "overlay.hpp"

#include <cairo/cairo.h>
#include <algorithm>

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
} // namespace spread
} // namespace wf
