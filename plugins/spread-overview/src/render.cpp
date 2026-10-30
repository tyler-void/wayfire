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
#include <wayfire/util/log.hpp>

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
    // 002 (FR-005): small-window emphasis. The layout treats < 1.0 as 1.0 anyway; clamp
    // here too so the value the layout sees is the value the option means.
    o.small_window_boost = std::max(1.0, (double)opt_small_window_boost);
    return o;
}

void spread_overview_t::build_spread(spread_anim anim)
{
    auto wset = output->wset();
    auto all  = wset->get_views(wf::WSET_MAPPED_ONLY | wf::WSET_EXCLUDE_MINIMIZED);
    auto grid = wset->get_workspace_grid_size();

    // 002 (T026/FR-009): capture the stacking order ONCE per build — top-most first. The
    // hit-test walks it so a press during the entry/reflow animation, when the live rects
    // still overlap, grabs the thumbnail that is drawn on top rather than an arbitrary one.
    // WSET_SORT_STACKING is documented as slow, so it must never run per input event; the
    // order cannot change during a session because raising is descoped from the overview.
    // Any view the sorted query omits (not attached to the scenegraph) is appended at the
    // back, so membership always matches `all`.
    hit_order.clear();
    {
        auto stacked = wset->get_views(wf::WSET_MAPPED_ONLY | wf::WSET_EXCLUDE_MINIMIZED |
            wf::WSET_SORT_STACKING);
        for (auto& v : stacked)
        {
            if (std::find(all.begin(), all.end(), v) != all.end())
            {
                hit_order.push_back(v);
            }
        }

        for (auto& v : all)
        {
            if (std::find(hit_order.begin(), hit_order.end(), v) == hit_order.end())
            {
                hit_order.push_back(v);
            }
        }
    }
    auto og   = output->get_relative_geometry();

    // T011: (view, source_ws, natural_size) -> layout input. Enumeration index is
    // the stable layout id; session_views maps it back to the real view.
    // 002 (T016): also the view's position INSIDE ITS OWN WORKSPACE. View geometry is
    // in the current workspace's coordinates, so subtracting the workspace delta times
    // the output size rebases it onto its own workspace — the same basis move.cpp uses
    // to place a relocated view (`cell_x = (target_ws.x - cv.x) * W`). The layout reads
    // it to order rows by vertical position and within a row by horizontal position;
    // only consistency WITHIN one workspace matters, so straddling/maximized views need
    // no special case.
    auto cv = wset->get_current_workspace();
    std::vector<layout_input_view> inputs;
    session_views.clear();
    for (auto& v : all)
    {
        auto ws = wset->get_view_main_workspace(v);
        auto vg = v->get_geometry();
        inputs.push_back(layout_input_view{
            (uint32_t)session_views.size(),
            ivec2{ws.x, ws.y},
            dimf{(double)vg.width, (double)vg.height},
            pointf{
                (double)vg.x - (double)(ws.x - cv.x) * (double)og.width,
                (double)vg.y - (double)(ws.y - cv.y) * (double)og.height}});
        session_views.push_back(v);
    }

    auto result = layout(inputs, ivec2{grid.width, grid.height},
        dimf{(double)og.width, (double)og.height}, current_layout_options());
    current_layout = result; // kept for the drop hit-test (Principle I)

    // T029: note over-dense clusters (uniform thumbnail scale fell below min_scale — too many
    // or too-large windows to fit comfortably). The layout still yields zero-overlap slots, so
    // this is just an observability hook; a visual badge would be optional polish.
    {
        int dense = 0;
        for (auto& c : result.clusters)
        {
            if (c.over_dense) { dense++; }
        }

        if (dense > 0)
        {
            LOGI("spread-overview: ", dense, " over-dense workspace(s) — thumbnails below min_scale");
        }
    }

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
        v->connect(&view_unmapped); // T028: react if this window closes mid-session
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
    // for US2 drops (Principle I). 002: the drop-target highlight is a SEPARATE node on
    // top (highlight_node_t), so it can cross-fade between cells while this texture, the
    // static grid, is rendered once per layout.
    {
        std::vector<rectf> cluster_rects;
        for (auto& c : result.clusters)
        {
            cluster_rects.push_back(c.region);
        }

        const int bs = std::max(1, (int)opt_border_size);
        const int hs = std::max(1, (int)opt_highlight_size);

        border_node = std::make_shared<border_node_t>();
        border_node->set_content(og, cluster_rects, bs, (wf::color_t)opt_border_color);
        wf::scene::add_front(output->node_for_layer(wf::scene::layer::OVERLAY), border_node);

        highlight_node = std::make_shared<highlight_node_t>();
        highlight_node->set_content(og, std::move(cluster_rects), hs,
            (wf::color_t)opt_highlight_color);
        // add_front AFTER the grid -> drawn above it.
        wf::scene::add_front(output->node_for_layer(wf::scene::layer::OVERLAY), highlight_node);
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
        ensure_anim_hook();
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

    // T028: stop listening for unmaps on this session's views (build_spread reconnects).
    view_unmapped.disconnect();

    for (auto& label : label_nodes)
    {
        wf::scene::remove_child(label);
    }
    label_nodes.clear();

    // Border overlay torn down identically to the labels (constraint: restore intact).
    if (highlight_node)
    {
        wf::scene::remove_child(highlight_node);
        highlight_node.reset();
    }

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

    // 003: the camera is session state. Dropping the clock here is what guarantees the next
    // session starts at the exact identity (#C1) rather than inheriting a finished zoom.
    camera = camera_t{};
    camera_pivot_index = -1;
    camera_clock.reset();
    exit_target_ws.reset();

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
    hit_order.clear();
    current_layout = layout_result{};
    output->render->damage_whole();
}

// T027 entry: per-frame tick. Read each clock's interpolated value into its transformer
// (begin/end_transform_update so the scene damages the moved node), keep rendering while any
// clock runs, and drop the hook once they all settle at their slot (end) values.
void spread_overview_t::animate_step()
{
    bool any_running = false;

    // 003 (T020): tick the CAMERA first — every camera-driven thumbnail and all four chrome
    // nodes read it below, so it must be current before anything composes against it. The
    // camera only exists while a direction is playing; outside that camera_pivot_index is -1
    // and this whole block is skipped, leaving 002's behaviour bit-for-bit.
    const bool cam_live = camera_clock && (camera_pivot_index >= 0) &&
        (camera_pivot_index < (int)current_layout.clusters.size());
    wf::point_t pivot_ws{-1, -1};
    if (cam_live)
    {
        camera.t = (double)*camera_clock;
        auto& pc = current_layout.clusters[camera_pivot_index];
        pivot_ws = wf::point_t{pc.ws.x, pc.ws.y};
        push_camera_to_overlays();
        if (camera_clock->running())
        {
            any_running = true;
        }
    }

    for (auto& [v, a] : anim_state)
    {
        auto it = thumbnails.find(v);
        if (!v || !v->is_mapped() || (it == thumbnails.end()))
        {
            continue; // Principle VI: a view that vanished mid-animation is simply skipped
        }

        auto tr = it->second;

        // The clock's interpolated slot<->identity values: the BASE transform for this frame.
        double sx = (double)a.scale_x, sy = (double)a.scale_y;
        double tx = (double)a.translation_x, ty = (double)a.translation_y;

        // 003 (T021): the view's ROLE, derived every frame and never stored, so a view that was
        // relocated or unmapped cannot carry a stale one (Principle VI):
        //   pivot cell    -> SETTLING. Left exactly as 002 wrote it: it interpolates to identity,
        //                    i.e. to the window's real desktop position. The camera must NOT be
        //                    applied here — a slot position is a PACKED position, so mapping it
        //                    through the camera would land the window where the layout put it,
        //                    blown up, instead of where it really lives (FR-005).
        //   anything else -> CAMERA-DRIVEN. Its slot rect is mapped through the camera, so it
        //                    rides outward as the pivot cell grows to fill the screen.
        if (cam_live)
        {
            auto mw = output->wset()->get_view_main_workspace(v);
            if ((mw.x != pivot_ws.x) || (mw.y != pivot_ws.y))
            {
                auto vg = v->get_geometry();
                const double vw = std::max(1.0, (double)vg.width);
                const double vh = std::max(1.0, (double)vg.height);
                const double w  = vw * sx, h = vh * sy;
                const double cx = (vg.x + vg.width / 2.0) + tx;
                const double cy = (vg.y + vg.height / 2.0) + ty;
                // camera_rect is the SAME mapping the chrome nodes use (overlay.hpp), which is
                // why a thumbnail and the cell drawn behind it stay locked together.
                auto m = camera_rect(camera, rectf{cx - w / 2.0, cy - h / 2.0, w, h});
                sx = m.w / vw;
                sy = m.h / vh;
                tx = (m.x + m.w / 2.0) - (vg.x + vg.width / 2.0);
                ty = (m.y + m.h / 2.0) - (vg.y + vg.height / 2.0);
            }
        }

        v->get_transformed_node()->begin_transform_update();
        tr->scale_x = (float)sx;
        tr->scale_y = (float)sy;
        tr->translation_x = (float)tx;
        tr->translation_y = (float)ty;
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
        if (highlight_node) { highlight_node->alpha = oa; }
        output->render->damage_whole();
        any_running = true;
    }

    // 002 (FR-011): the drop-target highlight cross-fade. Its own fixed ~120 ms clock, so it
    // is independent of the thumbnail animations and keeps the hook alive on its own while a
    // drag moves between cells.
    if (highlight_fade.running())
    {
        const double p = (double)highlight_fade;
        if (highlight_node)
        {
            highlight_node->set_progress(p);
        }

        if (dim_node)
        {
            dim_node->set_progress(p); // 003 I0: the veil fades with it
        }

        any_running = true;
    } else
    {
        // settle: the incoming cell fully in. Both setters early-return when unchanged, so
        // this costs nothing on the frames where no fade is running.
        if (highlight_node)
        {
            highlight_node->set_progress(1.0);
        }

        if (dim_node)
        {
            dim_node->set_progress(1.0);
        }
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

    // 003 (R-210): resolve the DESTINATION first, and hand the viewport over before computing
    // anything. The order matters and is not interchangeable: "settle to identity" only means
    // "arrive at the real on-screen position" once the viewport is already on the destination,
    // because a view's geometry is expressed in the current viewport's frame. Doing it the
    // other way round animates the destination's windows straight off the screen.
    auto dest = exit_target_ws.value_or(output->wset()->get_current_workspace());
    exit_target_ws.reset(); // consumed — never allowed to leak into the next session
    auto cws = output->wset()->get_current_workspace();
    if ((dest.x != cws.x) || (dest.y != cws.y))
    {
        handoff_viewport(dest);
        cws = output->wset()->get_current_workspace();
    }

    // 003 (FR-002): the camera converges on the destination cell, which the handoff just made
    // the current one. Fail-soft: an unknown cell leaves the camera at the identity and the
    // close degrades to exactly 002's fade.
    if (set_camera_pivot(cws))
    {
        camera_clock = std::make_unique<wf::animation::simple_animation_t>(opt_exit_duration);
        camera_clock->animate(0.0, 1.0);
    }

    // The per-cluster labels are the one piece of chrome that is NOT camera-aware (they are
    // upstream simple_text_node_t). Rather than let them hang motionless in mid-air while
    // everything else zooms, drop them at the start of the close. Before 003 they stayed fully
    // opaque and then snapped away at teardown, which the fade never covered either.
    for (auto& label : label_nodes)
    {
        wf::scene::remove_child(label);
    }

    label_nodes.clear();

    // Destination-aware exit (research: fade, don't fly). The destination's windows SETTLE back
    // to their real positions (staying opaque); every OTHER workspace's windows hold their slot
    // transform, which the camera then carries outward as it fills the screen, and fade as they
    // go — no ballooning off-screen, and an empty destination just clears calmly.

    for (auto& [v, tr] : thumbnails)
    {
        if (!v || !v->is_mapped())
        {
            continue;
        }

        // 002 (FR-010): the close runs on exit_duration, not the entry/reflow duration.
        auto& a = anim_state.try_emplace(v, opt_exit_duration).first->second;
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
            // Hold the slot transform and drop opacity to 0. With a camera running, animate_step
            // maps this held rect through it every frame (T021), so the window rides outward
            // with its cell instead of fading in place; with no camera it is 002's fade exactly.
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

    if (!anim_state.empty() || overlay_fade.running() ||
        (camera_clock && camera_clock->running()))
    {
        ensure_anim_hook();
    }
}

// 003 (I2): point the camera at the cluster occupied by `ws`. The pivot rect is the cluster's
// region straight out of layout(), so the camera converges on exactly the cell that was drawn
// (Principle I — one geometry, no second opinion). Returns false and leaves the camera at the
// identity if there is no such cluster, which keeps the close working rather than failing.
bool spread_overview_t::set_camera_pivot(wf::point_t ws)
{
    camera = camera_t{};
    camera_pivot_index = -1;

    auto og = output->get_relative_geometry();
    for (size_t i = 0; i < current_layout.clusters.size(); i++)
    {
        auto& c = current_layout.clusters[i];
        if ((c.ws.x == ws.x) && (c.ws.y == ws.y))
        {
            camera_pivot_index = (int)i;
            camera.pivot  = c.region;
            camera.output = dimf{(double)og.width, (double)og.height};
            camera.t      = 0.0; // #C1: the identity until the clock moves it
            return true;
        }
    }

    LOGI("spread-overview: no cluster for ws (", ws.x, ",", ws.y,
        ") — camera stays at identity");
    return false;
}

// Hand the SAME camera to all four chrome nodes. One call site, so the chrome can never be a
// frame behind the thumbnails; each setter damages its own node.
void spread_overview_t::push_camera_to_overlays()
{
    if (dim_node)       { dim_node->set_camera(camera); }
    if (wallpaper_node) { wallpaper_node->set_camera(camera); }
    if (border_node)    { border_node->set_camera(camera); }
    if (highlight_node) { highlight_node->set_camera(camera); }
}

// 003 (R-210): move the viewport to `dest` with no motion of its own, leaving the frame looking
// identical. Two verified facts make this both necessary and cheap:
//
//  1. wset->set_workspace() MOVES every non-sticky view by (old - new) * output_size
//     (src/output/workspace-impl.cpp:594-617) and does not go through
//     workspace_change_request_signal, so vswitch never sees it — the switch is instant and the
//     camera stays the only motion (R-209, the same reason expo.cpp:349 uses it). The actual
//     call goes through switch_workspace() in move.cpp, which stays the only workspace-switch
//     contact (Principle II).
//  2. A thumbnail's rendered rect is view geometry + transformer translation. So subtracting the
//     view's own movement from its translation leaves the rendered rect bit-identical. Measured
//     per view rather than assumed from the viewport delta, because sticky views do not move at
//     all and would otherwise be displaced by a correction they never needed.
void spread_overview_t::handoff_viewport(wf::point_t dest)
{
    // pointf, not wf::point_t: wf::geometry_t is FLOATING-POINT on 0.12, so an int point here
    // would quantize the delta and shift every thumbnail by the sub-pixel remainder.
    std::map<wayfire_toplevel_view, pointf> before;
    for (auto& [v, tr] : thumbnails)
    {
        if (v && v->is_mapped())
        {
            auto g = v->get_geometry();
            before[v] = pointf{(double)g.x, (double)g.y};
        }
    }

    switch_workspace(dest);

    for (auto& [v, tr] : thumbnails)
    {
        auto it = before.find(v);
        if (!v || !v->is_mapped() || (it == before.end()))
        {
            continue; // Principle VI
        }

        auto g = v->get_geometry();
        const double dx = (double)g.x - it->second.x;
        const double dy = (double)g.y - it->second.y;
        if ((dx == 0.0) && (dy == 0.0))
        {
            continue; // sticky / fixed: it did not move, so nothing to undo
        }

        v->get_transformed_node()->begin_transform_update();
        tr->translation_x = (float)((double)tr->translation_x - dx);
        tr->translation_y = (float)((double)tr->translation_y - dy);
        v->get_transformed_node()->end_transform_update();
    }

    // The veil's bright cell is a different cell now. Retarget it through the normal path so it
    // cross-fades on the existing ~120 ms clock (-1 = "no drop target", which that function maps
    // to the current workspace) — otherwise the cell the camera is about to zoom INTO would be
    // the one still painted dim.
    auto cws = output->wset()->get_current_workspace();
    current_ws_index = -1;
    for (size_t i = 0; i < current_layout.clusters.size(); i++)
    {
        if ((current_layout.clusters[i].ws.x == cws.x) &&
            (current_layout.clusters[i].ws.y == cws.y))
        {
            current_ws_index = (int)i;
            break;
        }
    }

    set_drop_highlight(-1);
    LOGI("spread-overview: viewport handed off to ws (", dest.x, ",", dest.y,
        ") before the zoom; ", before.size(), " thumbnails re-expressed");
}

// 002 (I3): the thumbnail's live on-screen rect — where it is DRAWN right now, which is its
// slot only once the animation has settled. The transformer is scale-about-center + translate,
// so center = view center + translation and size = view size * scale. get_geometry() is in the
// current viewport's coordinates and the translation was built against it, so the result is
// output-local — the same space as thumb_rects and hit_test_cluster().
std::optional<rectf> spread_overview_t::live_thumb_rect(wayfire_toplevel_view view)
{
    auto it = thumbnails.find(view);
    if (!view || !view->is_mapped() || (it == thumbnails.end()))
    {
        return {}; // Principle VI
    }

    auto tr = it->second;
    auto vg = view->get_geometry();
    const double w  = vg.width * tr->scale_x;
    const double h  = vg.height * tr->scale_y;
    const double cx = (vg.x + vg.width / 2.0) + tr->translation_x;
    const double cy = (vg.y + vg.height / 2.0) + tr->translation_y;
    return rectf{cx - w / 2.0, cy - h / 2.0, w, h};
}

// One place that installs the per-frame tick (build_spread, start_exit_anim and the snap-back
// all route through it).
void spread_overview_t::ensure_anim_hook()
{
    if (!anim_hook_active)
    {
        output->render->add_effect(&anim_hook, wf::OUTPUT_EFFECT_PRE);
        anim_hook_active = true;
    }

    output->render->schedule_redraw();
}

// 002 (FR-011): point the highlight at a cluster and cross-fade to it. The node keeps the
// outgoing cell so both are drawn during the ramp; progress is written by animate_step.
// `cluster_index` is the DRAG TARGET: a cluster index while dragging over one, or -1 for
// "no drop target". It is NOT the veil's idle cell -- the veil's bright cell is derived here
// (-1 maps it to the current workspace), so callers must never pass current_ws_index.
void spread_overview_t::set_drop_highlight(int cluster_index)
{
    if (!highlight_node)
    {
        return;
    }

    // 003 I0: the veil's bright cell retargets on the SAME clock, so the two focus cues move
    // in lockstep. Before I0 the veil could only snap, because it was one baked output-sized
    // texture -- which is exactly why 002 deferred FR-011's second half.
    bool changed = highlight_node->set_highlight(cluster_index);
    if (dim_node && dim_node->set_active(cluster_index >= 0 ? cluster_index : current_ws_index))
    {
        changed = true;
    }

    if (changed)
    {
        highlight_fade.animate(0.0, 1.0);
        ensure_anim_hook();
    }
}

// 002 (I3): glide ONE thumbnail from where it is drawn now to its layout slot. Used by the
// cancelled-drop snap-back (FR-008) and by the release of a thumbnail that a press froze —
// both must target the SLOT, because after the press-freeze the grab position can be a
// mid-flight one (spec US2 scenario 3).
void spread_overview_t::animate_thumb_to_slot(wayfire_toplevel_view view)
{
    auto it  = thumbnails.find(view);
    auto rit = thumb_rects.find(view);
    if (!view || !view->is_mapped() || (it == thumbnails.end()) || (rit == thumb_rects.end()))
    {
        return; // Principle VI
    }

    auto tr = it->second;
    auto vg = view->get_geometry();
    const auto& slot = rit->second;
    const double s  = slot.w / std::max(1.0, (double)vg.width);
    const double tx = (slot.x + slot.w / 2.0) - (vg.x + vg.width / 2.0);
    const double ty = (slot.y + slot.h / 2.0) - (vg.y + vg.height / 2.0);

    // Already there (no animation was running and the drag never moved it) -> nothing to do,
    // so a plain press/release does not start a pointless clock.
    if ((std::abs(tr->translation_x - tx) < 0.5) && (std::abs(tr->translation_y - ty) < 0.5) &&
        (std::abs(tr->scale_x - s) < 1e-4))
    {
        return;
    }

    auto& a = anim_state.try_emplace(view, opt_duration).first->second;
    a.scale_x.set(tr->scale_x, s);
    a.scale_y.set(tr->scale_y, s);
    a.translation_x.set(tr->translation_x, tx);
    a.translation_y.set(tr->translation_y, ty);
    // MANDATORY: animate_step writes tr->alpha every frame, so a freshly constructed clock
    // would drive the thumbnail to alpha 0 and fade it out mid-snap-back.
    a.alpha.set(1.0, 1.0);
    a.start();
    ensure_anim_hook();
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
        (void)tr;
        if (auto r = live_thumb_rect(v)) // same math the hit-test uses (Principle I)
        {
            reflow_prev_rects[v] = *r;
        }
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

// T028: scrub a view from every per-session map and stop listening to it. Called the moment a
// window unmaps, so the non-owning observer_ptr is gone before the view object is destroyed —
// nothing can dereference a dead pointer afterwards.
void spread_overview_t::forget_view(wayfire_toplevel_view view)
{
    session_views.erase(
        std::remove(session_views.begin(), session_views.end(), view), session_views.end());
    hit_order.erase(std::remove(hit_order.begin(), hit_order.end(), view), hit_order.end());
    thumbnails.erase(view);
    thumb_rects.erase(view);
    saved_alpha.erase(view);
    anim_state.erase(view);
    reflow_prev_rects.erase(view);
    view->disconnect(&view_unmapped);
}

// T028 (Principle VI / FR-013): a window closed while the overview was open.
void spread_overview_t::handle_view_unmapped(wayfire_toplevel_view view)
{
    // If the vanished view was the drag subject (or a pending press), end the gesture cleanly —
    // no relocate, no dangling press_view — and reset the drop-target cues.
    if (press_view == view)
    {
        dragging   = false;
        pressed    = false;
        press_view = nullptr;
        // Fades the highlight out and returns the veil's bright cell to the current
        // workspace in one call (003 I0 unified the two cues onto one clock).
        set_drop_highlight(-1);
    }

    forget_view(view); // scrub the dead pointer NOW, before it is destroyed

    if (session_views.empty())
    {
        deactivate(false); // nothing left to show -> close immediately (mirrors scale::finalize)
        return;
    }

    // Rebuild the survivors on the next idle — out of the unmap emit and any in-progress scene
    // walk. Snapshot their current rects first so they glide to their new slots (REFLOW).
    snapshot_thumb_screen_rects();
    unmap_reflow_idle.run_once([this] ()
    {
        if (state == session_state::ACTIVE)
        {
            reflow();
        }
    });
}
} // namespace spread
} // namespace wf
