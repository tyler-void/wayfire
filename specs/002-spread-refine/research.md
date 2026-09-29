# Phase 0 Research: 002-spread-refine

Source of record: `design-brief.md` (decided/measured 2026-09-29 against fork `master` @ `8b519e0b`,
0.12-dev, ABI `2026'08'01`, then reviewed). This file restates those findings as numbered R-entries
for the plan. **Sections of the brief marked NORMATIVE are not reinterpreted here** — R-101 points at
them and `contracts/layout.md` carries them verbatim in meaning.

Tags: **VERIFIED** = read in this checkout or primary source · **MEASURED** = produced by
`reference/layout_ref.py` (command given) · **INFERRED** = reasoned, confirm while implementing.

Numbering continues 001's research (R1–R16) from R-101 so the two sets never collide.

---

## R-101 — Within-cluster packing: port GNOME Shell's `UnalignedLayoutStrategy` (VERIFIED)

**Decision**: replace 001's "equal slots + one uniform scale per cluster" with a port of GNOME Shell
`js/ui/workspace.js` (`main` @ `0062bde`, 2026-09-27): `_computeWindowScale`, `computeLayout`,
`_keepSameRow`, `computeScaleAndSpace`, `computeWindowSlots`, the `_createBestLayout` search, with
`LAYOUT_SCALE_WEIGHT = 1` and `LAYOUT_SPACE_WEIGHT = 0.1`. The **normative pseudocode is
`design-brief.md §A.2`**; the executable oracle is `reference/layout_ref.py::layout_002()`.

