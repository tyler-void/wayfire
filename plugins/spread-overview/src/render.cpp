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

void spread_overview_t::build_spread(spread_anim anim)
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
        const double s  = vo.target_rect.w / std::max(1.0, (double)vg.width);
        const double tx = (vo.target_rect.x + vo.target_rect.w / 2.0) - (vg.x + vg.width / 2.0);
        const double ty = (vo.target_rect.y + vo.target_rect.h / 2.0) - (vg.y + vg.height / 2.0);

        if (anim == spread_anim::NONE)
        {
            tr->scale_x = tr->scale_y = (float)s;
            tr->translation_x = (float)tx;
            tr->translation_y = (float)ty;
        }
        else
        {
            // Animated: choose the START transform for this mode, then glide to the slot over
            // `duration`. Set the transformer to the START now so the first frame shows the
            // start, not a flash of the final slot; animate_step ticks it to the slot.
            double s0 = s, tx0 = tx, ty0 = ty;
            if (anim == spread_anim::ENTRY)
            {
                // From the window's real position (scale 1, no translation): off-workspace
                // windows enter from their grid direction (the loved directional slide-in).
                s0 = 1.0; tx0 = 0.0; ty0 = 0.0;
            }
            else // REFLOW
            {
                // From where the thumbnail visually WAS (captured pre-relocate), re-expressed
                // against the view's NEW geometry so the rendered rect starts unchanged and
                // glides to the slot. No snapshot (unexpected) -> just appear at the slot.
                auto pit = reflow_prev_rects.find(v);
                if (pit != reflow_prev_rects.end())
                {
                    const auto& old = pit->second;
                    s0  = old.w / std::max(1.0, (double)vg.width);
                    tx0 = (old.x + old.w / 2.0) - (vg.x + vg.width / 2.0);
                    ty0 = (old.y + old.h / 2.0) - (vg.y + vg.height / 2.0);
                }
            }

            auto& a = anim_state.try_emplace(v, opt_duration).first->second;
            a.scale_x.set(s0, s);
            a.scale_y.set(s0, s);
            a.translation_x.set(tx0, tx);
            a.translation_y.set(ty0, ty);
            a.alpha.set(1.0, 1.0); // entry/reflow never fade — keep opaque
            a.start();
            tr->scale_x = tr->scale_y = (float)s0;
            tr->translation_x = (float)tx0;
            tr->translation_y = (float)ty0;
            tr->alpha = 1.0f;
        }

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
        const int hs = std::max(1, (int)opt_highlight_size);
        border_node = std::make_shared<border_node_t>();
        border_node->set_content(og, std::move(cluster_rects),
            bs, (wf::color_t)opt_border_color, hs, (wf::color_t)opt_highlight_color);
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

    // T027: if we set up transitions (entry or reflow), install the per-frame tick hook and
    // kick a redraw so the clocks advance. animate_step removes the hook once they all settle.
    if ((anim != spread_anim::NONE) && !anim_state.empty())
    {
        if (!anim_hook_active)
        {
            output->render->add_effect(&anim_hook, wf::OUTPUT_EFFECT_PRE);
            anim_hook_active = true;
        }
        output->render->schedule_redraw();
    }

    output->render->damage_whole();
}

