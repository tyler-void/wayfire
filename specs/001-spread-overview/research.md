# Phase 0 Research: Spread Overview

All decisions are grounded in `docs/API-MAP-verified.md` (master signatures), the constitution, and
`docs/adr/001-self-managed-drag.md`. **No `NEEDS CLARIFICATION` remain.**

## R1 — Plugin form: standalone vs fork of scale
- **Decision**: Standalone in-tree plugin (SPEC B). Not a fork of `scale`.
- **Rationale**: `scale`'s all-workspace layout is flat-packed (`get_all_workspace_views()` +
  `layout_slots()` pack all views into one size-sorted grid; zero per-workspace regions), so
  drop-to-workspace is undefined there without invasive layout surgery in a 1562-line core plugin.
  A standalone plugin owns a workspace-clustered layout from the start.
- **Alternatives**: Fork scale (SPEC A) — rejected: flat-pack + rebase burden.

## R2 — Drag mechanism: self-managed vs core_drag_t
- **Decision**: Self-managed drag — per-frame update of the thumbnail's `view_2d_transformer_t`
  translation. `core_drag_t` is **not** used in v1.
- **Rationale**: `core_drag_t::adjust_view_on_output` resolves the target workspace from physical
  output-edge math (`grab.x / output_geometry.width`), meaningless when all workspaces are on one
  screen; it also drives the real view, not a thumbnail, and its `handle_motion` signature drifted
  (`point_t`→`pointf_t`). Full rationale: ADR-001.
- **Alternatives**: Reuse `core_drag_t` — rejected (wrong drop model, wrong subject, version churn).

## R3 — Rendering: scene transform vs custom output renderer
- **Decision**: Per-view scene transform (`wf::scene::view_2d_transformer_t`, header
  `src/api/wayfire/view-transform.hpp:340`), added via
  `view->get_transformed_node()->add_transformer(...)`; a separate overlay scene node for
  background dim, labels, and drop highlight.
- **Rationale**: Exactly how `scale` renders thumbnails; the scene API cleanly supports scale +
  translate + alpha and per-frame mutation for the drag. No custom renderer risk.
- **Alternatives**: Custom output renderer (expo-style) — rejected (more code, more churn).

## R4 — Relocate API
- **Decision**: `output->wset()->move_to_workspace(view, target_ws)`
  (`workspace-set.hpp:168`), the same call `vswitch` uses.
- **Rationale**: Stable, same-output. `start_move_view_to_wset` is for cross-*output* moves (out of
  scope for single-output v1).
- **Alternatives**: `start_move_view_to_wset` — deferred to multi-output v2.

## R5 — Fullscreen window handling
- **Decision**: Scale fullscreen windows in place and keep them fullscreen; no temporary
  un-fullscreen. On relocate, re-issue `window_manager::fullscreen_request(view, output, true,
  target_ws)`.
- **Rationale** (clarified 2026-07-11 + verified): `scale` does *zero* fullscreen special-casing
  (`grep -ni fullscreen plugins/scale/scale.cpp` → none) yet scales fullscreen views fine — the
  transformer handles a fullscreen view directly. Un-fullscreening would add capture/restore state
  (fighting Principle V) and a client-visible reconfigure. `fullscreen_request` exists at
  `window-manager.hpp:81`; `pending_fullscreen()`/`toplevel::fullscreen` query state.
- **Alternatives**: Temporarily un-fullscreen for the session — rejected (needless complexity/risk).

## R6 — Behavior after a successful relocate
- **Decision**: Overview stays open and re-flows (moved window animates to its new cluster); exit
  only via trigger/Esc/background-click.
- **Rationale** (clarified 2026-07-11): enables User Story 3 (reorganize many in one session);
  fixes the state machine edge `DRAGGING → ACTIVE`.
- **Alternatives**: Close after each move — rejected (drops US3).

## R7 — Input grab
- **Decision**: `wf::input_grab_t` (`plugins/common/wayfire/plugins/common/input-grab.hpp`,
  header-only), constructed `input_grab_t(name, output, this, this, this)` implementing
  `wf::pointer_interaction_t` + `wf::keyboard_interaction_t`; `grab_input(wf::scene::layer::OVERLAY)`
  / `ungrab_input()`.
