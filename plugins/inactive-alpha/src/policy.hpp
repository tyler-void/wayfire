#pragma once

// PURE decision core for inactive-alpha — constitution Principle I.
// Zero Wayfire dependencies: plain value types only, so it is unit-testable with no
// compositor. See specs/004-inactive-alpha/contracts/policy.md (NORMATIVE).
//
// The logic is three lines; the reason it is a separate, tested unit is that the edge
// cases live here, not in the signal wiring: suppression must beat everything (the bug
// that motivated this feature), and the function must stay total when nothing holds
// keyboard focus.

#include <cstdint>

namespace wf
{
namespace inactive_alpha
{
struct policy_input
{
    // The window that must stay opaque. 0 means "no window is known to hold focus".
    // The CALLER is responsible for keeping the LAST focused window here rather than
    // clearing it when keyboard focus moves to a non-window (a panel, layer-shell):
    // that is what keeps a panel click from dimming everything (FR-008, research R-303).
    uint32_t focused = 0;

    // True while any plugin is active on the output. Deliberately not "which plugin":
    // the daemon this replaces keyed on a hardcoded name list, which is exactly how a
    // focused window ended up dimmed inside the overview (FR-003, research R-301).
    bool suppressed = false;

    // Configured opacity for non-focused windows; clamped to [0, 1] on use.
    double dim = 0.85;
};

// The opacity this window should be rendered at. Pure, total, deterministic.
double target_alpha(const policy_input& in, uint32_t view_id);
} // namespace inactive_alpha
} // namespace wf
