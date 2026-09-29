# Feature Specification: Spread Overview Refinement — Legible Thumbnails & Motion Continuity

**Feature Branch**: `002-spread-refine`
**Created**: 2026-09-29
**Status**: Draft (clarified — ready for `/speckit-plan`)
**Input**: "When one workspace holds windows of very different sizes, the overview shows the big one
at a barely-usable size and the small ones tiny. There must be a better way to lay out the windows of
one workspace. Also refine the overview's animations." Design evidence: `design-brief.md` (this
folder) and the executable reference `reference/layout_ref.py`.

## Clarifications

### Session 2026-09-29

- Q: Which within-workspace arrangement replaces the current one (equal slots + one shared scale)?
  → A: **GNOME Shell's row-based arrangement** (per-window sizing, windows packed into rows of
  variable width, number of rows chosen automatically), compared visually against the current
  layout and a stronger-emphasis variant on the user's real geometry (`reference/layout-compare.png`).
- Q: How much should small windows be emphasized? → A: **Configurable**, default equal to GNOME's
  (×1.5 at most for the shortest windows); 1.0 turns the emphasis off. The stronger variant (×2.5)
  becomes a config value, not a code path.
- Q: Do the fixed, equal workspace regions of 001 (their size and position on screen) change?
  → A: **No.** Only the arrangement *inside* each workspace region changes. 001's invariant 9 stands.
- Q: Process? → A: Full spec-kit feature (002), implemented by the local coding agent, verified by the
  user on a throwaway second-console session before anything is pushed.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Every window on a crowded workspace is legible (Priority: P1)

The user keeps a maximized browser, a terminal and a small utility window (a volume mixer, a
calculator) on one workspace. Opening the overview, every one of those windows is shown large enough
to recognize — the small ones are no longer postage stamps just because a big window shares their
workspace — and they appear roughly where they sit on the real desktop (a window near the top stays
in the upper row, left-to-right order is kept). Everything else about the overview behaves exactly as
before.

**Why this priority**: This is the reported problem and the reason the feature exists. It is a
complete improvement on its own even if no animation is touched.

**Independent Test**: Recreate the reference scene (18 windows on a 3440×1440 output with a 3×3
workspace grid, `reference/golden-fixture.json`) or any workspace holding one maximized window plus
two small ones. Open the overview and compare thumbnail sizes against 001 (`layout-compare.png`,
top panel). Then run all of 001's acceptance scenarios to confirm nothing regressed.

**Acceptance Scenarios**:

1. **Given** the reference scene's first workspace (a maximized browser, a terminal and a volume
   mixer), **When** the overview opens, **Then** the volume mixer's thumbnail is at least twice as
   large in each dimension as under 001, and the browser's thumbnail is not smaller than under 001.
2. **Given** windows at different heights on one workspace, **When** the overview opens, **Then**
   windows that sit higher on the desktop appear in the same or an earlier row than windows below
   them, and within a row thumbnails keep the windows' left-to-right order.
3. **Given** the small-window emphasis is set to 1.0, **When** the overview opens, **Then** windows
   are arranged in rows with no extra emphasis for small windows (all windows in a row share one
   scale).
4. **Given** any number and size of windows, **When** the overview opens, **Then** no two thumbnails
   overlap, every thumbnail stays inside its workspace region, no thumbnail is larger than its real
   window, and the workspace regions are exactly where they were in 001.
5. **Given** the user drags a thumbnail to another workspace (001 User Story 2/3), **When** the
   overview re-flows, **Then** the moved window is arranged in its new workspace by the same rules.

---

### User Story 2 — Dragging and dropping never teleports a thumbnail (Priority: P2)

