# Implementation Plan: Spread Overview with Drag-to-Workspace

**Branch**: `001-spread-overview` | **Date**: 2026-07-11 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/001-spread-overview/spec.md`

## Summary

An in-tree Wayfire plugin (`plugins/spread-overview/`) that, on a keybind, spreads every window
from every workspace of the active output onto one screen — unoverlapped, grouped and labeled by
source workspace — and lets the user drag a window's thumbnail onto another workspace's region to
relocate it there for real. Technical approach: per-view scene transforms for the spread (the same
`view_2d_transformer_t` the `scale` plugin uses), a **pure** workspace-clustered layout function
that also owns the drop→workspace hit-test, a **self-managed** drag (per-frame transform translation,
not `core_drag_t` — see ADR-001), and relocation via `workspace_set_t::move_to_workspace`. Built and
run against Wayfire **master** (0.11-dev), inheriting the parent build's dependencies.

## Technical Context

**Language/Version**: C++17 (`meson.build:9` `cpp_std=c++17`)
**Primary Dependencies**: Wayfire master (0.11-dev) core API — scene graph
(`wf::scene::view_2d_transformer_t`, scene nodes/overlay), `wf::workspace_set_t`
(`get_views`, `get_view_main_workspace`, `move_to_workspace`, grid/current-ws), `wf::input_grab_t`
(common lib, header-only), `wf::per_output_plugin_instance_t`; wlroots 0.20.1 (transitive — `wlr_*`
input event structs); wf-config (options), wf-utils. **Not** `core_drag_t` (ADR-001).
**Storage**: N/A — transient in-memory session state; pre-session per-view state captured at
ACTIVATING and restored at DEACTIVATING.
**Testing**: doctest (`meson.build:264`, enabled with `-Dtests=true`) for the pure `layout()` unit
tests; scripted/manual integration on the running master compositor for render/input/relocate.
**Target Platform**: Wayfire master (0.11-dev) on wlroots 0.20.x, Linux, **single output**, installed
to `/usr/local`. Daily-driver compositor is `/usr/local/bin/wayfire`; Arch 0.10.1 kept as TTY
fallback (constitution Principle VIII).
**Project Type**: In-tree Wayfire compositor plugin (a `shared_module` under `plugins/`).
**Performance Goals**: spread visible within 500 ms of trigger (SC-001); 60 fps entry/drag/reflow
animation; no per-frame heap allocation on the pointer-motion path.
**Constraints**: exact restore of every touched view's alpha/transform/geometry/z-order on exit,
including forced `cancel` (Principle V); every per-view op guarded against mid-session view
destruction (Principle VI); zero thumbnail overlap (SC-005); plugin ABI `2026'07'09`.
**Scale/Scope**: single output; up to a few dozen windows across a configurable workspace grid
(typically ≤ 3×3).

## Constitution Check

*GATE: must pass before Phase 0. Re-checked after Phase 1 (below).*

| # | Principle | Status | How the design satisfies it |
|---|---|---|---|
| I | Pure, testable layout core | PASS | `layout.{hpp,cpp}` is a pure fn with zero Wayfire deps (contract: `contracts/layout.md`); it also owns the inverse drop→cluster hit-test so the two share geometry; carries doctest unit tests. |
| II | Thin API-contact wrappers | PASS | All Wayfire contact isolated in `render.cpp` (scene transforms/overlay), `input.cpp` (`input_grab_t`), `move.cpp` (`move_to_workspace`). `overview`/`layout` never call compositor APIs. |
| III | Incremental, verifiable build-up | PASS | Six ordered increments (below), each compiles + runs on master + is revertable; no step merged. |
| IV | Verify-before-build, master-only sig authority | PASS | Every API confirmed in `docs/API-MAP-verified.md` against `src/api/wayfire/**`; no `/usr/include` citations; deltas tabled. |
| V | Non-regression & coexistence | PASS\* | Per-view pre-session {alpha, transform, geometry, z-order} captured at ACTIVATING, restored at DEACTIVATING and on `cancel`; no existing plugin touched. **\*Designed, not yet proven**: the *opaque-thumbnail* mechanism depends on transformer-vs-view alpha semantics — see Known Opens (R10). |
| VI | View-lifetime safety | PASS | Views held as `wayfire_toplevel_view`; validity/`is_mapped()` checked before each per-view op; unmap signal drops the thumbnail and reflows (FR-013). |
| VII | ADR-recorded architecture | PASS | Drag pivot recorded in `docs/adr/001-self-managed-drag.md`; plan/tasks describe the self-managed drag, `core_drag_t` unused. |
| VIII | Locked build target & fallback | PASS | In-tree on master, `/usr/local`, `use_system_wlroots=disabled` pinned; Arch 0.10.1 kept as fallback; master build+boot verified 2026-07-11. |

**Technology constraints**: single-output v1 (layout takes an output param for later multi-output);
native C++ (no IPC drag); standalone plugin, not a scale fork. All honored.

**Gate result: PASS — no violations. Complexity Tracking is empty.** The gate passes *by design*; three
items below are resolved-at-implementation (they cannot be proven at plan time) and are tracked
explicitly rather than assumed — the row V asterisk is one of them.

## Known Opens / Deferred Verifications

These are **not** unresolved spec ambiguities (the spec is clarified) — they are implementation-time
verifications that the plan deliberately surfaces so "PASS by construction" is not overstated. Each
has a defined resolution point in the build-up order and must be closed before its dependent
acceptance scenario / principle is claimed done.

| ID | Open question | Resolve at | Risk if wrong | Fallback |
|---|---|---|---|---|
| R9 | Does `get_views(WSET_MAPPED_ONLY)` return fullscreen / direct-scanout clients, and can they be thumbnailed? | Build step 2 (static spread) | A fullscreen client is missing from the spread (violates FR-002) | Force composition while the overview is active |
| R10 | Does `view_2d_transformer_t` alpha **replace** or **multiply** the view's own alpha? Determines whether "force thumbnails opaque" works and whether Principle V's alpha-restore is real. | Build step 2 (render) | Thumbnails render dimmed (0.85) under the inactive-alpha daemon; restore path wrong | Capture + set the view's *own* alpha for the session and restore it |
| include_minimized | Where does `natural_size` come from for a minimized view (no displayed geometry)? | Before enabling the option | A minimized view has no size → layout undefined | Option stays **stubbed** (parsed, no effect) until specified |

R14 (inter-cluster space allocation) is **not** here — it is now *decided* (fixed-equal regions,
research.md R14) and *encoded* as layout invariants #9/#10, so the algorithm is specified, not
improvised.

## Project Structure

### Documentation (this feature)

```text
specs/001-spread-overview/
├── plan.md              # This file
├── research.md          # Phase 0 — decisions (Decision/Rationale/Alternatives)
├── data-model.md        # Phase 1 — entities + state machine
├── quickstart.md        # Phase 1 — build/run/test on master
├── contracts/
│   ├── layout.md        # the pure layout() + drop-hit-test contract (testable)
│   └── config-options.md# the metadata XML option schema (user-facing contract)
└── tasks.md             # Phase 2 — created by /speckit-tasks (NOT here)
```

### Source Code (repository root)

```text
plugins/spread-overview/
├── meson.build              # shared_module('spread-overview', ...), all_deps pattern like scale
└── src/
    ├── plugin.cpp           # per_output_plugin_instance_t + global wrapper + DECLARE_WAYFIRE_PLUGIN;
    │                        #   activator binding, activate/deactivate lifecycle
    ├── overview.hpp/.cpp     # session state machine; owns captured pre-session state + layout result
    ├── layout.hpp/.cpp       # PURE: (views+source_ws, grid, output_size, opts) -> rects+clusters;
    │                        #   + inverse drop-coord -> target cluster. NO Wayfire deps.
    ├── render.cpp            # view_2d_transformer_t thumbnails; background/label/drop-highlight overlay
    ├── input.cpp             # input_grab_t pointer/keyboard; click-vs-drag; self-managed drag; hit-test
    └── move.cpp              # thin wrapper: wset->move_to_workspace() (+ fullscreen re-request)

metadata/spread-overview.xml  # option schema (registered in metadata/meson.build)
test/plugins/spread-overview-layout.cpp   # doctest unit tests for the pure layout() (-Dtests=true)
```

Registration: add `'spread-overview'` to the `plugins = [...]` list in `plugins/meson.build:55`;
add `install_data('spread-overview.xml', ...)` to `metadata/meson.build`.

**Structure Decision**: In-tree Wayfire plugin. Module boundaries follow constitution Principle II
(pure core + thin API wrappers). The only unit-tested module is `layout` (Principle I); the wrappers
are exercised by integration on the running compositor (Principle III per-increment gate).

## Phase 0 — Research

See [research.md](./research.md). All architectural unknowns are resolved (SPEC B, self-managed drag,
scene-transform render, `move_to_workspace` relocate, fullscreen scale-in-place, stay-open reflow,
alpha coexistence, enumeration incl. fullscreen). **No `NEEDS CLARIFICATION` remain.**

## Phase 1 — Design & Contracts

- [data-model.md](./data-model.md) — the session/thumbnail/cluster/drag entities + the state machine
  (with the clarified `DRAGGING → ACTIVE` stay-open edge).
- [contracts/layout.md](./contracts/layout.md) — the pure layout + drop-hit-test contract with the
  invariants the doctest suite asserts.
- [contracts/config-options.md](./contracts/config-options.md) — the metadata XML option schema.
- [quickstart.md](./quickstart.md) — build in-tree, enable, run on master, verify each acceptance
  scenario, run the unit tests.

## Build-up order (Principle III — each builds, runs on master, is revertable)

1. **Skeleton**: `per_output_plugin_instance_t` + activator toggle → grab input via `input_grab_t` +
   clean teardown. Zero rendering. Proves lifecycle/grab/restore + master ABI load.
2. **Static spread**: enumerate `get_views` → `get_view_main_workspace` → pure `layout()` →
   `view_2d_transformer_t` per view at target rects; background/label overlay. Proves render +
   exact exit-restore (Principle V).
3. **Click-to-focus** (scale parity): click a thumbnail → focus + close.
4. **Self-managed drag** (ADR-001): press→threshold→drag; per-motion translate; snap back on release.
5. **Drop-to-relocate**: release → `layout` drop-hit-test → `move_to_workspace` → stay open + reflow.
   The payoff (User Story 2 + 3).
6. **Polish**: drop highlight, animations, fullscreen re-request on relocate, alpha coexistence,
   edge cases (view close mid-session, empty workspace, single window).

## Complexity Tracking

No constitution violations. Table intentionally empty.
