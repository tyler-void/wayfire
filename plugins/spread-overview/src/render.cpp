// spread-overview render wrapper (constitution Principle II — all scene/transform
// + alpha contact isolated here). T011 enumerate, T013 geometry, T015 alpha.

#include "overview.hpp"

#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-operations.hpp>

#include <algorithm>
#include <string>

namespace wf
{
namespace spread
{
static const std::string TRANSFORMER_NAME = "spread-overview";
static const std::string ALPHA_TRANSFORMER = "alpha"; // the alpha plugin / inactive-alpha daemon

// The view's "own alpha" is the value of its "alpha"-named view_2d_transformer, if
// present, else 1.0 (see research.md R10). Force it opaque for the session and
// remember the prior value so DEACTIVATING restores the daemon's state exactly.
static std::shared_ptr<wf::scene::view_2d_transformer_t> own_alpha_node(wayfire_toplevel_view v)
{
    return v->get_transformed_node()->get_transformer<wf::scene::view_2d_transformer_t>(
        ALPHA_TRANSFORMER);
}

layout_options spread_overview_t::current_layout_options()
{
    layout_options o; // max_scale / min_scale keep sensible defaults
    o.spacing      = (double)(int)opt_spacing;
    o.cluster_gap  = (double)(int)opt_cluster_gap;
    o.outer_margin = 0.0; // fill the whole output edge-to-edge (expo-style)
    return o;
}

void spread_overview_t::build_spread()
{
    auto wset = output->wset();
    auto all  = wset->get_views(wf::WSET_MAPPED_ONLY | wf::WSET_EXCLUDE_MINIMIZED);
    auto grid = wset->get_workspace_grid_size();
    auto og   = output->get_relative_geometry();

    // T011: (view, source_ws, natural_size) -> layout input. Enumeration index is
    // the stable layout id; session_views maps it back to the real view.
    std::vector<layout_input_view> inputs;
    session_views.clear();
    for (auto& v : all)
    {
        auto ws = wset->get_view_main_workspace(v);
        auto vg = v->get_geometry();
        inputs.push_back(layout_input_view{
            (uint32_t)session_views.size(),
            ivec2{ws.x, ws.y},
            dimf{(double)vg.width, (double)vg.height}});
        session_views.push_back(v);
    }

    auto result = layout(inputs, ivec2{grid.width, grid.height},
        dimf{(double)og.width, (double)og.height}, current_layout_options());
    current_layout = result; // kept for the drop hit-test (Principle I)

    // The current workspace's cluster is the veil's bright cell when idle (expo's focus
    // cue). Recomputed every build so it tracks the viewport across reflows.
    auto cws = wset->get_current_workspace();
    current_ws_index = -1;
    for (size_t i = 0; i < result.clusters.size(); i++)
    {
        if ((result.clusters[i].ws.x == cws.x) && (result.clusters[i].ws.y == cws.y))
        {
            current_ws_index = (int)i;
            break;
        }
    }

    for (auto& vo : result.views)
    {
        auto v  = session_views[vo.id];
        auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(v);
        v->get_transformed_node()->add_transformer(tr, wf::TRANSFORMER_2D + 1, TRANSFORMER_NAME);
        thumbnails[v]   = tr;
        thumb_rects[v]  = vo.target_rect;

        // T013 geometry: scale+translate onto the slot. get_geometry() is in global
        // coords, so an off-workspace view's translation delta pulls it on-screen.
        auto vg = v->get_geometry();
        const double s = vo.target_rect.w / std::max(1.0, (double)vg.width);
        tr->scale_x = tr->scale_y = (float)s;
        tr->translation_x = (float)((vo.target_rect.x + vo.target_rect.w / 2.0) -
            (vg.x + vg.width / 2.0));
        tr->translation_y = (float)((vo.target_rect.y + vo.target_rect.h / 2.0) -
            (vg.y + vg.height / 2.0));

        // T015: force opaque, capturing the daemon's dimming to restore on exit.
        if (auto a = own_alpha_node(v))
        {
            saved_alpha[v] = a->alpha;
            a->alpha = 1.0f;
        }
    }

    // T016 workspace borders: one overlay node strokes each cluster region — the
    // expo-style grid separating the workspaces. Same regions hit_test_cluster() uses
    // for US2 drops (Principle I). US2 highlights one via border_node->set_highlight().
    {
        std::vector<rectf> cluster_rects;
        for (auto& c : result.clusters)
        {
            cluster_rects.push_back(c.region);
        }
        const int bs = std::max(1, (int)opt_border_size);
        border_node = std::make_shared<border_node_t>();
        border_node->set_content(og, std::move(cluster_rects),
            bs, (wf::color_t)opt_border_color, bs * 2, wf::color_t{0.3, 0.6, 1.0, 1.0});
        wf::scene::add_front(output->node_for_layer(wf::scene::layer::OVERLAY), border_node);
    }

    // Per-workspace dim veil (expo + scale merge). Sits at the BACK of the WORKSPACE layer
    // — above the wallpaper/bottom panels, below the view thumbnails — so windows stay
    // crisp while each workspace's backdrop dims. The current workspace reads bright; the
    // drop-target brightens during a drag (input.cpp drives set_active()). Same cluster
    // regions as the border/hit-test (Principle I).
    {
        std::vector<rectf> dim_rects;
        for (auto& c : result.clusters)
        {
            dim_rects.push_back(c.region);
        }
        dim_node = std::make_shared<dim_node_t>();
        dim_node->set_content(og, std::move(dim_rects),
            (wf::color_t)opt_background, (double)opt_inactive_brightness, current_ws_index);
        wf::scene::add_back(output->node_for_layer(wf::scene::layer::WORKSPACE), dim_node);
    }

    // Per-cell wallpaper (B2) — the expo-style tile backdrop. Blits the wallpaper loaded
    // ONCE in wallpaper.cpp (B1), cover-cropped, into each cell. add_back AFTER the dim veil
    // so it lands at the very BACK of the WORKSPACE layer: wallpaper < dim veil < thumbnails
    // — each inactive cell shows a dimmed desktop, the active cell a bright one, thumbnails
    // crisp on top. FAIL-SOFT: only built when the B1 load succeeded (wallpaper_ok); if it
    // failed the node is skipped entirely and cells fall back to the real desktop showing
    // through (today's look). Isolated behind wallpaper_ok so a load failure never alters
    // the rest of the spread.
    if (wallpaper_ok)
    {
        std::vector<rectf> wp_rects;
        for (auto& c : result.clusters)
        {
            wp_rects.push_back(c.region);
        }
        wallpaper_node = std::make_shared<wallpaper_node_t>();
        wallpaper_node->set_content(og, std::move(wp_rects),
            wallpaper_tex.get_texture(), wallpaper_size);
        wf::scene::add_back(output->node_for_layer(wf::scene::layer::WORKSPACE), wallpaper_node);
    }

    // T016: one workspace label per cluster (incl. empty — they are US2 drop targets),
    // in the output OVERLAY layer so it renders on top of the thumbnails.
    if (opt_show_labels)
    {
        auto layer = output->node_for_layer(wf::scene::layer::OVERLAY);
        for (auto& c : result.clusters)
        {
            wf::cairo_text_t::params tp;
            tp.font_size  = 16;
            tp.bg_color   = wf::color_t{0.1, 0.1, 0.1, 0.75};
            tp.text_color = wf::color_t{1.0, 1.0, 1.0, 1.0};
            tp.exact_size = true;

            auto label = std::make_shared<simple_text_node_t>();
            label->set_text_params(tp);
            label->set_text(std::to_string(c.ws.y * grid.width + c.ws.x + 1));
            label->set_position({c.region.x + 8.0, c.region.y + 8.0});
            wf::scene::add_front(layer, label);
            label_nodes.push_back(label);
        }
    }

    output->render->damage_whole();
}

void spread_overview_t::clear_spread()
{
    for (auto& label : label_nodes)
    {
        wf::scene::remove_child(label);
    }
    label_nodes.clear();

    // Border overlay torn down identically to the labels (constraint: restore intact).
    if (border_node)
    {
        wf::scene::remove_child(border_node);
        border_node.reset();
    }

    // Dim veil torn down the same way (it lives in the WORKSPACE layer, but remove_child
    // detaches from whatever parent it has — no view state touched, Principle V).
    if (dim_node)
    {
        wf::scene::remove_child(dim_node);
        dim_node.reset();
    }

    // Wallpaper tile torn down identically (also a WORKSPACE-layer child; remove_child
    // detaches from whatever parent it has). The plugin's wallpaper_tex texture is NOT
    // freed here — it stays loaded for the plugin lifetime and is reused every open.
    if (wallpaper_node)
    {
        wf::scene::remove_child(wallpaper_node);
        wallpaper_node.reset();
    }
    current_ws_index = -1;

    for (auto& [v, tr] : thumbnails)
    {
        if (v && v->is_mapped())
        {
            v->get_transformed_node()->rem_transformer(TRANSFORMER_NAME);
        }
    }

    // Restore each view's own alpha exactly (Principle V — don't clobber the daemon).
    for (auto& [v, prev] : saved_alpha)
    {
        if (v && v->is_mapped())
        {
            if (auto a = own_alpha_node(v))
            {
                a->alpha = prev;
            }
        }
    }

    thumbnails.clear();
    thumb_rects.clear();
    saved_alpha.clear();
    session_views.clear();
    current_layout = layout_result{};
    output->render->damage_whole();
}

// Stay-open rebuild after a relocate (FR-010): tear the spread down and rebuild it, so
// the moved view lands in its new workspace cluster. State stays ACTIVE (grab kept).
// Animation of the moved thumbnail is deferred to T027.
void spread_overview_t::reflow()
{
    clear_spread();
    build_spread();
}
} // namespace spread
} // namespace wf
