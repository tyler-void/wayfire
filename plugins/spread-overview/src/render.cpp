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
    layout_options o; // outer_margin / max_scale / min_scale keep sensible defaults
    o.spacing     = (double)(int)opt_spacing;
    o.cluster_gap = (double)(int)opt_cluster_gap;
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
    output->render->damage_whole();
}
} // namespace spread
} // namespace wf
