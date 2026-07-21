#pragma once

// spread-overview session: the per-output plugin instance + state machine.
// This increment (tasks T009/T010) implements build-step-1: activator toggle,
// input grab, clean teardown — ZERO rendering. Render/input/relocate land in
// T011+ (see specs/001-spread-overview/tasks.md and data-model.md).

#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/bindings.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/util.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <wayfire/plugins/common/simple-text-node.hpp>

#include <memory>
#include <vector>
#include <map>

#include "layout.hpp"
#include "overlay.hpp"

namespace wf
{
namespace spread
{
// Per-thumbnail animation clock (T027): a duration_t carrying four timed transitions for the
// transformer's scale + translation. Mirrors scale's scale_animation_t. The transitions bind
// to *this, so instances MUST live in a node-stable container (std::map) — never moved after
// construction, or the self-bound clock pointers would dangle.
class thumb_anim_t : public wf::animation::duration_t
{
  public:
    using duration_t::duration_t;
    wf::animation::timed_transition_t scale_x{*this};
    wf::animation::timed_transition_t scale_y{*this};
    wf::animation::timed_transition_t translation_x{*this};
    wf::animation::timed_transition_t translation_y{*this};
    // Opacity (T027 A2 exit): destination-workspace windows stay opaque and settle; every
    // other window fades to 0 in place instead of ballooning off-screen. Entry/reflow keep
    // it pinned at 1 (set start==end==1) so those windows never fade.
    wf::animation::timed_transition_t alpha{*this};
};
// State machine (data-model.md). Only IDLE/ACTIVE are exercised this increment;
// ACTIVATING/DRAGGING/DEACTIVATING become meaningful with animation + drag (T011+).
enum class session_state
{
    IDLE,
    ACTIVATING,
    ACTIVE,
    DRAGGING,
    DEACTIVATING,
};

// How build_spread() places the thumbnails (T027):
//   NONE   — snap straight to the slot (used by nothing now; kept for a possible instant path)
//   ENTRY  — animate in from each window's real position (open); the loved directional slide-in
//   REFLOW — animate from each thumbnail's PRE-reflow on-screen rect to its new slot (after a
//            relocate), so the moved window glides into its target workspace instead of popping
enum class spread_anim
{
    NONE,
    ENTRY,
    REFLOW,
};

class spread_overview_t : public wf::per_output_plugin_instance_t,
    public wf::pointer_interaction_t,
    public wf::keyboard_interaction_t
{
  public:
    void init() override;
    void fini() override;

    // wf::pointer_interaction_t
    void handle_pointer_button(const wlr_pointer_button_event& event) override;
    void handle_pointer_motion(wf::pointf_t position, uint32_t time_ms) override;

    // wf::keyboard_interaction_t
    void handle_keyboard_key(wf::seat_t *seat, wlr_keyboard_key_event event) override;

  private:
    session_state state = session_state::IDLE;
    std::unique_ptr<wf::input_grab_t> grab;

    wf::option_wrapper_t<wf::activatorbinding_t> opt_toggle{"spread-overview/toggle"};

    wf::plugin_activation_data_t grab_interface = {
        .name = "spread-overview",
        .capabilities = wf::CAPABILITY_MANAGE_DESKTOP | wf::CAPABILITY_GRAB_INPUT,
        .cancel = [this] () { deactivate(); },
    };

    wf::activator_callback toggle_cb = [this] (auto) -> bool
    {
        return toggle();
    };

    bool toggle();
    bool activate();
    // animate=true (Esc/toggle/dismiss): play the exit animation, then tear down when it
    // settles. animate=false (click-to-navigate, grab cancel): tear down immediately.
    void deactivate(bool animate = true);
    // The real teardown: clear_spread + ungrab + deactivate_plugin + IDLE. Runs immediately
    // for a non-animated close, or (deferred via finish_idle) once the exit animation ends.
    void finish_deactivate();

    // Per-session spread state (T011/T013). Views in enumeration order; the
    // scale/translate transformer per view; the on-screen (output-local) rect per
    // view for click hit-testing; the captured pre-session "own alpha" per view.
    std::vector<wayfire_toplevel_view> session_views;
    std::map<wayfire_toplevel_view, std::shared_ptr<wf::scene::view_2d_transformer_t>> thumbnails;
    std::map<wayfire_toplevel_view, rectf> thumb_rects;
    std::map<wayfire_toplevel_view, float> saved_alpha; // views that were dimmed pre-session (T015)

    // Config (T019). background is read by the dim veil; duration drives the animations (T027).
    wf::option_wrapper_t<wf::animation_description_t> opt_duration{"spread-overview/duration"};
    wf::option_wrapper_t<int> opt_drag_threshold{"spread-overview/drag_threshold"};
    wf::option_wrapper_t<int> opt_spacing{"spread-overview/spacing"};
    wf::option_wrapper_t<int> opt_cluster_gap{"spread-overview/cluster_gap"};
    wf::option_wrapper_t<bool> opt_show_labels{"spread-overview/show_ws_labels"};
    wf::option_wrapper_t<int> opt_border_size{"spread-overview/border_size"};
    wf::option_wrapper_t<wf::color_t> opt_border_color{"spread-overview/border_color"};
    wf::option_wrapper_t<wf::color_t> opt_highlight_color{"spread-overview/highlight_color"};
    wf::option_wrapper_t<int> opt_highlight_size{"spread-overview/highlight_size"};
    wf::option_wrapper_t<wf::color_t> opt_background{"spread-overview/background"};
    wf::option_wrapper_t<double> opt_inactive_brightness{"spread-overview/inactive_brightness"};
    wf::option_wrapper_t<std::string> opt_wallpaper_path{"spread-overview/wallpaper_path"};

    // Per-cluster workspace labels (T016) + the thumbnail border overlay, both in the
    // output OVERLAY layer (above the thumbnails), both torn down the same way in
    // clear_spread(). dim_node is the per-workspace veil, in the WORKSPACE layer at the
    // back (BELOW the thumbnails). current_ws_index is the cluster of the current
    // workspace — the veil's bright cell when not dragging (recomputed each build_spread).
    std::vector<std::shared_ptr<simple_text_node_t>> label_nodes;
    std::shared_ptr<border_node_t> border_node;
    std::shared_ptr<dim_node_t> dim_node;
    std::shared_ptr<wallpaper_node_t> wallpaper_node; // B2: per-cell wallpaper tile, backmost
    int current_ws_index = -1;

    // Entry/exit/reflow animation (T027; A1 = entry). anim_state holds one clock+transitions
    // per thumbnail; anim_hook ticks them into the transformers each frame while any is
    // running (a per-frame OUTPUT_EFFECT_PRE effect, added on open, removed when animation
    // settles or the spread is torn down). std::map for node stability (see thumb_anim_t).
    std::map<wayfire_toplevel_view, thumb_anim_t> anim_state;
    wf::effect_hook_t anim_hook = [this] () { animate_step(); };
    bool anim_hook_active = false;
    // The exit teardown must not run from inside the render hook (it mutates the scene graph
    // and drops the input grab); animate_step defers it here to the next event-loop idle (A2).
    wf::wl_idle_call finish_idle;

    // Overlay opacity fade for the exit (A2): the dim veil, wallpaper tiles and grid fade out
    // together with the windows so the overview dissolves as one, instead of the grid snapping
    // away at teardown. Driven by the same `duration`; only runs during an animated close.
    wf::animation::simple_animation_t overlay_fade{opt_duration};

    // Each thumbnail's on-screen (output-local) rect captured just BEFORE a relocate, so the
    // REFLOW rebuild can animate every thumbnail from where it visually was to its new slot
    // (the moved window glides to its target cluster; others slide as the layout reflows).
    std::map<wayfire_toplevel_view, rectf> reflow_prev_rects;

    // Per-cell wallpaper (B1: load + upload + report ONLY; the per-cell blit is B2). The
    // ONLY image-file / texture-upload contact is wallpaper.cpp (Principle II). This is a
    // plain file->texture upload (cairo -> owned_texture_t), NOT a live-scene capture — it
    // deliberately sidesteps aux-buffer render passes / cross-GPU buffer handling. Loaded
    // ONCE on first activate(). FAIL-SOFT: unset path / missing file / decode / upload
    // failure leaves wallpaper_ok=false and the overview falls back to today's look (no
    // per-cell wallpaper), NEVER a crash.
    wf::owned_texture_t wallpaper_tex;
    wf::dimensions_t wallpaper_size{0, 0};
    bool wallpaper_load_attempted = false;
    bool wallpaper_ok = false;

    // The layout of the current spread — kept so a drop can hit_test_cluster() against
    // the exact regions that were rendered (Principle I).
    layout_result current_layout;

    // Click vs drag (T017/T020/T024). press_view is the pressed/dragged thumbnail; the
    // threshold on movement is the ONLY thing separating a click (focus+close) from a
    // drag (follow + relocate). drag_orig_t* is the transformer translation at drag
    // start, so snap-back / follow are computed as a delta from the layout position.
    bool pressed = false;
    bool dragging = false;
    wf::pointf_t press_pos;
    wayfire_toplevel_view press_view;
    double drag_orig_tx = 0.0, drag_orig_ty = 0.0;

    // render.cpp (Principle II — scene/transform + alpha + label contact isolated here).
    layout_options current_layout_options();
    void build_spread(spread_anim anim = spread_anim::NONE);
    void clear_spread();
    void reflow(); // stay-open rebuild after a relocate (FR-010) — animates via REFLOW

    // Animation (T027). animate_step ticks the transitions into the transformers each frame;
    // finalize_entry_anim snaps every thumbnail to its final slot and drops the hook (called on
    // drag start so the drag-follow never fights the animation). snapshot_thumb_screen_rects
    // captures each thumbnail's on-screen rect BEFORE a relocate, so reflow can animate from it.
    void animate_step();
    void finalize_entry_anim();
    void snapshot_thumb_screen_rects();
    void start_exit_anim(); // A2: animate every thumbnail back to its real position (identity)

    // move.cpp — the only view-relocation / workspace-switch contact (Principle II).
    void relocate(wayfire_toplevel_view view, wf::point_t target_ws);
    void switch_workspace(wf::point_t target_ws);

    // Place a FLOATING view fully inside the target workspace cell — the discrete "drop =
    // it's here now" guarantee (research decision). Preserves the window's in-cell offset,
    // clamps so it fits entirely inside the cell, and centers it if it is larger than a
    // workspace. Deliberately guard-free (unlike wset->move_to_workspace, which no-ops when
    // the window already overlaps the target) so a straddling window can be dropped onto a
    // workspace it partially overlaps.
    void snap_into_workspace(wayfire_toplevel_view view, wf::point_t target_ws);

    // wallpaper.cpp — the only image-file load + texture-upload contact (Principle II).
    // B1: load once, report, draw nothing. B2 will add the per-cell blit.
    void load_wallpaper();

    // input.cpp — output-local hit-test against thumb_rects + drag end (drop → relocate
    // or snap-back).
    wayfire_toplevel_view thumb_at(wf::pointf_t local);
    void end_drag(wf::pointf_t release_local);

    // Single source of truth for "which cell is the drag over": the dragged thumbnail's
    // on-screen CENTER given the current cursor. Both the drop-target highlight and the
    // drop resolution use this, so what lights up is what receives the window (Principle
    // I) — critical for large/maximized thumbnails where the cursor is far from center.
    wf::pointf_t dragged_thumb_center(wf::pointf_t cursor_local);
};
} // namespace spread
} // namespace wf