Two moments currently break the otherwise smooth motion: a drop that is cancelled (released over the
window's own workspace or outside any workspace) makes the thumbnail jump back instantly, and
grabbing a thumbnail while the opening animation is still playing makes every thumbnail jump to its
final place at once. After this story, a cancelled drop glides the thumbnail back to its place, and a
press during the opening animation catches the thumbnail exactly where it is on screen — it stops
there and then follows the pointer — while the others finish their motion.

**Why this priority**: Visible polish on the core interaction; independent of User Story 1.

**Independent Test**: (a) Drag a thumbnail and release it over its own workspace → it animates back.
(b) Open the overview and immediately press-drag a thumbnail that is still flying in → no thumbnail
jumps; the grabbed one stays under the cursor from its current position.

**Acceptance Scenarios**:

1. **Given** a drag in progress, **When** it is released over the window's own workspace region or
   outside every region, **Then** the thumbnail animates back to its slot over the configured
   animation duration and nothing is relocated (001 FR-009 still holds).
2. **Given** the opening animation is still running, **When** the user presses on a thumbnail and
   drags, **Then** the thumbnail that is visibly under the pointer is the one grabbed, it stops where
   it is at the press and then follows the pointer (no jump), the drop target shown matches where
   the thumbnail actually is, and the other thumbnails reach their slots without jumping.
3. **Given** a drag started during the opening animation is cancelled, **When** it is released,
   **Then** the thumbnail animates to its final slot (not to the mid-animation spot where it was
   grabbed).
4. **Given** the grabbed window is closed by its application mid-drag or mid-snap-back, **Then** the
   overview neither crashes nor leaves a ghost thumbnail (001 FR-013 still holds).

---

### User Story 3 — Snappier close and softer drop-target feedback (Priority: P3)

Closing the overview feels as responsive as opening it, and the highlight that marks the workspace
under a dragged thumbnail eases in and out instead of flicking between workspaces.

**Why this priority**: Taste-level polish; the least valuable slice and the easiest to defer.

**Independent Test**: Open and close the overview and compare the two durations; drag a thumbnail
slowly across several workspaces and watch the highlight transition.

**Acceptance Scenarios**:

1. **Given** default settings, **When** the overview closes, **Then** the close animation finishes
   in the configured close duration (shorter than the opening one by default), and setting it equal
   to the opening duration restores 001's behavior.
2. **Given** a drag in progress, **When** the pointer moves from one workspace region to another,
   **Then** the old region's highlight fades out and the new one's fades in over a short, fixed time.

---

### Edge Cases

- **One window in a workspace** — shown as large as the region allows, centered, never above its
  real size (same outcome as 001).
- **A very wide window in a narrow workspace region** (e.g. 4×2 grid on a 16:9 output) — the region's
  width limits the row, so emphasizing short windows can shrink their siblings; accepted and
  quantified in `design-brief.md §A.5`. In a 5,000-workspace random sample on the target geometry
  (3440×1440, 3×3; `layout_ref.py --fuzz-target 5000`, seed 5) the smallest thumbnail came out
  smaller than under 001 once at the default emphasis, by 4% of its area. This is a sample, not a
  bound.
- **Extremely crowded workspace** (spacing alone would not fit, e.g. dozens of windows in a small
  region) — spacing inside that workspace collapses to zero rather than producing zero-size or
  overlapping thumbnails.
- **Windows at identical positions** — ordering falls back to the stable window id; the result is
  still deterministic.
- **Degenerate sizes** (1-px windows, zero spacing) — no overlap, no thumbnail outside its region.
- **Many windows in one workspace** — thumbnails shrink to keep zero overlap; the "over-dense" signal
  from 001 is raised when the smallest thumbnail's scale falls below the layout's minimum-scale
  constant (`min_scale`, not a user option).
- **A window closes, or is relocated, while its thumbnail is animating or snapping back** — skipped
  safely (001 Principle VI), no crash, no ghost.
- **A drag starts while the overview is closing** — unchanged from 001 (input ignored while closing).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Within a workspace region, each thumbnail MUST be sized individually; a window MUST
  NOT be shrunk merely because a larger window shares its workspace (replaces 001's single shared
  scale per workspace).
- **FR-002**: Thumbnails MUST preserve their window's aspect ratio and MUST NOT exceed the window's
  real size.
- **FR-003**: Thumbnails MUST be arranged in rows within the region: row membership follows the
  windows' vertical position (top to bottom), order within a row follows horizontal position (left
  to right), ties resolved by a stable id; identical inputs MUST give identical output.
- **FR-004**: The number of rows MUST be chosen automatically to make thumbnails as large as
  possible while filling the region well (the selection rule is fixed in `design-brief.md §A`).
- **FR-005**: The system MUST expose a small-window emphasis setting (`small_window_boost`,
  number ≥ 1.0, default 1.5): the shortest windows are enlarged by up to this factor relative to
  full-height windows before packing; 1.0 disables emphasis. It MUST be declared in the plugin's
  metadata and take effect the next time the overview opens or re-flows.
- **FR-006**: All 001 layout guarantees MUST continue to hold: zero overlap (001 FR-003 / SC-005),
  fixed equal workspace regions independent of content (001 invariant 9), every thumbnail inside
  its workspace region, drop resolution agreeing with what is shown (001 FR-007/FR-008), and
  determinism.
- **FR-007**: The arrangement MUST reproduce the executable reference (`reference/layout_ref.py`)
  within 0.5 px for every window of the golden fixture at emphasis 1.0, 1.5 and 2.5.