**Rationale**: 001's `pack_cluster()` sizes every window in a cluster by the largest member
(001 invariant #10, R14's second half). On the target geometry a maximized 3440×1440 browser in a
355.6-px slot forces `s = 0.103`, rendering a 620×460 volume mixer at 64×48 px. GNOME's strategy
sizes each window individually, packs rows of variable width, and picks the row count by a proven
objective — a shipped, widely-used rule rather than a bespoke one.

**Alternatives considered** (brief §A.6):
- *Keep 001* — rejected: it is the defect.
- *Equal-height justified rows* — not a separate mode; `β ≥ 2.5` approaches it.
- *Per-cluster fallback to 001 when 001's smallest thumbnail would be larger* — rejected for now:
  two arrangement styles side by side on one screen, for a case at ~0.02% on the target geometry.
  Recorded in ADR-004 "Revisit if".
- *Fix GNOME's spacing reservation instead of D4* — rejected: it changes ordinary results to fix a
  degenerate case; D4 only fires when a candidate row count is infeasible.

**ADR**: `docs/adr/004-per-window-cluster-scale.md` (Principle VII) — supersedes 001 invariant #10,
keeps R14's first half (fixed-equal regions, invariant #9).

---

## R-102 — Four deliberate deviations from GNOME (VERIFIED, D4 found in review)

| # | Deviation | Why |
|---|---|---|
| D1 | Empty rows are dropped | GNOME can leave a row empty when one window is ≥ 2× the ideal row width; the empty row still consumes row spacing |
| D2 | A row's height counts only its own windows | GNOME updates `row.fullHeight` *before* deciding the window stays, so the next row's first window inflates the previous row |
| D3 | The emphasis ratio is clamped to [0, 1] | GNOME does not clamp; a window taller than the output would get an emphasis below 1 |
| D4 | Row counts whose spacing alone cannot fit are skipped; zero-spacing retry as a last resort | GNOME reserves spacing only for the widest row, so a row with more (narrower) windows can need more spacing than the area has → zero/negative sizes, and very dense clusters reach `s = 0` (division by zero in `place`) |

**Not ported**: `_adjustSpacingAndPadding` (room for title/close overlays this plugin does not draw)
and pixel flooring (001 does not floor).

**D4 evidence (MEASURED)** — `layout_ref.py::dense_repros()`, all in a 480×540 region at spacing 40:
1920×1080 + 11×(60×60) and + 12×(60×60) (GNOME rule → 0×0 or negative widths), and 85×(800×600)
(→ `s = 0`). With D4 all three come out ≥ 6.7 px on the short side with no overlap.

---

## R-103 — Small-window emphasis is an option, not a code path (VERIFIED)

**Decision**: `small_window_boost` (β), `double`, default **1.5** (GNOME's constant), min 1.0,
max 4.0; values < 1.0 are treated as 1.0. Per window
`b(v) = β + (1−β)·clamp(v.h / max(1, output_h), 0, 1)` — GNOME's
`lerp(1.5, 1, height/monitor.height)` with the clamp of D3. `β = 1.0` disables emphasis.

**Rationale**: the planning comparison (`reference/layout-compare.png`) put 001, β=1.5 and β=2.5 side
by side on the user's real geometry; the stronger variant was worth keeping but not as a second
algorithm. GNOME's `WINDOW_PREVIEW_MAXIMUM_SCALE` (0.95) maps onto the existing `max_scale`, so no
second cap is introduced.

**Alternatives**: hard-coding 1.5 (loses the comparison the user wanted); a discrete
"normal/strong" enum (same effect, coarser, more code).

---

## R-104 — Measured effect on the reference scene (MEASURED)

`python3 reference/layout_ref.py --golden golden-fixture.json` — 18 windows, 3440×1440, 3×3,
spacing 20, gap 0, margin 0 (the plugin's defaults):

| Window | 001 | β=1.0 | **β=1.5 (default)** | β=2.5 |
|---|---|---|---|---|
| browser 3440×1440, ws(0,0) | 356×149 | 711×298 | **649×272** | 553×231 |
| volume mixer 620×460 | 64×48 | 128×95 | **157×116** | 201×149 |
| calculator 400×560 | 83×116 | 111×156 | **141×197** | 194×272 |
| notification 420×220 | 80×42 | 115×60 | **134×70** | 142×74 |

Smallest thumbnail area 3,047 px² → 9,427 px² (**3.1×**). No fixture window shrinks at any of the
three β. Single-window workspaces are unchanged. → SC-001, SC-002.

**Robustness**: `--fuzz 6000` (seed 11) = 6,000 random clusters (grids 1×1…4×2; outputs 1920×1080 /
2560×1600 / 3440×1440; spacing {0, 8, 20, 40}; 1–100 windows; sides 1 px…full output) + the 3 dense
repros, × 3 β = **18,009 layouts, 0 overlap / out-of-region / non-positive-size violations**
(tolerance 1e-6 px). Re-verified in this checkout on 2026-09-29 before planning; the golden fixture
regenerated byte-identical. → SC-003.

---

## R-105 — "Never worse than 001" is NOT an invariant (MEASURED)

Emphasis is keyed to **height** (GNOME's rows align by height), so in a **width-bound** region
boosting a short-but-wide window can shrink its siblings; GNOME's objective is "layout scale +
filled space", not "maximize the smallest thumbnail".

| Sample | β=1.0 | β=1.5 | β=2.5 |
|---|---|---|---|
| Target geometry 3440×1440, 3×3, spacing 20; 5,000 clusters (`--fuzz-target 5000`, seed 5) | 0 | 1 (worst 0.96×) | 24 (worst 0.77×) |
| Broad fuzz, clusters where 001's smallest side ≥ 16 px — 1,523 of 6,003 (`--fuzz 6000`, seed 11) | 1 (0.94×) | 8 (0.71×) | 25 (0.31×) |
| Broad fuzz, any density | 7 | 18 | 64 |

Samples from one generator and seed each — **not bounds**. Consequence for the contract and tests:
SC-002 is asserted **on the golden fixture only**; the randomized property test MUST NOT assert
"smallest ≥ 001". Recorded in ADR-004 Costs and spec Edge Cases.

---

## R-106 — Layout input gains `natural_pos` (VERIFIED types, INFERRED basis)

**Decision**: `layout_input_view` gains `pointf natural_pos{}` — the window's top-left **inside its
own workspace**. `render.cpp`'s `build_spread()` fills it as
`pos = view geometry − (source_ws − current_ws) · output_size`.

**Rationale**: row membership is ordered by window center `cy` and within-row order by `cx`
(FR-003); without a position the arrangement cannot be spatial. The workspace-local basis is the one
the fork already uses for relocation (`move.cpp`, `cell_x = (target_ws.x − cv.x) * W`).

**INFERRED**: that this basis is right for every view class (maximized / fullscreen / straddling).
Ordering only needs consistency *within one workspace*, so a straddling window landing a few pixels
either way changes nothing observable. Confirm in I2 on the test session.

**Constitution impact**: Principle I's signature contract text lists the layout input as
`{view_id, source_ws, natural_size}` → MINOR amendment (R-113).

---

## R-107 — No-jump press during an animation (VERIFIED code path)

**Today**: drag start calls `finalize_entry_anim()`, which snaps **every** thumbnail to its end
state — a visible collective jump (FR-009's defect).

**Decision**:
1. **On press** (not at the drag threshold): if `press_view` has an entry in `anim_state`, erase it.
   Its transformer already holds the current interpolated values because `animate_step` writes them
   every frame, so erasing freezes it exactly where it is drawn. Other clocks keep running.
2. At drag start, `drag_orig_*` = that frozen transform, so drag-follow moves the thumbnail by
   exactly the pointer displacement since the press (SC-005).
3. **Live geometry everywhere the code assumes a thumbnail sits at its slot**: `thumb_at()`
   (hit-test) and `dragged_thumb_center()` (drop resolution + highlight) must use the *rendered*
   rect — centre = view centre + translation, size = view size × scale — the same math as
   `snapshot_thumb_screen_rects()`. Factor into one helper. When nothing animates, live rect == slot,
   so behaviour is unchanged.
4. `finalize_entry_anim()` becomes unused → **delete it** rather than leave dead code.
5. **Every release unfreezes** (added in the planning review). Freezing on press creates a state the
   old code never had: a thumbnail stopped away from its slot with no clock. Some releases are
   neither a click, a drag nor a relocate — a sub-threshold release **outside** the pressed
   thumbnail's rect, or **over a different** thumbnail (`press_view != rel_view`). Those paths
   (`input.cpp:100-122`) currently do nothing, so the thumbnail would stay frozen mid-flight until
   the next reflow or close. **Any release that neither relocates nor closes the overview MUST
   return a thumbnail frozen by that press to its layout slot**, with the same animation as the
   snap-back (R-108). One shared helper serves both paths so they cannot drift apart.
6. **Hit-test in stacking order, top-most first** (added in the planning review). Mid-animation the
   live rects **overlap** — the entry starts each thumbnail at its real desktop position, where a
   maximized window covers its neighbours. `thumb_at()` iterates `thumb_rects`, a `std::map` keyed
   by view **pointer**, so "first hit" is an arbitrary map order, not what is drawn on top. That
   contradicts FR-009's "the thumbnail visibly under the pointer". Candidates MUST be tested
   top-most first.

**Rationale**: without (3) the press can grab the window whose *slot* is under the cursor rather than
the thumbnail the user sees, and the drop target/highlight are computed from the wrong point —
breaking 001 FR-007/FR-008 exactly when the user is most likely to notice. (5) and (6) are the two
states that only become reachable *because* of (1) and (3).

**Alternative rejected**: keep `finalize_entry_anim()` but only for the pressed view — same as (1)
with more code, and it still teleports the pressed thumbnail to its slot.

---

## R-115 — Stacking order for the hit-test (VERIFIED in this tree, Principle IV)

**Decision**: capture the session's view order **once per `build_spread()` / `reflow()`** via
`output->wset()->get_views(WSET_SORT_STACKING | …)` and hit-test that order, **taking the first hit
as the top-most**.

**Verification (master source, this checkout):**

| Fact | Where | Result |
|---|---|---|
| `WSET_SORT_STACKING` = "same order as the scenegraph nodes… may be slow, should not be used on hot paths" | `src/api/wayfire/workspace-set.hpp:40-43` | as stated |
| the sort itself: `std::sort` ascending on `find_index_in_parent(...)` under the LCA | `src/output/workspace-impl.cpp:450-465` | ascending child index |
| which end is top: `raise_to_front` erases and re-inserts at `children.begin()`; `add_front` also inserts at `begin()` | `src/api/wayfire/scene-operations.hpp:35-41`, `:63-79` | **index 0 = front = top-most** |

⇒ `get_views(WSET_SORT_STACKING)` returns **top-most first**; iterate in order and take the first
hit. (Assuming the opposite would have silently picked the *bottom* window of every overlap — the
reason the brief said to verify rather than rely on it.)

**Why cache it**: the flag is documented as slow, and pointer motion is a hot path. Stacking cannot
change while the overview is open — raising is descoped from the session — so one capture per
layout build is both correct and cheap. Recorded in `docs/API-MAP-verified.md` (task T026).

**When nothing overlaps** (the steady state, after the entry animation settles) the result is
identical to today's arbitrary order, so this is a no-op except in exactly the case it fixes.

---

## R-108 — Cancelled drop animates back (VERIFIED code path)

**Today**: snap-back assigns `drag_orig_tx/ty` and damages — an instant jump.

**Decision**: start a `thumb_anim_t` for `press_view` from its current transform to its **layout
slot** (scale + translation recomputed from `current_layout` the way the entry path does), install
`anim_hook` if not running, and **set `a.alpha.set(1.0, 1.0)`**.

**Rationale for the alpha detail**: `animate_step` writes `tr->alpha` every frame; a freshly
constructed clock would otherwise drive alpha to 0 and fade the thumbnail out during the snap-back.
The entry path already does the same. Target the **slot**, not `drag_orig_*`: after R-107 the grab
position can be mid-flight (spec US2 scenario 3).

---

## R-109 — Separate close duration (VERIFIED code path)

**Decision**: add `exit_duration` (`animation`, default **225 ms**) and use it for **both** the
thumbnail exit clocks (`start_exit_anim`) and the overlay dissolve (`overlay_fade`), which only runs
during an exit. `duration`'s metadata `_long` text is updated to say the close uses `exit_duration`.

**Rationale**: 225 ms = 0.75 × the 300 ms open — a taste default proposed in planning (spec
Assumptions), reversible by setting it to 300 ms, which restores 001 exactly.

---

## R-110 — Highlight fade (VERIFIED code path, deferrable)

**Today**: the drop-target highlight is baked into `border_node_t`'s single cairo texture together
with all grid borders; the dim veil's bright cell switches instantly in `dim_node_t::set_active`.

**Decision**: give the highlight its own node (or its own texture) with an alpha ramp of a fixed
**≈120 ms** (a constant, not an option — configuration surface stays small, constitution
"Upstreamability"). **FR-011 is SHOULD**: if this grows beyond splitting one element out, defer it
and record the reason in `tasks.md` (spec allows it; I5 is the deferrable increment).

---

## R-111 — Test strategy for the pure core (VERIFIED constraints)

Same doctest binary (`Spread overview layout test`). New tests:

1. **Golden parity (#13)**: the fixture and its 54 expected rects (18 windows × 3 β) embedded as
   **generated constants** — the test must never parse JSON or invoke Python (offline, hermetic,
   no new build dependency).
2. **SC-002 on the fixture**: a **test-only** copy of 001's `pack_cluster` as the "001 size" oracle.
3. **The three dense repros** (D4): positive sizes, no overlap, inside the region.
4. **Randomized property test**: fixed seed (`std::mt19937{11}`), ≥ 2,000 clusters × 3 β, spacing
   including 0, 1–100 windows, sides down to 1 px → invariants #2, #3, #4, #5, #10-positivity, #11.
   **Never** asserts "smallest ≥ 001" (R-105).
5. **#12** with β = 1.0 (row-uniform scale when emphasis is off).
6. All existing tests keep passing; the existing over-dense test still holds (MEASURED: 60 windows
   of 1600×1000 in an 800×600 output → min scale 0.0389 < 0.05 → `over_dense`).

**Epsilon**: all *new* geometric comparisons use 1e-6 px. At spacing 0 adjacent rects touch and a
strict comparison reports ~1e-12 px false overlaps. The existing strict `overlaps()` helper stays for
the existing tests (they use spacing 20).

---

## R-112 — Types are appended, never reordered (VERIFIED)

`pointf` is a new plain type beside `ivec2`/`rectf`/`dimf`. `natural_pos` is appended to
`layout_input_view`; `small_window_boost` is appended to `layout_options` — **every existing member
and default keeps its position and value**, because `render.cpp` default-constructs `layout_options`
and existing aggregate initializers (`{id, {ws}, {w, h}}`, `layout_options{20.0, 40.0, 20.0, 1.0,
0.05}`) must keep compiling. `natural_pos{}` is declared **with braces**: without them g++ 13/clang
warn `-Wmissing-field-initializers` on the existing test initializers (found in review).

---

## R-113 — Constitution MINOR amendment 1.2.0 → 1.3.0 (VERIFIED against this tree)

Two edits, both required or the plan's own Constitution Check fails:

1. **Principle I signature contract** — add `natural_pos` (and the emphasis option) to the listed
   layout input.
2. **Principle VIII + Technology Constraints facts** — they still say `0.11-dev`, ABI `2026'07'09`,
   wlroots `0.20.1`, wf-config `0.11.0`. This tree: **0.12-dev**, ABI **`2026'08'01`**, wlroots
   submodule at tag **0.20.2**, wf-config required **`>=0.12.0,<0.13.0`**. Each is re-verified in the
   tree before it is written (plan Constitution Check). The historical boot evidence line
   (2026-07-11, `8603d187`) is **kept**; the 2026-09-29 upstream sync is **added** as current
   evidence (built, 35/35 tests, verified on tty2).

A Sync Impact entry is added at the top of the constitution, per its own Governance section.

---

## R-114 — Increment order and gates (Principle III)

| # | Increment | Gate |
|---|---|---|
| I1 | Constitution **1.3.0** amendment (see below) + pure core + tests; **no plugin change** | layout test green; golden parity passes; full suite |
| I2 | Wiring: `natural_pos`, `small_window_boost` | build + install; tty2: a reference-like scene matches `layout-compare.png` panel 2; 001 spot-check |
| I3 | R-107 + R-108 (coupled: snap-back targets the slot *because* mid-flight grabs exist) | tty2: grab during the opening animation → no jump, drop target correct; cancelled drop glides back |
| I4 | R-109 `exit_duration` | tty2: close is shorter; `exit_duration = 300ms` restores 001 |
| I5 | R-110 highlight fade **or a recorded deferral** | tty2 |
| I6 | Full 001 quickstart re-run (SC-007) + remaining housekeeping (ADR-004 affected-artifacts, FEATURES.md, plugin README) | user sign-off |

**Deviation from brief §F**: the brief puts all of §E housekeeping in I6. The **constitution
amendment moves to I1** instead, because `natural_pos` contradicts Principle I's signature-contract
text the moment the type lands — the tree would otherwise carry code that violates its own
governance for four increments. Docs-only, no code risk. The rest of §E stays in I6.

I3 keeps R-107 and R-108 together deliberately: once a press can freeze a mid-flight thumbnail, the
snap-back target must be the slot rather than the grab position, so splitting them would ship a
knowingly wrong intermediate state.

**Test session protocol**: the user runs `/usr/local/bin/wayfire -d | tee /tmp/master-log` on
**tty2**; **tty1** is the daily desktop. To end a session: `loginctl list-sessions` → the tty2
session → `loginctl terminate-session <id>`, then confirm tty1's `wayfire` PID is unchanged.
**Never kill by process name** — both sessions run `wayfire`. Each increment stops at a clean build
plus a concrete tty2 plan and waits for the user's result.
