# Contract: the overview camera (pure)

**Adds to**, and does not modify, the layout contracts of 001 and 002: `layout()`,
`hit_test_cluster()`, their types and invariants 1–13 are untouched (spec FR-012). This file
specifies one new **pure** function living beside them, so constitution Principle I covers it
without amending Principle I's signature contract (the camera is not part of `layout()`'s input
or output).

This file is **NORMATIVE**. If the implementation proves a statement here wrong, stop and report
the evidence.

---

## 1. Type and function

```cpp
// specs: 003-spread-zoom. Pure, deterministic, no Wayfire dependencies.
struct camera_t
{
    rectf  pivot;     // the cell the camera converges on / departs from (output-local)
    dimf   output;    // logical output size
    double t = 0.0;   // 0 = whole grid visible, 1 = pivot fills the output
};

// Map an overview-space point to the screen.
pointf camera_apply(const camera_t& cam, pointf p);

// The scale factor the camera applies at cam.t (both axes; see invariant #C3).
double camera_scale(const camera_t& cam);
```

A rect is mapped by transforming its centre with `camera_apply` and multiplying its size by
`camera_scale` — the plugin does exactly that for thumbnails (via the view transformer) and for
overlay destination rects.

---

## 2. Definition — NORMATIVE

With `C = centre(cam.pivot)`, `Oc = centre(output rect at (0,0,output.w,output.h))`,
`t = clamp(cam.t, 0, 1)`:

```
s    = max(output.w / max(1, pivot.w),  output.h / max(1, pivot.h))   // uniform "cover"
k    = 1 + (s − 1) · t                                               // lerp(1, s, t)
A    = C + (Oc − C) · t                                              // lerp(C, Oc, t)

camera_apply(cam, p) = (p − C) · k + A
camera_scale(cam)    = k
```

`s ≥ 1` always (a cell is never larger than the output), so `k ∈ [1, s]`.

---

## 3. Invariants (asserted by the doctest suite)

- **#C1 — Identity at rest.** `cam.t == 0` ⇒ `camera_apply(cam, p) == p` **exactly**, for every
  `p`, and `camera_scale == 1`. (A formulation anchored on `Oc` instead of `lerp(C, Oc, t)` fails
  this: it is a translation at `t = 0`.)
- **#C2 — Cover at full zoom.** `cam.t == 1` ⇒ `camera_apply(cam, C) == Oc`, and the mapped pivot
  rect covers the output rect: mapped width `≥ output.w` and mapped height `≥ output.h`, within
  1e-9.
- **#C3 — One scale for both axes.** `camera_scale` is a single scalar; mapping a rect multiplies
  width and height by the same factor, so aspect ratio is preserved for all `t`.
- **#C4 — Degenerate grid is a no-op.** If `pivot` equals the output rect (a 1×1 grid), then
  `s == 1`, `k ≡ 1` and `A ≡ C`, so `camera_apply` is the identity for every `t`.
- **#C5 — Monotonic.** For a point outside the pivot, its distance from `A` is non-decreasing in
  `t`; a point at `C` never moves relative to the anchor. (No fold-back or overshoot.)
- **#C6 — Determinism.** Pure: no global state, no time, no RNG; identical inputs give bitwise
  identical output.
- **#C7 — Clamping.** `t` outside `[0,1]` is clamped, never extrapolated.

---

## 3b. Added in I3 — the inverse (#C8)

```cpp
pointf camera_unapply(const camera_t& cam, pointf q);  // inverse of camera_apply
rectf  camera_unrect(const camera_t& cam, const rectf& r);  // inverse of camera_rect
```

```
camera_unapply(cam, q) = (q − A) / k + C
```

- **#C8 — Round trip.** `camera_unapply(cam, camera_apply(cam, p)) == p` and
  `camera_unrect(cam, camera_rect(cam, r)) == r`, within 1e-9, for every `t`. Well-defined
  everywhere because `k ≥ 1`, so there is no division to guard.

**Why it exists.** The camera is composed *into* each thumbnail's `view_2d_transformer_t`. Any
code that reads a **rendered** transform back and reuses it as the **base** of a new animation
would therefore have the camera applied twice on the next frame. There are exactly two such
paths — the drag snap-back (`animate_thumb_to_slot`) and the reflow snapshot
(`snapshot_thumb_screen_rects`) — and both divide the camera out first. `live_thumb_rect`
deliberately does **not**: hit-testing and the drop target want screen space. The rule is
*screen space for input, base space for clocks*.

**Correction to §3 #C1 as applied to rects.** #C1's exactness is a property of `camera_apply` on
**points**. A rect is mapped through its centre, and `(r.x + r.w/2) − r.w/2` is not bit-exact in
floating point for general `r`, so `camera_rect`/`camera_unrect` at `t = 0` are exact in **size**
but only approximate in **position**. This is not a defect and nothing rests on it: the plugin
skips the camera outright at `t == 0`, so no rect is ever mapped there.

---

## 4. Tests to add (same doctest binary `Spread overview layout test`)

1. **#C1** on a grid of sample points, including points inside, on the edge of, and far outside
   the pivot cell — exact equality, not epsilon.
2. **#C2** for every cell of a 3×3 grid on 3440×1440 and of a 4×2 grid on 1920×1080 (the
   non-square case the "cover" choice exists for).
3. **#C3**: a unit square mapped at `t ∈ {0, 0.25, 0.5, 0.75, 1}` keeps its aspect ratio.
4. **#C4** with `pivot == output` (1×1 grid) at the same `t` values.
5. **#C5** monotonicity over 101 `t` steps for a sample of points.
6. **#C7** with `t = −1` and `t = 2`.
7. A randomised property pass (fixed seed) over grids 1×1…4×4, outputs from the fuzz set, and
   every cell as pivot: #C1, #C3, #C5, #C7 and finiteness (no NaN/Inf).

All existing 001/002 tests keep passing unchanged, including golden parity — the camera must not
touch `layout()`.

---

## 5. Overlay rendering contract (I0)

The nodes stop baking output-sized textures; they draw solid rects. Behaviour that MUST be
preserved exactly at rest (`t = 0`, no fade):

- **Veil**: every cell except the active one is covered at alpha
  `background.a · (1 − inactive_brightness)`; the active cell is uncovered. With
  `cluster_gap > 0` the gaps and margins are covered at the same alpha, **each region painted
  exactly once** (no double-darkening where a horizontal and a vertical strip would cross).
- **Grid**: one line per row boundary and per column boundary, including the outer edges, each of
  `border_size` width — so a boundary shared by two cells is drawn **once**, at single thickness
  and single alpha (the cairo version achieved this by coincident strokes).
- **Per-cell alpha** becomes animatable, which is what closes 002's deferred FR-011: the outgoing
  cell ramps to the dim alpha while the incoming one ramps to transparent, over the same ≈120 ms
  constant the highlight already uses.