- **FR-008**: A cancelled drop (001 FR-009) MUST animate the thumbnail back to its current layout
  slot over the configured animation duration instead of jumping.
- **FR-009**: Pressing on a thumbnail while the opening or re-flow animation is running MUST NOT make
  any thumbnail jump: the thumbnail visibly under the pointer is the one pressed, it stops at its
  current on-screen position at the press and, once a drag starts, follows the pointer; the other
  thumbnails complete their motion. Hit-testing the press and resolving the drop target MUST use
  where thumbnails actually are on screen, so FR-006's "drop agrees with what is shown" still holds
  mid-animation.
- **FR-010**: The close animation MUST use its own duration setting (`exit_duration`, default
  225 ms); the thumbnails' settle/fade and the overlay dissolve MUST both use it.
- **FR-011**: The drop-target highlight (and the matching bright workspace in the dim veil) SHOULD
  fade between workspaces over a short fixed time (≈120 ms). This requirement MAY be deferred with a
  recorded reason if it needs more than splitting the highlight into its own overlay element.
- **FR-012**: The feature MUST NOT change behavior outside the overview, MUST keep coexisting with the
  user's inactive-window dimming helper, and MUST restore every window's state on close exactly as
  001 does (001 FR-012/FR-014).

### Key Entities

- **Workspace region**: unchanged from 001 — a fixed, equal cell of the workspace grid; the drop
  target for its workspace.
- **Thumbnail row**: an ordered run of thumbnails inside one region; rows are stacked top to bottom,
  thumbnails left to right.
- **Window natural placement**: a window's real size and its position inside its own workspace;
  input to row membership and ordering.
- **Small-window emphasis**: the per-window enlargement factor derived from how short the window is
  relative to the output, capped by `small_window_boost`.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In the reference scene, the smallest thumbnail's area is at least 3× that of 001
  (001: 3,047 px², the volume mixer at 64×48; reference at default emphasis: 9,427 px²), and the
  volume mixer's thumbnail is at least 128×96 (reference: 157×116).
- **SC-002**: In the reference scene, no thumbnail is smaller than under 001, at emphasis 1.0, 1.5
  and 2.5.
- **SC-003**: Zero overlaps, zero out-of-region and zero non-positive-size thumbnails across the
  reference fuzz (6,000 random workspaces of 1–100 windows plus 3 known degenerate cases, × 3
  emphasis values; varied grids, outputs, spacing including 0, window sides down to 1 px) and across
  the implementation's own randomized test.
- **SC-004**: All 18 golden-fixture windows match the reference within 0.5 px at emphasis 1.0, 1.5
  and 2.5 (54 rectangles).
- **SC-005**: When a thumbnail is pressed during the opening animation, its on-screen position does
  not change between the press and the drag start, and after the drag starts its displacement from
  the press-time position equals the pointer's displacement since the press within 1 px (checked
  from the debug log on the test session).
- **SC-006**: A cancelled drop returns the thumbnail to its slot in the configured duration ± one
  frame; the close animation completes in `exit_duration` ± one frame.
- **SC-007**: All of 001's acceptance scenarios pass unchanged on the test session
  (`specs/001-spread-overview/quickstart.md`).

## Assumptions

- **Target geometry for evaluation** is the user's setup — 3440×1440 output, 3×3 workspace grid,
  default spacing 20 and workspace gap 0 — but nothing in the algorithm is specific to it.
- **Emphasis is keyed to window height** relative to the output height, as in GNOME (rows line up by
  height). In regions limited by width rather than height, emphasis can shrink siblings; accepted and
  quantified (Edge Cases).
- **Close duration default (225 ms = 0.75 × the 300 ms open)** is a taste default — exits a bit faster
  than entries — proposed in planning, not dictated by the user; 300 ms restores 001 exactly.
- **Highlight fade time (≈120 ms)** is a fixed constant, not a new option, to keep the configuration
  surface small (constitution: upstreamability).
- **Row selection and sizing follow GNOME Shell** (`js/ui/workspace.js`, main @ `0062bde`,
  2026-09-27) with four deliberate, documented deviations (empty rows dropped; a row's height counts
  only its own windows; the emphasis ratio is clamped; infeasible row counts are skipped, with a
  zero-spacing fallback) — see `design-brief.md §A.3`.
- **001's recorded decision R14** keeps its first half (fixed-equal regions) and loses its second half
  (one uniform scale per workspace). This reversal is recorded as ADR-004 (constitution Principle VII).
