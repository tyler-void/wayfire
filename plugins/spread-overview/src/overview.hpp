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
#include <wayfire/plugins/common/input-grab.hpp>

#include <memory>

#include "layout.hpp"

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
};
} // namespace spread
} // namespace wf
