#include "policy.hpp"

#include <algorithm>

namespace wf
{
namespace inactive_alpha
{
double target_alpha(const policy_input& in, uint32_t view_id)
{
    // P2: suppression wins over everything, whatever `dim` says. While a plugin owns the
    // output (an overview, scale, expo) every window must read at full opacity.
    if (in.suppressed)
    {
        return 1.0;
    }

    // P1: the focused window is never dimmed.
    if (view_id == in.focused)
    {
        return 1.0;
    }

    // P3: everyone else gets exactly the configured value, clamped.
    return std::min(1.0, std::max(0.0, in.dim));
}
} // namespace inactive_alpha
} // namespace wf
