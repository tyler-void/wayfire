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

// ---------------------------------------------------------------------------------
// 003: the overview CAMERA — pure, and a sibling of layout() rather than part of it
// (layout()'s input and output are unchanged). Contract:
// specs/003-spread-zoom/contracts/camera.md, invariants #C1-#C7.
//
// It maps an overview-space point to the screen as the view zooms between "the whole
// workspace grid is visible" (t = 0) and "the pivot cell fills the screen" (t = 1):
//
//   s = max(output.w / pivot.w, output.h / pivot.h)   // uniform "cover" scale
//   k = lerp(1, s, t)                                 // one scalar for BOTH axes
//   A = lerp(centre(pivot), centre(output), t)        // the anchor
//   camera_apply(p) = (p - centre(pivot)) * k + A
//
// Anchoring on lerp(centre(pivot), centre(output), t) rather than on centre(output) is
// what makes t = 0 the identity; the latter is a TRANSLATION at t = 0, which would make
// the overview jump the moment an animation armed.
// ---------------------------------------------------------------------------------
struct camera_t
{
    rectf pivot;      // the cell the camera converges on / departs from (output-local)
    dimf  output;     // logical output size; the output rect is (0, 0, output.w, output.h)
    double t = 0.0;   // 0 = whole grid visible, 1 = pivot fills the output. Clamped on use.
};

// The scale the camera applies at cam.t — a single scalar, so aspect ratios are preserved
// (#C3). Exactly 1.0 at t = 0 and for a 1x1 grid (#C1, #C4).
double camera_scale(const camera_t& cam);

// Map an overview-space point to the screen. At t = 0 this returns `p` bit-exactly (#C1).
pointf camera_apply(const camera_t& cam, pointf p);

// Map a whole rect: its centre through camera_apply, its size by camera_scale. This is the
// single definition the plugin uses for BOTH thumbnails (via the view transformer) and the
// overlay nodes' destination rects, so they can never disagree about where the camera is.
rectf camera_rect(const camera_t& cam, const rectf& r);

// The INVERSE of camera_apply / camera_rect: screen space back to overview space. Needed
// because the camera is composed INTO each thumbnail's transformer, so any code that reads a
// rendered rect back and uses it as the base of a new animation (the drag snap-back, the reflow
// snapshot) must divide the camera out first, or the next frame would zoom it twice. Exact at
// t = 0 (the identity) and well-defined everywhere, since camera_scale is never below 1.
// See contracts/camera.md #C8.
pointf camera_unapply(const camera_t& cam, pointf q);
rectf  camera_unrect(const camera_t& cam, const rectf& r);

// Inverse of the cluster regions produced by layout(); shares the same geometry.
// Returns the workspace whose cluster region contains (px,py), or nullopt if the
// point is in a gap / margin / outside all clusters (caller treats as snap-back).
std::optional<ivec2> hit_test_cluster(const layout_result& r, double px, double py);
} // namespace spread
} // namespace wf
