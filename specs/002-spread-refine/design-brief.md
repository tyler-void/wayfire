# Design Brief: 002-spread-refine

Input to `/speckit-plan`. Everything below was decided or measured in the planning conversation on
2026-09-29 against fork `master` @ `8b519e0b` (0.12-dev, ABI `2026'08'01`), then checked by an
independent review pass whose findings are folded in. Fold this brief into the plan's Phase 0/1
artifacts: `research.md` (as R-entries), `contracts/layout.md`, `data-model.md`, `quickstart.md`.
Sections marked **NORMATIVE** are not open for reinterpretation; if the code proves one wrong, stop
and report — do not silently adapt.

Tags: **VERIFIED** = read in this checkout or in primary source; **MEASURED** = produced by
`reference/layout_ref.py` (exact command given); **INFERRED** = reasoned, confirm while implementing.

---

## §A Within-cluster layout

### A.1 Root cause in 001 (VERIFIED)

`pack_cluster()` in `plugins/spread-overview/src/layout.cpp`:

1. Splits the cluster into `cols × rows` **equal slots** (`cols = round(sqrt(n · aspect))`).
2. Uses **one scale for the whole cluster**: the largest `s ≤ max_scale` at which *every* member
   fits its slot. The widest/tallest window sets `s` for all.

This is 001 invariant #10 (`specs/001-spread-overview/contracts/layout.md`) and the second half of
decision R14 (`specs/001-spread-overview/research.md`). Reference scene, workspace (0,0): a
maximized 3440×1440 browser in a 355.6-px-wide slot forces `s = 0.103`, so a 620×460 volume mixer
renders at **64×48 px**.

### A.2 Algorithm — NORMATIVE

Per cluster, with members `M` (n ≥ 1), cluster region `R`, `σ = spacing`, `S = max_scale`,
`β = max(1, small_window_boost)`, `H_out = max(1, output_size.h)`. Each member has natural size
`w, h` and natural position `x, y` (top-left inside its own workspace). Wherever a size is used as
a *size* (widths, heights, divisions) it is clamped to ≥ 1 (`W(v) = max(1, v.w)`,
`H(v) = max(1, v.h)`); the emphasis ratio and the centers use the raw values.
`layout_ref.py::layout_002()` is the executable form; the C++ MUST match it (§B invariant 13).

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

Complexity: at most `n` candidate row counts, each `O(n log n)` — trivial at "a few dozen windows"
(001 plan). The golden fixture never triggers D4 (its output is bit-identical with and without it).

### A.3 Source and deviations (VERIFIED)

Port of GNOME Shell `js/ui/workspace.js`, `main` @ `0062bde` (2026-09-27): `UnalignedLayoutStrategy`
(`_computeWindowScale` = `lerp(1.5, 1, height/monitor.height)`, `computeLayout`, `_keepSameRow`,
`computeScaleAndSpace`, `computeWindowSlots`), `LAYOUT_SCALE_WEIGHT = 1`, `LAYOUT_SPACE_WEIGHT = 0.1`,
and the `_createBestLayout` loop. GNOME's constant 1.5 becomes the option `β`; GNOME's
`WINDOW_PREVIEW_MAXIMUM_SCALE` (0.95) becomes the existing `max_scale`.

- **D1 — empty rows are dropped.** GNOME can leave a row empty when one window is ≥ 2× the ideal row
  width; the empty row still consumes row spacing.
- **D2 — a row's height counts only its own windows.** GNOME updates `row.fullHeight` *before*
  deciding the window stays, so the first window of the next row inflates the previous row.
- **D3 — the emphasis ratio is clamped to [0, 1].** GNOME does not clamp, so a window taller than the
  output would get an emphasis below 1.
- **D4 — feasibility guard (found in review).** GNOME's `scale_space` reserves spacing only for the
  widest row; a row with more (narrower) windows can then need more spacing than the area has,
  giving zero or negative thumbnail sizes, and very dense clusters reach `s = 0` (division by zero in
  `place`). Reproductions are built into the reference (`dense_repros()`): a 480×540 region, spacing
  40, one 1920×1080 window + 11 or 12 windows of 60×60 (GNOME rule → 0×0 / negative widths), and 85
  windows of 800×600 (→ `s = 0`). With D4 they come out ≥ 6.7 px on the short side, no overlap.
- **Not ported:** `_adjustSpacingAndPadding` (room for title/close overlays spread-overview does not
  draw) and pixel flooring (001 does not floor).

### A.4 Evidence (MEASURED)