- **Rationale**: The common-lib grab the modern `scale` uses; header-only, no extra link.
- **Alternatives**: Hand-rolled seat grab — rejected (reinvents a maintained helper).

## R8 — View enumeration + source workspace
- **Decision**: `output->wset()->get_views(WSET_MAPPED_ONLY [| WSET_EXCLUDE_MINIMIZED])` for the
  set; `get_view_main_workspace(view)` for each view's source workspace; `get_workspace_grid_size()`
  + `get_current_workspace()` for the grid.
- **Rationale**: `get_view_main_workspace` is the per-view→workspace mapping that makes clustered
  layout + labels possible (the mapping SPEC A feared scale lacked).
- **Alternatives**: Recompute workspace from geometry — rejected (the API already provides it).

## R9 — Fullscreen/direct-scanout enumeration  [RESOLVED 2026-07-11 — by code evidence]
- **Finding — `get_views(WSET_MAPPED_ONLY)` DOES include fullscreen toplevels.** Evidence:
  1. Fullscreen is a **toplevel state, not a layer** — the scene `enum class layer`
     (`scene.hpp:463`) has BACKGROUND/WORKSPACE/TOP/OVERLAY and **no `LAYER_FULLSCREEN`**.
  2. Core itself iterates fullscreen views *out of* `get_views(WSET_MAPPED_ONLY)`:
     `src/output/workspace-impl.cpp:233` does `for (view : get_views(WSET_MAPPED_ONLY)) { … if
     (view->toplevel()->current().fullscreen) … }` — only meaningful because fullscreen views are
     in the returned set.
- **Conclusion**: no enumeration fallback needed. A fullscreen view is an ordinary workspace toplevel
  and gets a thumbnail via the same `view_2d_transformer_t` path.
- **Direct-scanout** is a render optimization Wayfire disables whenever composition is required — and
  the overview forces composition (input grab + per-view transformers), so a scanned-out client is
  composited normally while active. **To confirm live**: open a genuinely fullscreen client (video /
  F11 browser), open the overview, verify its thumbnail appears and renders. If a live gap ever
  shows, the fallback is `force_composition` while active — but the code says it won't be needed.

## R10 — Alpha coexistence with the inactive-alpha daemon  [RESOLVED 2026-07-11 — inspect done (T012)]
- **Requirement**: thumbnails render opaque during the session regardless of the daemon's per-view
  alpha; each view's pre-session alpha is captured at ACTIVATING and restored exactly at DEACTIVATING
  (FR-014, Principle V).
- **Finding (T012) — MULTIPLIES.** Three independent proofs:
  1. Header: *"A multiplier for the view's opacity. … setting alpha=1.0 does not make it opaque."*
     (`src/api/wayfire/view-transform.hpp:350`).
  2. **Render node**: `src/view/view-3d.cpp:269` composites the (already-rendered) child texture at
     `self->get_alpha()` — so each transformer in the stack multiplies the opacity below it.
  3. **The daemon's own mechanism is a stacked transformer**: the `alpha` plugin sets a view's alpha
     by adding a `view_2d_transformer_t` **named `"alpha"`** at z-order `TRANSFORMER_2D`
     (`plugins/single_plugins/alpha.cpp:92-93`; `adjust_alpha` just does `tr->alpha = value`,
     `:99-107`). Our spread transformer sits at `TRANSFORMER_2D + 1`, above it → they multiply
     (0.85 × 1.0 = 0.85). Naive "force opaque via our transformer" fails.
- **Decision — candidate (a).** The view's "own alpha" *is* the `"alpha"`-named transformer's value
  (or 1.0 if absent). At ACTIVATING, for each view read that transformer; if present, capture its
  value and set it to 1.0 (opaque). At DEACTIVATING/`cancel`, restore the captured value exactly, so
  the daemon's dimming state is untouched. Implemented in T015 → flips plan row-V `PASS*` → `PASS`.

## R11 — Testing the pure layout
- **Decision**: doctest unit tests for `layout.cpp` under `test/`, built with `-Dtests=true`
  (`meson.build:264`). Pure inputs/outputs → no compositor needed.
