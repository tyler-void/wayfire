# Contract: pure `layout()` — 002 delta

**Supersedes** 001 invariant **#10** only (`specs/001-spread-overview/contracts/layout.md`).
Invariants 1–9 of that contract, its types, its function signatures and `hit_test_cluster` are
**unchanged and still binding**; 001's file is append-only and is not edited. Recorded as
`docs/adr/004-per-window-cluster-scale.md`.

This file is **NORMATIVE** (it restates `design-brief.md` §A.2 and §B). If the implementation proves
a statement here wrong, stop and report the evidence — do not adapt it silently.

Constitution Principle I still holds in full: the module is pure, takes plain value types, has zero
Wayfire dependencies, owns the inverse hit-test, and carries the unit tests. Its **signature
contract text** gains `natural_pos` (constitution 1.3.0, MINOR).

---

## 1. Type delta

Members are **appended** to the existing structs. Every existing member keeps its position, name and
default, so `render.cpp`'s default-constructed `layout_options` and existing aggregate initializers
(`{id, {ws}, {w, h}}`, `layout_options{20.0, 40.0, 20.0, 1.0, 0.05}`) keep compiling.

```cpp
struct pointf { double x = 0, y = 0; };          // NEW plain type, beside ivec2/rectf/dimf

struct layout_input_view {
    uint32_t id = 0;
    ivec2    source_ws;
    dimf     natural_size;
    pointf   natural_pos{};      // NEW: top-left inside its OWN workspace (workspace-local).
                                 // Braces are required: without them g++13/clang warn
                                 // -Wmissing-field-initializers on existing test initializers.
};

struct layout_options {
    double spacing      = 20.0;
    double cluster_gap  = 40.0;
    double outer_margin = 20.0;
    double max_scale    = 1.0;
    double min_scale    = 0.05;
    double small_window_boost = 1.5;   // NEW: β. Values < 1.0 are treated as 1.0.
};
```

`layout_result`, `cluster_out`, `view_out` and the `layout()` / `hit_test_cluster()` signatures are
unchanged.

---

## 2. Algorithm — NORMATIVE

