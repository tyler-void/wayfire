// inactive-alpha — dim every window except the focused one (feature 004).
//
// Replaces the pywayfire daemon that previously did this over IPC. Constitution
// Principle II: this file is the ONLY Wayfire contact; every decision lives in the pure
// policy (policy.hpp, unit-tested without a compositor).
//
// Three signals drive it, all verified in docs/API-MAP-verified.md §7:
//   keyboard_focus_changed_signal          (core)   — who should stay opaque
//   output_plugin_activated_changed_signal (output) — whether to dim at all
//   view_mapped_signal / view_unmapped_signal (core) — which windows exist
//
// Opacity is written through the "alpha"-named view_2d_transformer_t at TRANSFORMER_2D —
// the SAME transformer the stock `alpha` plugin, its IPC method and spread-overview's
// save/restore use. Last writer wins, by design (Principle V, amended in 1.4.0).

#include "policy.hpp"

#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/util/log.hpp>

#include <algorithm>
#include <set>
#include <string>

namespace wf
{
namespace inactive_alpha
{
static const std::string ALPHA_TRANSFORMER = "alpha";

class inactive_alpha_t : public wf::per_output_plugin_instance_t
{
  public:
    void init() override
    {
        wf::get_core().connect(&on_focus_changed);
        wf::get_core().connect(&on_view_mapped);
        wf::get_core().connect(&on_view_unmapped);
        output->connect(&on_plugin_activation);
        opt_dim_alpha.set_callback(dim_alpha_changed);

        // Adopt the focus that is already current, so enabling the plugin mid-session
        // dims immediately instead of waiting for the next focus change.
        if (auto v = wf::toplevel_cast(wf::get_core().seat->get_active_view()))
        {
            last_focused = v->get_id();
        }

        apply_all();
    }

    void fini() override
    {
        // FR-005: leave nothing dimmed. Restore exactly the views this plugin dimmed —
        // not every view, so a value the user set by hand is not clobbered on unload.
        for (auto id : dimmed)
        {
            if (auto v = view_by_id(id))
            {
                set_alpha(v, 1.0);
            }
        }

        dimmed.clear();
        active_plugins.clear();
    }

  private:
    wf::option_wrapper_t<double> opt_dim_alpha{"inactive-alpha/dim_alpha"};

    // The LAST toplevel to hold keyboard focus. Deliberately retained when focus moves to
    // a non-window (a panel, layer-shell): clearing it would dim every window at once
    // (FR-008; the pure policy stays total either way — see policy.hpp).
    uint32_t last_focused = 0;

    // Views this plugin currently holds below 1.0, so fini() restores exactly those.
    std::set<uint32_t> dimmed;

    // Plugins currently active on this output. Non-empty => suppressed. A SET, not a
    // counter, so a double activation or a missed deactivation cannot wedge it. No plugin
    // NAME is ever compared: the daemon this replaces keyed on a hardcoded name list,
    // which is exactly how a focused window ended up dimmed inside the overview.
    std::set<std::string> active_plugins;

    bool suppressed() const
    {
        return !active_plugins.empty();
    }

    policy_input current_policy() const
    {
        policy_input in;
        in.focused    = last_focused;
        in.suppressed = suppressed();
        in.dim = opt_dim_alpha;
        return in;
    }

    wayfire_toplevel_view view_by_id(uint32_t id) const
    {
        for (auto& v : output->wset()->get_views(wf::WSET_MAPPED_ONLY))
        {
            if (v->get_id() == id)
            {
                return v;
            }
        }

        return nullptr;
    }

    // Mirrors the stock alpha plugin (alpha.cpp:87-108): get-or-create the shared
    // transformer, and REMOVE it again at 1.0 so an opaque window carries no idle node.
    void set_alpha(wayfire_toplevel_view view, double alpha)
    {
        if (!view || !view->is_mapped())
        {
            return; // Principle VI
        }

        auto tmgr = view->get_transformed_node();
        auto tr   = tmgr->get_transformer<wf::scene::view_2d_transformer_t>(ALPHA_TRANSFORMER);

        if (alpha >= 1.0)
        {
            if (tr)
            {
                tr->alpha = 1.0f;
                tmgr->rem_transformer(ALPHA_TRANSFORMER);
            }

            return;
        }

        if (!tr)
        {
            auto node = std::make_shared<wf::scene::view_2d_transformer_t>(view);
            tmgr->add_transformer(node, wf::TRANSFORMER_2D, ALPHA_TRANSFORMER);
            tr = tmgr->get_transformer<wf::scene::view_2d_transformer_t>(ALPHA_TRANSFORMER);
        }

        if (!tr)
        {
            return;
        }

        // SC-007: write (and damage) only when the value actually changes, so an unchanged
        // state costs no repaint.
        if (tr->alpha != (float)alpha)
        {
            tr->alpha = (float)alpha;
            view->damage();
        }
    }

    void apply_all()
    {
        const auto in = current_policy();

        for (auto& v : output->wset()->get_views(wf::WSET_MAPPED_ONLY | wf::WSET_EXCLUDE_MINIMIZED))
        {
            const double target = target_alpha(in, v->get_id());
            set_alpha(v, target);

            if (target >= 1.0)
            {
                dimmed.erase(v->get_id());
            } else
            {
                dimmed.insert(v->get_id());
            }
        }
    }

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_focus_changed =
        [this] (wf::keyboard_focus_changed_signal *ev)
    {
        // new_focus is a scene NODE and may be null (seat.hpp:22). Only a toplevel view
        // updates the focus; anything else (a panel taking focus, or focus going away)
        // leaves last_focused alone — FR-008.
        if (auto v = wf::toplevel_cast(wf::node_to_view(ev->new_focus)))
        {
            if (v->get_id() != last_focused)
            {
                last_focused = v->get_id();
                apply_all();
            }
        }
    };

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped =
        [this] (wf::view_mapped_signal *ev)
    {
        if (wf::toplevel_cast(ev->view))
        {
            apply_all(); // the newcomer may be focused, or may need dimming
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped =
        [this] (wf::view_unmapped_signal *ev)
    {
        if (auto v = wf::toplevel_cast(ev->view))
        {
            // Just forget it: writing opacity to a view that is going away is pointless,
            // and its transformer dies with it.
            dimmed.erase(v->get_id());

            if (v->get_id() == last_focused)
            {
                // Keep the id: the compositor will send the next focus change shortly, and
                // zeroing it here would dim every window for that one frame.
            }
        }
    };

    wf::signal::connection_t<wf::output_plugin_activated_changed_signal> on_plugin_activation =
        [this] (wf::output_plugin_activated_changed_signal *ev)
    {
        const bool was_suppressed = suppressed();

        if (ev->activated)
        {
            active_plugins.insert(ev->plugin_name);
        } else
        {
            active_plugins.erase(ev->plugin_name);
        }

        // Only act on the transitions: empty -> non-empty (everything to full opacity for
        // the overview) and non-empty -> empty (re-dim from the focus current *now*).
        if (was_suppressed != suppressed())
        {
            apply_all();
        }
    };

    wf::config::option_base_t::updated_callback_t dim_alpha_changed = [this] ()
    {
        apply_all(); // FR-002: applies to already-open windows, no restart
    };
};
} // namespace inactive_alpha
} // namespace wf

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wf::inactive_alpha::inactive_alpha_t>);