void spread_overview_t::clear_spread()
{
    // Stop the animation tick and drop its per-view clocks before the transformers they drive
    // are removed below (Principle V — no view state left touched, no dangling hook).
    if (anim_hook_active)
    {
        output->render->rem_effect(&anim_hook);
        anim_hook_active = false;
    }
    anim_state.clear();

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

// T027 entry: per-frame tick. Read each clock's interpolated value into its transformer
// (begin/end_transform_update so the scene damages the moved node), keep rendering while any
// clock runs, and drop the hook once they all settle at their slot (end) values.
void spread_overview_t::animate_step()
{
    bool any_running = false;
    for (auto& [v, a] : anim_state)
    {
        auto it = thumbnails.find(v);
        if (!v || !v->is_mapped() || (it == thumbnails.end()))
        {
            continue; // Principle VI: a view that vanished mid-animation is simply skipped
        }

        auto tr = it->second;
        v->get_transformed_node()->begin_transform_update();
        tr->scale_x = (float)(double)a.scale_x;
        tr->scale_y = (float)(double)a.scale_y;
        tr->translation_x = (float)(double)a.translation_x;
        tr->translation_y = (float)(double)a.translation_y;
        tr->alpha = (float)(double)a.alpha;
        v->get_transformed_node()->end_transform_update();

        if (a.running())
        {
            any_running = true;
        }
    }

    // Exit overlay fade (A2): dissolve the veil / wallpaper / grid alongside the windows, so
    // the overview fades out as one instead of the grid cutting away at teardown.
    if (overlay_fade.running())
    {
        const float oa = (float)(double)overlay_fade;
        if (dim_node)       { dim_node->alpha      = oa; }
        if (wallpaper_node) { wallpaper_node->alpha = oa; }
        if (border_node)    { border_node->alpha    = oa; }
        output->render->damage_whole();
        any_running = true;
    }

    if (any_running)
    {
        output->render->schedule_redraw();
    }
    else if (anim_hook_active)
    {
        output->render->rem_effect(&anim_hook);
        anim_hook_active = false;

        // If this was the EXIT animation (A2), run the real teardown — but OUTSIDE this render
        // hook, which mutates the scene graph and drops the input grab. Defer to the next idle.
        if (state == session_state::DEACTIVATING)
        {
            finish_idle.run_once([this] () { finish_deactivate(); });
        }
    }
}

// A2 exit: animate every thumbnail from its current transform back to its real position
// (identity — scale 1, no translation), the reverse of the entry slide-in. finish_deactivate
// (deferred out of animate_step) tears the session down once the clocks settle.
void spread_overview_t::start_exit_anim()
{
    anim_state.clear();

    // Destination-aware exit (research: fade, don't fly). On a dismiss the destination is the
    // current viewport: its windows SETTLE back to their real positions (staying opaque), while
    // every OTHER workspace's windows FADE OUT in place — no ballooning off-screen, and an
    // empty destination just clears calmly instead of a flock of windows flying past.
    auto cws = output->wset()->get_current_workspace();

    for (auto& [v, tr] : thumbnails)
    {
        if (!v || !v->is_mapped())
        {
            continue;
        }

        auto& a = anim_state.try_emplace(v, opt_duration).first->second;
        auto mw = output->wset()->get_view_main_workspace(v);
        const bool on_destination = (mw.x == cws.x) && (mw.y == cws.y);

        if (on_destination)
        {
            // Settle to the real desktop position (identity), staying opaque.
            a.scale_x.set(tr->scale_x, 1.0);
            a.scale_y.set(tr->scale_y, 1.0);
            a.translation_x.set(tr->translation_x, 0.0);
            a.translation_y.set(tr->translation_y, 0.0);
            a.alpha.set(tr->alpha, 1.0);
        }
        else
        {
            // Fade out where it sits — hold the transform, drop opacity to 0.
            a.scale_x.set(tr->scale_x, tr->scale_x);
            a.scale_y.set(tr->scale_y, tr->scale_y);
            a.translation_x.set(tr->translation_x, tr->translation_x);
            a.translation_y.set(tr->translation_y, tr->translation_y);
            a.alpha.set(tr->alpha, 0.0);
        }

        a.start();
    }

    // Fade the veil / wallpaper / grid out in lockstep with the windows (applied per-frame in
    // animate_step) so the whole overview dissolves rather than the grid snapping at teardown.
    overlay_fade.animate(1.0, 0.0);

    if (!anim_state.empty() || overlay_fade.running())
    {
        if (!anim_hook_active)
        {
            output->render->add_effect(&anim_hook, wf::OUTPUT_EFFECT_PRE);
            anim_hook_active = true;
        }
        output->render->schedule_redraw();
    }
}

// Snap every thumbnail straight to its final slot (the transition END) and drop the hook.
// Called on drag start so the drag-follow reads a stable translation and the per-frame tick
// never fights the cursor (the entry animation and the drag are sequential, never concurrent).
void spread_overview_t::finalize_entry_anim()
{
    if (!anim_hook_active)
    {
        return;
    }

    for (auto& [v, a] : anim_state)
    {
        auto it = thumbnails.find(v);
        if (!v || !v->is_mapped() || (it == thumbnails.end()))
        {
            continue;
        }

        auto tr = it->second;
        tr->scale_x = (float)a.scale_x.end;
        tr->scale_y = (float)a.scale_y.end;
        tr->translation_x = (float)a.translation_x.end;
        tr->translation_y = (float)a.translation_y.end;
        tr->alpha = (float)a.alpha.end;
    }

    output->render->rem_effect(&anim_hook);
    anim_hook_active = false;
    output->render->damage_whole();
}

// Capture each thumbnail's current on-screen (output-local) rect, so a following REFLOW can
// animate it from here to its new slot. Called in end_drag BEFORE relocate() moves the dragged
// view's geometry. The transformer is a scale-about-center + translate, so the rendered rect is
// center = view.center + translation, size = view.size * scale.
void spread_overview_t::snapshot_thumb_screen_rects()
{
    reflow_prev_rects.clear();
    for (auto& [v, tr] : thumbnails)
    {
        if (!v || !v->is_mapped())
        {
            continue;
        }

        auto vg = v->get_geometry();
        const double w  = vg.width  * tr->scale_x;
        const double h  = vg.height * tr->scale_y;
        const double cx = (vg.x + vg.width  / 2.0) + tr->translation_x;
        const double cy = (vg.y + vg.height / 2.0) + tr->translation_y;
        reflow_prev_rects[v] = rectf{cx - w / 2.0, cy - h / 2.0, w, h};
    }
}

// Stay-open rebuild after a relocate (FR-010): tear the spread down and rebuild it, so the
// moved view lands in its new workspace cluster. State stays ACTIVE (grab kept). Rebuilt with
// REFLOW so every thumbnail glides from its pre-relocate on-screen rect (snapshot above) to its
// new slot — the moved window flies into its target cluster instead of popping (T027 A3).
void spread_overview_t::reflow()
{
    clear_spread();
    build_spread(spread_anim::REFLOW);
}
} // namespace spread
} // namespace wf