Per cluster, with members `M` (n ≥ 1), cluster region `R`, `σ = spacing`, `S = max_scale`,
`β = max(1, small_window_boost)`, `H_out = max(1, output_size.h)`. Each member has natural size
`w, h` and natural position `x, y`. Wherever a size is used **as a size** (widths, heights,
divisions) it is clamped to ≥ 1 — `W(v) = max(1, v.w)`, `H(v) = max(1, v.h)`; the emphasis ratio and
the centres use the **raw** values. `reference/layout_ref.py::layout_002()` is the executable form;
the C++ MUST match it (invariant #13).

```
layout(M, σ):
  result = layout_with_spacing(M, σ)
  if result == none: result = layout_with_spacing(M, 0)          // D4 fallback, always feasible
  return result

layout_with_spacing(M, σ):
  A      = (R.x+σ, R.y+σ, max(R.w−2σ, 1), max(R.h−2σ, 1))        // packing area
  b(v)   = β + (1−β)·clamp(v.h / H_out, 0, 1)                    // GNOME _computeWindowScale (D3)
  cx(v)  = v.x + v.w/2 ;  cy(v) = v.y + v.h/2

  compute_layout(k):                                             // GNOME computeLayout
    ideal = Σ W(v)·b(v) / k
    L     = M sorted by (cy, id) ascending
    rows  = k empty rows {fw=0, fh=0, wins=[]}; idx = 0
    for i in 0..k−1:
      while idx < n:
        v = L[idx]; w = W(v)·b(v); h = H(v)·b(v)
        keep = (row.fw + w ≤ ideal) or |1 − (row.fw+w)/ideal| < |1 − row.fw/ideal|
        if keep or i == k−1: append v; row.fw += w; row.fh = max(row.fh, h); idx++   // D2
        else: break
    drop empty rows                                                                  // D1
    sort each row's wins by (cx, id) ascending
    maxrow = first row with the largest fw (strict >: first wins ties)
    gw = maxrow.fw ; maxcols = |maxrow.wins| ; gh = Σ row.fh

  scale_space(Lyt):                                              // GNOME computeScaleAndSpace
    hs = (maxcols−1)·σ ; vs = (|rows|−1)·σ
    s  = min((A.w−hs)/gw, (A.h−vs)/gh, S)
    space = ((gw·s+hs)·(gh·s+vs)) / (A.w·A.h)

  feasible(Lyt, s):                                              // D4
    s > 0  and  (|rows|−1)·σ < A.h  and  for every row: (|row.wins|−1)·σ < A.w

  better(s0,sp0,s,sp):                                           // GNOME _isBetterScaleAndSpace
    scale_pow = (s−s0)·1.0 ; space_pow = (sp−sp0)·0.1
    s>s0 ∧ sp>sp0 → true ; s>s0 ∧ sp≤sp0 → scale_pow>space_pow
    s≤s0 ∧ sp>sp0 → space_pow>scale_pow ; else → false

  choose k:                                                      // GNOME _createBestLayout + D4
    last_cols = −1; best = none
    for k = 1 .. n:
      cols = ceil(n/k); if cols == last_cols: break
      Lyt = compute_layout(k); (s, sp) = scale_space(Lyt)
      if not feasible(Lyt, s): continue                          // skip; last_cols untouched
      if best ≠ none and not better(s_best, sp_best, s, sp): break
      best = Lyt; last_cols = cols; (s_best, sp_best) = (s, sp)
    if best == none: return none

  place(best, s = s_best):                                       // GNOME computeWindowSlots
    for row: row.w = row.fw·s + (|wins|−1)·σ ; row.h = row.fh·s
    hwo = Σ row.h ; vsp = (|rows|−1)·σ ; add_v = min(1, (A.h−vsp)/hwo)
    comp = 0 ; y = 0
    for row (top to bottom):
      hsp = (|wins|−1)·σ ; wwo = row.w − hsp ; add_h = min(1, (A.w−hsp)/wwo)
      if add_h < add_v: row.add = add_h ; comp += (add_v−add_h)·row.h
      else:             row.add = add_v
      row.x = A.x + max(A.w − (wwo·row.add + hsp), 0)/2
      row.y = A.y + max(A.h − (hwo + vsp), 0)/2 + y
      y += row.h·row.add + σ
    comp /= 2
    for row: ry = row.y + comp ; rh = row.h·row.add ; x = row.x
      for v in row.wins (left to right):
        s_v = s·b(v)·row.add ; cw = W(v)·s_v ; ch = H(v)·s_v
        s_c = min(s_v, S)    ; tw = W(v)·s_c ; th = H(v)·s_c
        tx  = x + (cw−tw)/2
        ty  = (|rows| == 1) ? ry + (rh−th)/2 : ry + rh − ch
        target_rect(v) = (tx, ty, tw, th) ; x += cw + σ
```

Complexity: at most `n` candidate row counts, each `O(n log n)` — trivial at "a few dozen windows".
The golden fixture never triggers D4 (its output is bit-identical with and without it).

Deviations from GNOME Shell `js/ui/workspace.js` (`main` @ `0062bde`, 2026-09-27) — D1 empty rows
dropped, D2 a row's height counts only its own windows, D3 emphasis clamped to [0, 1], D4
feasibility guard — are motivated in `research.md` R-102. `_adjustSpacingAndPadding` and pixel
flooring are deliberately not ported.

---

## 3. Invariants

**1–9: unchanged** from 001 — including #3 zero-overlap and #9 fixed-equal, density-independent
cluster regions.

- **#10 (REPLACED) — Per-window scale.** Each `target_rect` has scale
  `min(max_scale, L · b(v) · a_row)`: `L` one layout scale per cluster, `b(v)` the emphasis above,
  `a_row ∈ (0, 1]` a per-row fit factor. Aspect ratio is preserved and every `target_rect` has
  **positive** width and height. `over_dense` ⇔ the smallest per-window scale in the cluster
  `< min_scale` (informational only; #3 still wins).
- **#11 (NEW) — Spatial order.** Within a cluster, row index is non-decreasing in `(cy, id)` order;
  within a row, `target_rect.x` is strictly increasing in `(cx, id)` order.
- **#12 (NEW) — Emphasis off ⇒ row-uniform.** With `small_window_boost = 1.0`, every window in a row
  that is not capped by `max_scale` has the same scale.
- **#13 (NEW) — Reference parity.** For `reference/golden-fixture.json`, every rect matches
  `reference/layout_ref.py` within **0.5 px** at β ∈ {1.0, 1.5, 2.5}.

**Not an invariant** (`research.md` R-105): "no thumbnail smaller than 001". It holds on the golden
fixture (SC-002) and is asserted only there.

---

## 4. Tests (doctest, same binary `Spread overview layout test`)

1. **Golden parity (#13)** — fixture + 54 expected rects embedded as **generated constants**; the
   test must not parse JSON or run Python.
2. **SC-002 on the fixture** — a **test-only** copy of 001's `pack_cluster` as the "001 size" oracle;
   no fixture window shrinks at β ∈ {1.0, 1.5, 2.5}.
3. **D4 dense repros** — the three cases of `layout_ref.py::dense_repros()`: positive sizes, no
   overlap, inside the region.
4. **Randomized property test** — `std::mt19937{11}`, ≥ 2,000 clusters × 3 β, spacing including 0,
   1–100 windows, sides down to 1 px: #2, #3, #4, #5, #10 positivity, #11. Never asserts
   "smallest ≥ 001".
5. **#12** with β = 1.0.
6. **All existing tests unchanged and still passing**, including the over-dense test (60 windows of
   1600×1000 in an 800×600 output → min scale 0.0389 < 0.05 → `over_dense`).

**Epsilon**: every geometric comparison in a *new* test uses 1e-6 px — at spacing 0 adjacent rects
touch and strict comparisons report ~1e-12 px false overlaps. The existing strict `overlaps()` helper
stays as-is for the existing (spacing-20) tests.
