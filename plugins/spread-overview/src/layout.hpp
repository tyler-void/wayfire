#pragma once

// PURE layout core for spread-overview — constitution Principle I.
// Zero Wayfire dependencies: operates on plain value types so it is unit-testable
// in isolation. See specs/001-spread-overview/contracts/layout.md.

#include <vector>
#include <cstdint>
#include <optional>

namespace wf
{
namespace spread
{
struct ivec2 { int x = 0, y = 0; };
struct rectf { double x = 0, y = 0, w = 0, h = 0; };
struct dimf  { double w = 0, h = 0; };
struct pointf { double x = 0, y = 0; }; // 002: workspace-local position

struct layout_input_view
{
    uint32_t id = 0;        // stable id, maps results back to the real view
    ivec2    source_ws;     // from get_view_main_workspace()
    dimf     natural_size;  // the view's real (unscaled) size
    // 002: the view's top-left INSIDE ITS OWN WORKSPACE (workspace-local). Drives row
    // membership (by center y) and within-row order (by center x); the id only breaks
    // ties. Braces are required — without them the 3-member aggregate initializers in
    // the existing tests warn under -Wmissing-field-initializers.
    pointf   natural_pos{};
};

struct layout_options
{
    double spacing      = 20.0; // gap between thumbnails within a cluster (px)
    double cluster_gap  = 40.0; // gap between clusters (px)
    double outer_margin = 20.0; // margin around the whole overview (px)
    double max_scale    = 1.0;  // hard cap: never upscale beyond this
    double min_scale    = 0.05; // soft target: below this a cluster is "over-dense"
    // 002: small-window emphasis (beta). The shortest windows are enlarged by up to this
    // factor relative to full-height ones before packing; 1.0 disables the emphasis.
    // Values < 1.0 are treated as 1.0.
    double small_window_boost = 1.5;
};

struct cluster_out { ivec2 ws; rectf region; bool over_dense = false; };
struct view_out    { uint32_t id = 0; rectf target_rect; ivec2 cluster_ws; };

struct layout_result
{
    std::vector<cluster_out> clusters; // one per grid cell (incl. empty)
    std::vector<view_out>    views;    // one per input view
};

// Pure, deterministic. No global state, no time, no RNG.
// Region sizing is fixed-equal (invariant #9); within-cluster scale is uniform (#10).
layout_result layout(const std::vector<layout_input_view>& views,
    ivec2 grid, dimf output_size, const layout_options& opts);

// Inverse of the cluster regions produced by layout(); shares the same geometry.
// Returns the workspace whose cluster region contains (px,py), or nullopt if the
// point is in a gap / margin / outside all clusters (caller treats as snap-back).
std::optional<ivec2> hit_test_cluster(const layout_result& r, double px, double py);
} // namespace spread
} // namespace wf