Reference scene = `reference/golden-fixture.json` (18 windows, 3440×1440, 3×3, spacing 20, gap 0,
margin 0 — the plugin's defaults: `render.cpp:34-41`, `metadata/spread-overview.xml:29-40`).
Command: `python3 layout_ref.py --golden golden-fixture.json`.

| Window | 001 | 002 β=1.0 | **002 β=1.5 (default)** | 002 β=2.5 |
|---|---|---|---|---|
| browser 3440×1440, ws(0,0) | 356×149 | 711×298 | **649×272** | 553×231 |
| volume mixer 620×460 | 64×48 | 128×95 | **157×116** | 201×149 |
| calculator 400×560 | 83×116 | 111×156 | **141×197** | 194×272 |
| notification 420×220 | 80×42 | 115×60 | **134×70** | 142×74 |

- Smallest thumbnail area: 001 = 3,047 px² → β=1.5: 9,427 px² (3.1×). No fixture window shrinks vs
  001 at any of the three β values. Single-window workspaces are unchanged; crowded ones gain most.
- Robustness — `python3 layout_ref.py --fuzz 6000` (seed 11): 6,000 random clusters (grids 1×1…4×2,
  outputs 1920×1080 / 2560×1600 / 3440×1440, spacing {0, 8, 20, 40}, 1–100 windows, sides 1 px…full
  output) + the 3 dense repros, × 3 β = 18,009 layouts → **0 overlap / out-of-region / non-positive
  size violations** (tolerance 1e-6 px).

### A.5 Known limitation (MEASURED) — record it in ADR-004

Emphasis is keyed to **height** (GNOME's rows align by height); in a **width-bound** region, boosting
a short-but-wide window can shrink its siblings, and GNOME's row-count objective (layout scale +
space) is not "maximize the smallest thumbnail". So "never worse than 001" is **not** an invariant —
do not assert it as a general property (it *is* asserted on the golden fixture, SC-002).

"002's smallest thumbnail smaller than 001's", per cluster:

| Sample (command) | β=1.0 | β=1.5 | β=2.5 |
|---|---|---|---|
| Target geometry 3440×1440, 3×3, spacing 20; 5,000 clusters of 1–10 windows 200–3440 × 150–1440 (`--fuzz-target 5000`, seed 5) | 0 | 1 (worst 0.96×) | 24 (worst 0.77×) |
| Broad fuzz, only clusters where 001's smallest side ≥ 16 px — 1,523 of 6,003 (`--fuzz 6000`, seed 11) | 1 (0.94×) | 8 (0.71×) | 25 (0.31×) |
| Broad fuzz, any density (both layouts sub-pixel in the extreme cases) | 7 | 18 | 64 |

These are samples from one generator and seed each, not bounds.

### A.6 Alternatives considered

- **Keep 001 (equal slots, uniform scale)** — rejected: it is the defect.
- **Equal-height "justified" rows** — not a separate mode: β ≥ 2.5 approaches it; GNOME's rule is a
  proven default.
- **Per-cluster fallback to 001 when 002's smallest thumbnail is smaller** — rejected for now: two
  arrangement styles side by side on one screen for a case at ~0.02% on the target geometry.
  Revisit if it shows up in daily use (ADR-004 "Revisit if").
- **Fix GNOME's spacing reservation instead of D4** (reserve per-row spacing in `scale_space`) —
  rejected: it changes results in ordinary cases (departs from the proven rule everywhere) to fix a
  degenerate one; D4 only acts when a candidate is infeasible.

---

## §B Contract delta — NORMATIVE for `specs/002-spread-refine/contracts/layout.md`

Principle I (purity, plain value types, unit tests) holds, but its **signature contract text** gains
`natural_pos` — see §E (constitution MINOR amendment).

**Types — append these members at the end of the existing structs; leave every existing member and
its default exactly as it is** (`render.cpp:36` default-constructs `layout_options` and relies on the
`max_scale`/`min_scale` defaults; existing aggregate initializers such as `{id, {ws}, {w, h}}` and
`layout_options{20.0, 40.0, 20.0, 1.0, 0.05}` must keep compiling):

```cpp
struct pointf { double x = 0, y = 0; };          // new plain type, next to ivec2/rectf/dimf

// layout_input_view — appended:
    pointf natural_pos{};        // top-left inside its own workspace (workspace-local)
// layout_options — appended:
    double small_window_boost = 1.5;   // β; values < 1.0 are treated as 1.0
```

(Declare `natural_pos{}` with braces: without them g++ 13 / clang warn
`-Wmissing-field-initializers` on the existing test initializers — verified in review.)

**Invariants:**

- **1–9: unchanged**, including #3 zero-overlap and #9 fixed-equal regions.
- **10 (REPLACED):** *Per-window scale.* Each `target_rect` has scale
  `min(max_scale, L · b(v) · a_row)` — `L` one layout scale per cluster, `b(v)` the emphasis of
  §A.2, `a_row ∈ (0, 1]` a per-row fit factor; aspect ratio preserved; every `target_rect` has
  positive width and height. `over_dense` ⇔ the smallest per-window scale in the cluster
  `< min_scale` (informational; #3 still wins).
- **11 (NEW):** *Spatial order.* Within a cluster, row index is non-decreasing in `(cy, id)` order;
  within a row, `target_rect.x` is strictly increasing in `(cx, id)` order.
- **12 (NEW):** *Emphasis off ⇒ row-uniform.* With `small_window_boost = 1.0`, every window in a row
  that is not capped by `max_scale` has the same scale.
- **13 (NEW):** *Reference parity.* For `reference/golden-fixture.json`, every rect matches the
  reference within 0.5 px at β ∈ {1.0, 1.5, 2.5}.

**Tests to add** (doctest, same binary `Spread overview layout test`; all geometric comparisons in
new tests use an epsilon of 1e-6 px — at spacing 0, adjacent rects touch and strict comparisons
report ~1e-12 px false overlaps; the existing strict `overlaps()` helper is fine for the existing
tests, which use spacing 20):

1. Golden parity (#13): embed the fixture and the 54 expected rects (18 × β ∈ {1.0, 1.5, 2.5}) as
   constants generated from `golden-fixture.json` — the test must not parse JSON or run Python.
2. SC-002 on the fixture: a **test-only** copy of 001's `pack_cluster` as the "001 size" oracle;
   no fixture window shrinks at the three β values.
3. The three dense repros from `layout_ref.py::dense_repros()`: positive sizes, no overlap, inside
   the region (D4).
4. Randomized property test (fixed seed, e.g. `std::mt19937{11}`, ≥ 2,000 clusters × 3 β, spacing
   including 0, 1–100 windows, sides down to 1 px): #2, #3, #4, #5, #10 positivity, #11 — never
   "smallest ≥ 001" (§A.5).
5. #12 with β = 1.0.
6. Keep all existing tests. The existing over-dense test still holds (MEASURED: 60 windows of
   1600×1000 in an 800×600 output → min scale 0.0389 < 0.05 → `over_dense`).

---

## §C Plugin wiring

- **Input** (`render.cpp:44-66`, `build_spread`): fill `natural_pos` workspace-locally:
  `pos = view geometry − (source_ws − current_ws) · output_size`. The fork already uses this basis in
  `move.cpp:84-86` (`cell_x = (target_ws.x - cv.x) * W`). INFERRED that it is right for every view
  class (maximized/fullscreen/straddling); ordering only needs it to be consistent within one
  workspace.
- **Options** (`metadata/spread-overview.xml`, `overview.hpp:122-126`, `render.cpp:34-41`):
  - `small_window_boost` — `double`, default `1.5`, min `1.0`, max `4.0`.
  - `exit_duration` — `animation`, default `225ms` (§D-3). Update `duration`'s `_long` text
    (`spread-overview.xml:16`, currently "Entry/exit and reflow animation length.") to "Entry and
    reflow animation length (the close uses exit_duration)."
- The id is still the enumeration index (`render.cpp:52-62`); ordering is by position now, the id
  only breaks ties.
- Principle IV: verify and record in `docs/API-MAP-verified.md` every API touched that is not there
  yet (e.g. `wf::animation::simple_animation_t` constructed from a second option). Already recorded:
  `get_current_workspace` (`API-MAP-verified.md:100`). Update the layout-input description at
  `API-MAP-verified.md:139` to include `natural_pos`.

---

## §D Motion changes (VERIFIED code references)

- **D-2 No jump when a press/drag happens during the opening or re-flow animation (FR-009).**
  Today: `input.cpp:170-173` calls `finalize_entry_anim()` (`render.cpp:456+`) at drag start, which
  snaps **every** thumbnail to its end state. Change:
  - **On press** (not at threshold): if `press_view` has an entry in `anim_state`, erase it — its
    transformer already holds the current interpolated values because `animate_step`
    (`render.cpp:337-380`) writes them every frame. The other clocks keep running.
  - At drag start `drag_orig_*` = the (frozen) current transform; drag-follow is unchanged, so once
    the drag starts the thumbnail moves by exactly the pointer's displacement since the press.
  - **Use live geometry wherever the code assumes the thumbnail is at its slot**, because it may now
    be mid-flight: `thumb_at()` (`input.cpp:22-34`, hit-tests `thumb_rects` = final slots) and
    `dragged_thumb_center()` (`input.cpp:36-50`, slot center + cursor delta). Both must use the
    rendered rect from the transformer — the same math as `snapshot_thumb_screen_rects()`
    (`render.cpp:486-505`: center = view center + translation, size = view size × scale). Factor it
    into one helper. Otherwise the press can grab the window whose *slot* is under the cursor, and
    the drop target / highlight are computed from the wrong point (breaks 001 FR-007/FR-008).
    When nothing is animating, live rect == slot, so behavior is unchanged.
  - `finalize_entry_anim()` may become unused — delete it rather than leave dead code.
- **D-1 Snap-back animates (FR-008).** Today `input.cpp:271-278` assigns `drag_orig_tx/ty` and
  calls `damage_whole()` — an instant jump. Change: start a `thumb_anim_t` for `press_view` from its
  current transform to its **layout slot** (scale + translation recomputed from `current_layout`
  as `render.cpp:105-112` does), **with `a.alpha.set(1.0, 1.0)`** — `animate_step` writes
  `tr->alpha` every frame (`render.cpp:353`) and a fresh clock would otherwise drive it to 0 (the
  entry path does the same at `render.cpp:152`). Install `anim_hook` if needed (pattern at
  `render.cpp:245-255`). Target the slot, not `drag_orig_*` — after D-2 that can be a mid-flight
  position.
- **D-3 Close duration (FR-010).** Today both the thumbnail exit clocks
  (`anim_state.try_emplace(v, opt_duration)` in `start_exit_anim`, `render.cpp:~412`) and the
  overlay dissolve (`overlay_fade{opt_duration}`, `overview.hpp:161`) use `opt_duration`. Add
  `opt_exit_duration` and use it for both (`overlay_fade` only runs during the exit,
  `overview.hpp:160`). Default `225ms` is a taste default (Assumptions in `spec.md`); `300ms`
  restores 001.
- **D-4 Highlight fade (FR-011, P3, deferrable).** Today the drop-target highlight is baked into the
  single cairo texture of `border_node_t` with all grid borders (`overlay.hpp:60-85`,
  `border_node_t::set_highlight`, `overlay.cpp:25`), and the dim veil's bright cell switches in
  `dim_node_t::set_active`. A fade needs the highlight as its own node (or texture) with an alpha
  ramp (~120 ms, fixed constant). If that grows beyond splitting one element out, defer it and record
  why in `tasks.md`.

---

## §E Housekeeping

- **Constitution MINOR → 1.3.0** (add a Sync Impact entry):
  - Principle I's signature contract (`constitution.md:55-57`) lists the layout input as
    `{view_id, source_ws, natural_size}` → add `natural_pos` and the emphasis option.
  - Principle VIII and Technology Constraints still say `0.11-dev`, ABI `2026'07'09`, wlroots
    `0.20.1`, wf-config `0.11.0`. This tree: `0.12-dev`, ABI `2026'08'01`
    (`src/api/wayfire/plugin.hpp:110`), wlroots submodule at tag `0.20.2`, wf-config required
    `>=0.12.0,<0.13.0` (`meson.build:111`). Verify each in the tree before writing it. The boot
    evidence line ("headless smoke test on 2026-07-11, commit `8603d187`") is historical — keep it,
    and add the current evidence (the 2026-09-29 upstream sync: built, 35/35 tests, verified on tty2).
  - Without this, the plan's Constitution Check flags Principles I and VIII.
- **ADR-004** (Principle VII): draft ships in this pack at `docs/adr/004-per-window-cluster-scale.md`;
  update its affected-artifacts list to what was actually built.
- **FEATURES.md**: add the refinement under spread-overview.
- **001 artifacts are append-only**: do not rewrite 001's spec or contract; 002's contract supersedes
  invariant #10 and ADR-004 records it.
- Spec wording: `min_scale` is a layout constant, not a user option.

---

## §F Increments and verification (Principle III)

| # | Increment | Gate before the next one |
|---|---|---|
| I1 | Pure core (§A, §B) + tests. No plugin change. | `meson test -C build "Spread overview layout test"` green; golden parity passes |
| I2 | Wiring: `natural_pos`, `small_window_boost` option | Build + install; **tty2 test session**: a reference-like scene looks like `layout-compare.png` panel 2; 001 quickstart spot-check |
| I3 | D-2 + D-1 (coupled: snap-back targets the slot because D-2 allows mid-flight grabs) | tty2: grab during opening animation → no jump, drop target correct; cancelled drop glides back |
| I4 | D-3 `exit_duration` | tty2: close is shorter; `exit_duration = 300ms` restores 001 |
| I5 | D-4 highlight fade (or recorded deferral) | tty2 |
| I6 | Full 001 quickstart re-run (SC-007) + housekeeping (§E) | user sign-off → only then commit/push |

**Test session protocol** (the one the user already uses): the user logs in on **tty2** and runs
`/usr/local/bin/wayfire -d | tee /tmp/master-log`; the daily desktop is **tty1**. To end the test
when asked: find the tty2 session with `loginctl list-sessions`, then `loginctl terminate-session
<id>`, and confirm tty1's `wayfire` PID is unchanged. **Never kill by process name** — both sessions
run `wayfire`. After each increment, stop at a clean build plus a concrete tty2 test plan and wait
for the user's result before continuing.
