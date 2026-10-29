# Implementation Plan: Overview Opens and Closes as One Camera Zoom

**Branch**: `003-spread-zoom` | **Date**: 2026-10-29 | **Spec**: [spec.md](./spec.md)
**Input**: `specs/003-spread-zoom/spec.md` + [design-notes.md](./design-notes.md) (verified expo
mechanism + recorded decisions)

## Summary

Replace the overview's two unrelated animations with **one camera in two directions**: opening
pulls back from the current workspace's cell until the whole grid is visible, closing converges on
the cell the user is about to land on. Hybrid roles — the pivot cell's windows settle to/from their
real desktop positions (today's seamless hand-off), every other cell's thumbnails and all the
chrome follow the camera and fade.

Expo's own mechanism (a composed workspace wall with an animated viewport) is unusable here, so the
camera is **arithmetic folded into what each node already computes**: each view's existing
`view_2d_transformer_t`, and each overlay node's destination rects. One consequence is worth the
whole design: because the camera lands *inside* the view transformer, 002's live-rect hit-test
keeps working mid-zoom unchanged, which is most of FR-010 for free.

**I0 comes first and is a prerequisite, not cleanup.** Two overlay nodes currently bake an
output-sized cairo texture (**19.8 MB** at 3440×1440) and re-upload it — the dim veil on *every
cell crossing during a drag*. A baked output-sized texture cannot be scaled without re-rasterising
it per frame, so the camera is impossible to do cheaply until the veil and grid draw solid rects.
Converting them also collects **002's deferred FR-011** (the veil's bright-cell fade becomes a
per-cell alpha ramp) and removes ~40 MB of memory traffic per cell crossing.

## Technical Context

**Language/Version**: C++17 (`meson.build:9`)
**Primary Dependencies**: Wayfire 0.12.0-dev, plugin ABI `2026'08'01`
(`src/api/wayfire/plugin.hpp:110`); wlroots submodule 0.20.2; wf-config 0.12.0. Feature-specific
surface: `wf::render_pass_t::add_rect` (`src/api/wayfire/render.hpp:659`),
`begin_transform_update()` / `end_transform_update()` (`src/api/wayfire/view-transform.hpp:270,276`),
`wf::animation::simple_animation_t`. **No new dependency; no core header touched; no new
configuration option.**
**Storage**: N/A — transient session state.
**Testing**: doctest, same binary `Spread overview layout test` (`test/plugins/meson.build:12`),
`-Dtests=enabled`; tty2 sessions for the interactive gates.
**Target Platform**: self-built Wayfire at `/usr/local`; the measurement target is the user's
**3440×1440 @ 165 Hz** panel (Samsung LC34G55T over Thunderbolt→DP on the **Intel TigerLake-H GT1**
iGPU, 32 EUs) — a **6.06 ms** frame budget, which is why I0's byte-traffic reduction matters.
**Project Type**: in-tree Wayfire plugin (`plugins/spread-overview/`).
**Performance Goals**: no increase in dropped frames versus today (SC-007); the camera is a few
multiplies per view per frame plus new destination rects — **zero uploads, zero allocations**.
**Constraints**: aspect ratios preserved (FR-007); layout untouched (FR-012); exact restore
(Principle V); view-lifetime safety across two new animation paths (Principle VI); the pivot cell's
windows must land within 1 px of their real geometry (SC-002).
**Scale/Scope**: single output, ≤ a few dozen windows, grid typically 3×3.

## Constitution Check

*GATE: must pass before Phase 0; re-checked after Phase 1 (below).* Constitution **v1.3.0**.

