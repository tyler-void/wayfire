# Contract: pure `layout()` + drop hit-test

This is the one module with real design decisions and the only one carrying unit tests
(constitution Principle I). It has **zero** Wayfire rendering/input/scene dependencies — it operates
on plain value types so it can be tested with no compositor.

## Types (plain values — no Wayfire deps)

```cpp
struct ivec2   { int x, y; };
struct rect_t  { double x, y, w, h; };          // output-local coordinates
struct dimf_t  { double w, h; };

struct layout_input_view {
    uint32_t id;            // stable id for mapping results back to the real view
    ivec2    source_ws;     // from get_view_main_workspace()
    dimf_t   natural_size;  // the view's real (unscaled) size
};

struct layout_options {
    double spacing;         // gap between thumbnails within a cluster (px)
    double cluster_gap;     // gap between clusters (px)
    double outer_margin;    // margin around the whole overview (px)
    double max_scale;       // never upscale a thumbnail beyond this factor (hard cap)
    double min_scale;       // SOFT target: below this a cluster is "over-dense" (informational
                            //   only — never overrides zero-overlap; see invariant #10 note)
};

struct cluster_out { ivec2 ws; rect_t region; };
struct view_out    { uint32_t id; rect_t target_rect; ivec2 cluster_ws; };

struct layout_result {
    std::vector<cluster_out> clusters;   // one per grid cell (incl. empty)
    std::vector<view_out>    views;      // one per input view
};
```

## Functions

```cpp
// Pure. Deterministic. No global state, no time, no RNG.
layout_result layout(const std::vector<layout_input_view>& views,
                     ivec2 grid,             // workspace grid (width,height)
                     dimf_t output_size,     // logical output size
                     const layout_options& opts);

// Inverse of the cluster regions produced by layout(). Shares the same geometry.
// Returns the workspace whose cluster region contains `p`, or std::nullopt if `p`
// is in a gap / outside all clusters (→ caller treats as snap-back).
std::optional<ivec2> hit_test_cluster(const layout_result& r, rect_t /*unused*/,
                                      double px, double py);
```

## Invariants (asserted by the doctest suite)

1. **Cluster coverage**: `clusters.size() == grid.x * grid.y`; every cell present even if empty
   (empty workspaces remain valid drop targets — spec Edge Cases).
2. **Grouping**: every `view_out.cluster_ws` equals its input `source_ws`; every `view_out.target_rect`
   lies within that cluster's `region` (inflated by `spacing`).
3. **Zero overlap** (SC-005): no two `view_out.target_rect` intersect — including inputs whose
   `natural_size` equals the output (the "maximized" case).
4. **Bounds**: every `target_rect` and every cluster `region` lies within `output_size` minus
   `outer_margin`.
5. **No upscale past cap**: a thumbnail's effective scale ≤ `opts.max_scale`.
6. **Hit-test round-trip**: for every `view_out`, `hit_test_cluster(r, _, center(target_rect))`
   returns that view's `cluster_ws`. (Guarantees drop resolution agrees with the visuals.)
7. **Gap → nullopt**: a point in `cluster_gap`/`outer_margin` space returns `std::nullopt`.
8. **Determinism**: identical inputs → identical output (stable ordering; no time/RNG).
9. **Fixed-equal region sizing** (R14 — the inter-cluster allocation rule): the working area
   (`output_size` minus `outer_margin`) is divided into a `grid.x × grid.y` matrix of **equal** cells
   separated by `cluster_gap`; the cluster for workspace `(cx,cy)` is exactly cell `(cx,cy)`. Cluster
   `region` size/position is a pure function of `grid` + `output_size` + `opts` and is **independent
   of content density**. This makes on-screen cluster position a bijection with workspace grid
   position (spatial legibility). Assert: two inputs differing only in per-cluster view *counts*
   produce identical `clusters[]` regions.
10. **Uniform within-cluster scale**: within a cluster, all `target_rect`s share one scale factor —
    the largest value ≤ `max_scale` at which all the cluster's views pack (row/column) inside the
    region with `spacing` and zero overlap. Density changes thumbnail *size*, never cluster
    *position*. *`min_scale` note*: if that scale falls below `opts.min_scale` the cluster is flagged
    over-dense (a `view_out`/`cluster_out` may carry an `over_dense` bool for the UI), but the layout
    still fits at the smaller scale — **zero-overlap (invariant 3) always wins over `min_scale`**.

## Representative test cases

- Empty input → `grid.x*grid.y` empty clusters, no views, no overlap.
- One view on a 3×3 grid → placed in its source cluster; hit-test of its center returns its ws.
- Two overlapping/maximized views on the same workspace → both shrink, no overlap (invariant 3).
- N views spread across all 9 cells → each in the right cluster; drop-point in an empty cell resolves
  to that empty cell's ws; drop-point in a gap → nullopt.
- Many views on one workspace → thumbnails shrink to preserve invariant 3 within the cluster region.
- **Density-invariant regions (R14 / inv. 9)**: two inputs identical except one has 8 views in
  cluster A + 1 in cluster B, the other 1 + 8 → the `clusters[]` regions are identical; only
  thumbnail sizes differ. Confirms cluster geometry never depends on content density.
- **Over-dense flag (inv. 10)**: a cluster with enough views that the fit scale < `min_scale` still
  produces zero-overlap rects and reports `over_dense = true`.

## Notes

- The plugin's `render`/`input` wrappers translate real Wayfire views ↔ `layout_input_view`/`view_out`
  by `id`. `layout` never sees a `wayfire_toplevel_view` (Principle I/II).
- The algorithm is **two-level and fully specified** (R14): (1) region sizing = fixed-equal grid cells
  (invariant 9); (2) within each cell, borrow `scale`'s compiz-derived row/column packing
  (`scale.cpp:920+`) at a single uniform scale (invariant 10). There is no discretionary "how much
  space does this cluster get" step left to the implementer.