- **Rationale**: Constitution Principle I mandates isolated unit tests; doctest is Wayfire's existing
  unit-test framework.
- **Alternatives**: Manual-only — rejected (violates Principle I). Note: the `doctest` package must
  be present to build tests (header-only; not required for the plugin itself).

## R12 — Build wiring
- **Decision**: In-tree: add `'spread-overview'` to `plugins = [...]` (`plugins/meson.build:55`);
  create `plugins/spread-overview/meson.build` mirroring `plugins/scale/meson.build`'s `all_deps` +
  `all_include_dirs`; register `metadata/spread-overview.xml` in `metadata/meson.build`.
- **Rationale**: Inherits the parent build's resolved `wlroots`/`wfconfig` objects — the plugin never
  re-declares wlroots (Principle VIII); consistent with every other core plugin.
- **Alternatives**: Out-of-tree via `wayfire.pc` — rejected by Principle VIII.

## R13 — Minimized windows  [`include_minimized`: STUBBED — known-open]
- **Decision**: Excluded from the spread by default (`WSET_EXCLUDE_MINIMIZED`); matches scale.
- **`include_minimized` is exposed but STUBBED (non-functional) in v1.** Gap: `layout()` requires a
  `natural_size` for every view, but a minimized view has no *displayed* geometry. The source of
  `natural_size` when minimized is unspecified — probably the view's retained `get_geometry()`
  (Wayfire toplevels keep their size across minimize) or a saved last-known size, but this is **not
  verified**. Resolve (confirm `get_geometry()` is valid while minimized, or choose a fallback size)
  before making the option functional. Until then the option is accepted by config but has no effect.
- **Rationale**: don't ship a half-working toggle; record the data-source gap explicitly.

## R15 — Raising the dragged thumbnail  [DESCOPED — won't-do]
- **Decision**: do NOT raise the dragged thumbnail above the others during drag.
- **Rationale**: the drop-center fix (drop + highlight both resolve from the dragged
  thumbnail's center, not the cursor) removed the *functional* need — a window lands on
  exactly the highlighted cell even when the thumbnail is partly occluded. Raising is now
  purely cosmetic. Exact z-order restore would require capturing/restoring the full wset
  child-node list (no clean insert-at-index API; `get_views` order is not documented as
  stacking), which is high restore-path risk (Principle V) for a small visual gain.
- **Revisit** only if occlusion during drag proves annoying in real use.

## R14 — Inter-cluster space allocation  [DECIDED]
- **Decision**: **Fixed-equal cluster regions on the workspace grid.** The working area (output minus
  `outer_margin`) is divided into a `grid.width × grid.height` matrix of equal cells separated by
  `cluster_gap`; cluster (cx,cy) occupies cell (cx,cy). Cluster region size is **independent of
  content density**. Within a cluster, all views share one uniform scale — the largest that fits all
  of them with `spacing` and zero overlap, capped at `max_scale`. Density affects thumbnail *size*,
  never cluster *position*.
- **Rationale**: the feature's core value is knowing *where each window lives*; the user recognizes a
  workspace by its **grid position**. Fixed-equal regions make on-screen cluster position a bijection
  with workspace grid position — the spatial mental map is preserved exactly. Fully deterministic and
  simple to unit-test.
- **Trade-off (accepted)**: a dense cluster (e.g. 8 windows) packs smaller thumbnails than a sparse
  one (1 window); sparse cells "waste" space. This is the honest cost of legibility, bounded in v1
  scope (a few dozen windows over ≤3×3). A `min_scale` **soft target** flags pathological density but
  never overrides zero-overlap (see `contracts/layout.md`).
- **Alternatives rejected**:
  - *Content-proportional* (cluster size ∝ window count/area): best space use, but wildly variable
    cluster sizes break the grid mental map — a workspace would change size/position with its content,
    defeating spatial legibility. **Rejected.**
  - *Hybrid* (fixed grid position, row/column sizes vary with density): preserves position while
    easing density, but adds complexity and still shifts cell sizes. **Deferred** to a possible v2
    option.
- **Encoded** in `contracts/layout.md` as invariant #9 (fixed-equal region-sizing rule) and #10
  (uniform within-cluster scale).