| # | Principle | Status | How the design satisfies it |
|---|---|---|---|
| I | Pure, testable layout core | PASS | The camera is a **pure function on plain value types** (`contracts/camera.md`), beside `layout()` with its own doctests (#C1–#C7). `layout()`'s input/output is unchanged, so **Principle I's signature contract needs no amendment** — unlike 002, this feature adds a sibling, not a field. |
| II | Thin API-contact wrappers | PASS | Camera math pure; camera *state* in `render.cpp`/`overview.hpp`; overlay nodes stay render-only and merely receive a `camera_t`. No compositor call moves into the pure module. |
| III | Incremental, verifiable build-up | PASS | I0–I4 (below). I1 is test-only, so the transform is proven before it can move a pixel; I0 lands first because I2/I3 depend on it. |
| IV | Verify-before-build (master is the only authority) | PASS | Every API cited from this tree (table below); expo's mechanism read in `expo.cpp`/`workspace-wall.hpp` rather than assumed. New entries go to `docs/API-MAP-verified.md` in I0/I1. |
| V | Non-regression & coexistence | PASS | Capture/restore untouched; FR-014 keeps the dimming helper working and is re-checked at I4. **Note**: a native replacement for that helper is being considered separately — it must not be assumed here. |
| VI | View-lifetime safety | PASS | The per-view camera role is **derived** from `get_view_main_workspace` each frame, never stored, so a view that unmaps or is relocated cannot leave a stale role; `forget_view` needs no new field. |
| VII | ADR discipline | **PASS via ADR-005** | Replacing 001's directional slide-in entry reverses a recorded animation decision → `docs/adr/005-overview-camera-zoom.md` records the camera, the hybrid roles, the rejected wall approach, and the honest costs. |
| VIII | Locked build target & fallback | PASS | In-tree, `/usr/local`, Arch 0.10.1 fallback untouched; no core header changed, so the ABI stamp stays `20260801` and plugins-extra needs no rebuild (verified by the stamp check, not assumed). |

**Technology constraints**: single output, native C++, standalone plugin, option-gated behaviour
with **no new option** (Upstreamability — the camera reuses `duration`/`exit_duration`). All honored.

**Gate result: PASS.** Complexity Tracking is empty. The one structural addition (a camera) is the
feature itself, and it is contained in a pure, unit-tested function plus per-frame arithmetic.

### Principle IV verification (read in this checkout, 2026-10-29)

| Claim | Location | Result |
|---|---|---|
| Expo zooms by animating a wall viewport | `plugins/single_plugins/expo.cpp:41-46`, `:333-342`; `workspace-wall.hpp:56,69,102` | verified — not reusable (ADR-001/002) |
| Solid-rect drawing in a render pass | `src/api/wayfire/render.hpp:659` | verified (already used by `highlight_node_t`) |
| Scene-correct damage for a transform write | `src/api/wayfire/view-transform.hpp:270,276` | verified — `animate_step` already uses it; the drag path does not (R-205) |
| Veil re-rasterises the whole output per cell crossing | `plugins/spread-overview/src/overlay.cpp` (`dim_node_t::set_active` → `rerender`) | verified — 19.8 MB alloc + CPU fill + 19.8 MB upload |
| Grid relies on coincident strokes at `gap = 0` | `overlay.cpp` (`border_node_t::rerender` comment) | verified — rects must be drawn as lines, not per-cell boxes (R-204) |
| Drag damages the whole output per motion | `plugins/spread-overview/src/input.cpp:258` | verified |
| `outer_margin` hardcoded to 0 in the plugin | `plugins/spread-overview/src/render.cpp:39` | verified — the camera's cell maths can assume it, tests must not |
| Damage-driven repaint & scanout fast path exist upstream | `src/output/render-manager.cpp:1455-1456`, `:1462` | verified — the framework rewards *not* damaging the whole output |
| Workspace switch must be resolved explicitly | `scale.cpp:410-411`; `output.cpp:195` (`ensure_visible` bails at `area >= 0.1`) | verified — the close's pivot routes share this code (R-206) |

## Project Structure

### Documentation (this feature)

```text
specs/003-spread-zoom/
├── spec.md                 # input (final)
├── design-notes.md         # verified expo mechanism + recorded decisions
├── plan.md                 # this file
├── research.md             # Phase 0 — R-201…R-208
├── data-model.md           # Phase 1 — delta over 001 + 002
├── quickstart.md           # Phase 1 — per-increment tty2 gates
├── contracts/camera.md     # Phase 1 — NORMATIVE camera + overlay rendering contract
├── checklists/requirements.md
└── tasks.md                # Phase 2 (/speckit-tasks)
```

### Source code

```text
plugins/spread-overview/src/
├── layout.hpp / layout.cpp   # + pure camera_t, camera_apply, camera_scale        [I1]
├── overlay.hpp / overlay.cpp # veil + grid -> rects; per-cell alpha; set_camera   [I0, I2]
├── render.cpp                # camera state + per-frame compose; entry rewrite     [I2, I3]
├── input.cpp                 # drag damage via begin/end_transform_update;
│                             #   pivot selection per dismissal route               [I0, I2]
└── overview.hpp              # camera session state; - spread_anim::ENTRY path     [I2, I3]

test/plugins/spread-overview-layout.cpp  # camera tests #C1-#C7                     [I1]
docs/API-MAP-verified.md                 # add_rect, transform-update, wall refs    [I0, I1]
docs/adr/005-overview-camera-zoom.md     # NEW (Principle VII)                      [I4]
specs/002-spread-refine/tasks.md         # close the FR-011 deferral note           [I0]
FEATURES.md, plugins/spread-overview/README.md                                      [I4]
```

**Structure Decision**: no new module. A pure sibling function plus edits to the three existing
wrappers, which is what keeps Principle II intact.

## Increments (Principle III)

| # | Scope | Gate |
|---|---|---|
| **I0** | Veil + grid → solid rects (R-204, incl. the gap-strip and shared-edge rules); per-cell veil alpha ramp → **closes 002's FR-011**; drag damage via `begin/end_transform_update` (R-205) | build + layout suite; **tty2 §I0**: no visual change at rest, bright cell now fades, no doubled gridline, no darker gap crossing |
| **I1** | Pure `camera_t` + `camera_apply`/`camera_scale` + tests #C1–#C7. **No plugin behaviour change** | layout suite + **full** suite; exact identity at `t=0` |
| **I2** | Camera on **close**: chrome + non-pivot thumbnails; pivot cell keeps settling; pivot chosen per dismissal route (Esc/toggle, clicked view, clicked empty cell) | build/install/stamp; **tty2 §I2** |
| **I3** | Camera on **open** (mirror); **delete** 001's directional slide-in (FR-015) | build/install/stamp; **tty2 §I3**, incl. the mid-zoom grab (SC-005) |
| **I4** | ADR-005; API map; FEATURES.md + plugin README; full 001 regression (SC-006) + 20-cycle soak (SC-008) | user sign-off |

I2 and I3 are split because the close is the reported complaint (P1) and is independently
valuable: if the mirrored entry turns out to look wrong, I3 can be reverted without losing it.

## Complexity Tracking

*No constitution violations to justify.*

## Known opens (resolved at implementation, tracked in tasks)

1. **Gap-strip geometry** (R-204, INFERRED): the veil's non-overlapping cover of gaps and margins
   is new arithmetic. Confirmed visually at the I0 gate with `cluster_gap = 40`; at the defaults
   (`gap = 0`, `outer_margin = 0`) the cells tile the output and no strips exist, so the risk is
   confined to a non-default config.
2. **Fade-vs-camera interaction on the pivot cell**: its chrome zooms while its windows settle.
   Expected to read fine because the chrome is fading out, but it is a judgement call the I2 gate
   must look at directly.
3. **Camera-driven views and `max_scale`**: a camera-driven thumbnail's effective scale exceeds
   `max_scale` during a close by design (it is zooming past its real size on its way off-screen).
   Invariant #5 constrains `layout()`, not the animation — stated here so it is not "fixed" by
   mistake.
4. **SC-007 measurement**: upstream added render-scheduling diagnostics over IPC in the 2026-09
   sync. The exact call is not yet identified; if before/after numbers are wanted rather than feel,
   that is a short research task at I0.
