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
    void deactivate();

    // Per-session spread state (T011/T013). Views in enumeration order; the
    // scale/translate transformer per view; the on-screen (output-local) rect per
    // view for click hit-testing; the captured pre-session "own alpha" per view.
    std::vector<wayfire_toplevel_view> session_views;
    std::map<wayfire_toplevel_view, std::shared_ptr<wf::scene::view_2d_transformer_t>> thumbnails;
    std::map<wayfire_toplevel_view, rectf> thumb_rects;
    std::map<wayfire_toplevel_view, float> saved_alpha; // views that were dimmed pre-session (T015)

    // Config (T019). duration/background are read as the animation (T025) + dim land.
    wf::option_wrapper_t<int> opt_drag_threshold{"spread-overview/drag_threshold"};
    wf::option_wrapper_t<int> opt_spacing{"spread-overview/spacing"};
    wf::option_wrapper_t<int> opt_cluster_gap{"spread-overview/cluster_gap"};
    wf::option_wrapper_t<bool> opt_show_labels{"spread-overview/show_ws_labels"};
    wf::option_wrapper_t<int> opt_border_size{"spread-overview/border_size"};
    wf::option_wrapper_t<wf::color_t> opt_border_color{"spread-overview/border_color"};

    // Per-cluster workspace labels (T016) + the thumbnail border overlay, both in the
    // output OVERLAY layer, both torn down the same way in clear_spread().
    std::vector<std::shared_ptr<simple_text_node_t>> label_nodes;
    std::shared_ptr<border_node_t> border_node;

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
    void build_spread();
    void clear_spread();
    void reflow(); // stay-open rebuild after a relocate (FR-010)

    // move.cpp — the only view-relocation / workspace-switch contact (Principle II).
    void relocate(wayfire_toplevel_view view, wf::point_t target_ws);
    void switch_workspace(wf::point_t target_ws);

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
