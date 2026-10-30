# Tasks: Overview Opens and Closes as One Camera Zoom

**Feature**: `003-spread-zoom` | **Plan**: [plan.md](./plan.md) | **Contract**:
[contracts/camera.md](./contracts/camera.md) (NORMATIVE) | **Gates**:
[quickstart.md](./quickstart.md) | **Decision**: [ADR-005](../../docs/adr/005-overview-camera-zoom.md)

Phases map onto the increments I0–I4 of `plan.md`. **Every increment ends with a build + test task,
and each one from I0 on ends with a tty2 gate that stops work until the user reports back**
(constitution Principle III).

Tests are mandatory: the contract requires them and Principle I makes the pure core the one
unit-tested component.

**Legend**: `[P]` = parallelizable · `[US1/2/3]` = user story from `spec.md`.

---

## Phase 1 — I0: overlays become rects 🔴 PREREQUISITE, NOT CLEANUP

**Why first**: a node that draws rects is scaled for free; a node that blits a baked
**output-sized** texture cannot be scaled without re-rasterising 19.8 MB per frame. The camera
(I2/I3) is impossible to do cheaply until the veil and grid stop being textures. This phase also
**closes 002's deferred FR-011** and removes ~40 MB of per-cell-crossing memory traffic.

**Independent test**: quickstart §I0 — *no visual change at rest*, plus the veil's bright cell now
fades.

- [X] T001 In `plugins/spread-overview/src/overlay.hpp`, give `dim_node_t` a rect-based model: per-cluster `cell_alpha` (animatable), the precomputed gap/margin strips, and `set_camera(...)`; drop the `wf::owned_texture_t` member and the `rerender()` declaration
- [X] T002 In `plugins/spread-overview/src/overlay.cpp`, implement `dim_node_t` rendering with `render_pass_t::add_rect` (`src/api/wayfire/render.hpp:659`): draw the **inactive cells**, not "fill everything then punch a hole" — alpha compositing cannot subtract (research R-204). Veil alpha per cell stays `background.a × (1 − inactive_brightness)`
- [X] T003 In `plugins/spread-overview/src/overlay.cpp`, compute the gap/margin cover for `cluster_gap > 0`: full-width horizontal strips between rows plus vertical strips **clipped to the row bands**, so a strip crossing is painted exactly **once** (no double-darkening). At the defaults (`cluster_gap = 0`, `outer_margin` hardcoded `0` in `render.cpp:39`) there are no strips
- [X] T004 In `plugins/spread-overview/src/overlay.cpp`, replace `border_node_t`'s baked texture with **boundary lines**: one full-width rect per row boundary and one full-height rect per column boundary, including the outer edges — a shared edge is drawn **once**, at single thickness and single alpha. Four rects per cell would paint shared edges twice (the cairo version relied on coincident strokes)
- [X] T005 **MOVED to Phase 3 (before T020)** — `set_camera(const camera_t&)` on all four overlay nodes (`dim_node_t`, `border_node_t`, `wallpaper_node_t`, `highlight_node_t`) applying the transform to **destination rects only**; it needs `camera_t`, which I1 introduces (T012), and writing the setter in I0 would mean dead API against a type that does not exist yet. The rect conversion that makes it *possible* is what I0 delivers
- [X] T006 [US3-adjacent] In `plugins/spread-overview/src/overlay.cpp` + `render.cpp`, make the veil's bright cell **fade** over the same fixed ≈120 ms ramp the highlight uses: `dim_node_t::set_active` retargets a per-cell alpha ramp instead of re-rasterising, ticked from `animate_step`. **This closes 002's deferred FR-011**
- [X] T007 In `plugins/spread-overview/src/input.cpp` (drag-follow, ~`:258`), wrap the transform write in `begin_transform_update()` / `end_transform_update()` (`src/api/wayfire/view-transform.hpp:270,276`) and **drop `output->render->damage_whole()`** — `animate_step` already does it this way; the drag path was repainting all 4.95 Mpixel per motion event (research R-205)
- [X] T008 [P] Update `specs/002-spread-refine/tasks.md` § Deferred: record that FR-011's veil half is **closed by 003 I0**, with the reason it became cheap (the veil is no longer a baked texture). 002's other artifacts stay append-only
- [X] T009 [P] Add to `docs/API-MAP-verified.md`: `render_pass_t::add_rect` (`render.hpp:659`) and `begin/end_transform_update` (`view-transform.hpp:270,276`), cited from this tree
- [X] T010 Build, run `meson test -C build` (full), install, and run the ABI-stamp check from `docs/SYNCING-UPSTREAM.md` — one `plugins:` value equal to `pluginabi` (expect `20260801`)
- [ ] T011 🛑 **tty2 gate** — present quickstart §I0 as a numbered plan (no visual change at rest; **look hard at a boundary shared by two cells and at a gap crossing with `cluster_gap = 40`**; the bright cell now fades; the drag feels no worse) and **STOP** for the user's result

---

## Phase 2 — I1: the pure camera 🔴 BLOCKS US1 AND US2

**Goal**: the transform is proven before it can move a pixel. **No plugin behaviour changes.**

**Independent test**: `meson test -C build "Spread overview layout test"` plus the full suite.

- [X] T012 In `plugins/spread-overview/src/layout.hpp`, add `struct camera_t { rectf pivot; dimf output; double t = 0.0; };` beside the existing plain types, plus declarations for `camera_apply(const camera_t&, pointf)` and `camera_scale(const camera_t&)` (contract §1). Zero Wayfire dependencies; no change to `layout()`'s signature
- [X] T013 In `plugins/spread-overview/src/layout.cpp`, implement the camera exactly as contract §2: `s = max(output.w/max(1,pivot.w), output.h/max(1,pivot.h))`, `k = 1 + (s−1)·t`, `A = C + (Oc−C)·t`, `camera_apply = (p−C)·k + A`, with `t` clamped to `[0,1]`. **Anchor on `lerp(C, Oc, t)`, never on `Oc`** — the latter is a translation at `t = 0` rather than the identity, which is the classic zoom bug
- [X] T014 Add the camera tests to `test/plugins/spread-overview-layout.cpp`: **#C1 exact identity at `t = 0`** (equality, not epsilon) over points inside, on the edge of, and far outside the pivot
- [X] T015 [P] Add **#C2** (cover at `t = 1`): the pivot centre maps to the output centre and the mapped pivot covers the output, for every cell of a 3×3 on 3440×1440 **and** a 4×2 on 1920×1080 — the non-square case the uniform "cover" scale exists for
- [X] T016 [P] Add **#C3** (one scale both axes: a unit square keeps its aspect at `t ∈ {0, .25, .5, .75, 1}`) and **#C4** (`pivot == output` ⇒ identity at every `t`, the 1×1-grid no-op)
- [X] T017 [P] Add **#C5** (monotonic over 101 `t` steps), **#C7** (`t = −1` and `t = 2` clamp), and the randomised property pass (fixed seed, grids 1×1…4×4, every cell as pivot, asserting #C1/#C3/#C5/#C7 and finiteness — no NaN/Inf)
- [X] T018 Run `meson test -C build "Spread overview layout test"` and the **full** `meson test -C build`; confirm 002's golden parity still passes untouched (the camera must not touch `layout()`). **No install, no tty2** — this increment cannot affect the desktop

---

## Phase 3 — US1 (P1): closing zooms into the workspace I am going to (I2)

**Goal**: the reported complaint, fixed and independently valuable.
**Independent test**: quickstart §I2 — six steps across all four dismissal routes.

- [X] T019 [US1] In `plugins/spread-overview/src/overview.hpp`, add the camera session state per `data-model.md`: a `camera_t`, a `simple_animation_t` driven by `opt_exit_duration` (close) / `opt_duration` (open), and `camera_pivot_index`. **No new option** (FR-008)
- [X] T020 [US1] In `plugins/spread-overview/src/render.cpp`, extend `animate_step` to tick the camera and, for each **camera-driven** view, write `scale = slot_scale × camera_scale` and the translation that puts the rendered centre at `camera_apply(slot_centre, t)`; push the same camera into the four overlay nodes (T005)
- [X] T021 [US1] In `plugins/spread-overview/src/render.cpp`, derive each view's role per frame — **pivot cell ⇒ settling** (the existing slot⇄identity path, untouched), **everything else ⇒ camera-driven** — from `get_view_main_workspace(v)` versus the pivot. Derived, never stored, so a relocated or unmapped view cannot hold a stale role (Principle VI)
- [X] T022 [US1] In `plugins/spread-overview/src/render.cpp` (`start_exit_anim`), set the pivot and run the camera `t: 0 → 1` over `exit_duration`, replacing the fixed-geometry fade of the chrome; the pivot cell's windows keep settling to their real positions (FR-005)
- [X] T023 [US1] In `plugins/spread-overview/src/input.cpp`, choose the pivot per dismissal route (FR-002): the **clicked view's workspace**, the **clicked empty cell**, or the **current workspace** for Esc/toggle. Resolve it explicitly — never via `focus_raise_view(.., true)`, whose `ensure_visible` bails once ≥10% of a view is on screen (the bug fixed in `46f31453`)
- [X] T022b [US1] In `plugins/spread-overview/src/render.cpp`, add the close's viewport handoff per research R-210: snapshot the on-screen rects, `set_workspace(destination)`, **re-express** every thumbnail's transform against its new geometry so nothing moves visually, recompute `current_ws_index`, then pivot the camera on the destination cell. Without this the destination workspace's windows settle to geometry that is OFF-SCREEN
- [X] T022c [US1] In `plugins/spread-overview/src/input.cpp`, change the clicked-thumbnail and clicked-empty-cell routes from `deactivate(false)` to an **animated** close, so the zoom actually plays on the two routes the user uses most (today they tear down instantly). Esc/toggle already animate
- [X] T023b [US1] In `plugins/spread-overview/src/move.cpp` (`switch_workspace`) and the clicked-thumbnail path in `input.cpp`, change the viewport switch from `request_workspace()` to **`set_workspace()`** — instant, no vswitch animation — so the camera is the only motion (research R-209, mirroring `expo.cpp:349`). Without this the cell zooms while vswitch simultaneously slides the viewport, which is the "being dragged sideways" the user reported at the I0 gate
- [X] T024 [US1] Build, install, run the ABI-stamp check, and `meson test -C build "Spread overview layout test"` to confirm the pure core is untouched
- [ ] T025 [US1] 🛑 **tty2 gate** — present quickstart §I2 as a numbered plan (all four dismissal routes, the 1×1-grid degeneracy, and a long `exit_duration` to watch the motion) and **STOP** for the user's result

---

## Phase 4 — US2 (P2): opening zooms out from the workspace I was on (I3)

**Goal**: open and close read as one gesture.
**Independent test**: quickstart §I3 — including the mid-zoom grab (SC-005).

- [ ] T026 [US2] In `plugins/spread-overview/src/render.cpp` (`build_spread`, `spread_anim::ENTRY`), set the pivot to the workspace current at activation and run the camera `t: 1 → 0` over `duration`; the pivot cell's windows converge from their real positions, which is what 001's entry already did for on-workspace views
- [ ] T027 [US2] In `plugins/spread-overview/src/render.cpp` + `overview.hpp`, **delete** 001's per-cluster directional slide-in: the camera subsumes it for off-workspace views (FR-015). Remove the dead branch rather than leaving it unreachable, as `finalize_entry_anim` was removed in 002
- [ ] T028 [US2] Verify FR-010 holds under the camera: because the camera is composed **into** each view's transformer, `live_thumb_rect` already reports the true on-screen rect, so press hit-testing, the drop target and the press-freeze need no change. Confirm by reading the path, and if anything bypasses the transformer, fix it rather than special-casing the camera
- [ ] T029 [P] [US2] In `plugins/spread-overview/src/input.cpp`, extend the existing `drag start` log line with the camera's `t` at press, so SC-005's 1-px check can be read from `/tmp/master-log` mid-zoom
- [ ] T030 [US2] Build, install, run the ABI-stamp check
- [ ] T031 [US2] 🛑 **tty2 gate** — present quickstart §I3 as a numbered plan (mirror check from three workspaces, the mid-zoom grab, a window closing during both animations) and **STOP** for the user's result

---

## Phase 5 — US3 (P3): the zoom never costs control

**Goal**: a correctness guard rather than a feature — it protects 002's guarantee under a moving
camera. Most of it should already hold by construction (T028); this phase is where it is *proven*.

- [ ] T032 [US3] Confirm `max_scale` is **not** clamped for camera-driven views: a thumbnail zooming off-screen deliberately exceeds it, because invariant #5 constrains `layout()`, not the animation (plan § Known opens 3). Add a comment at the clamp site so it is not "fixed" by mistake
- [ ] T033 [US3] Confirm Principle VI across both animations: a view unmapping mid-zoom is skipped by `animate_step` and scrubbed by `forget_view`, and the derived role cannot go stale. Add the scrub if anything is missing
- [ ] T034 [US3] 🛑 **tty2 gate** — the US3 scenarios: press mid-zoom grabs the thumbnail under the pointer, its drop lands on the highlighted cell, and closing a window from its application during each animation leaves no crash and no ghost. **STOP** for the user's result

---

## Phase 6 — I4: regression, docs, housekeeping

- [ ] T035 Run the **full** `meson test -C build` and the ABI-stamp check after a clean `ninja -C build && sudo ninja -C build install`
- [ ] T036 🛑 **tty2 gate** — re-run all seven 001 acceptance scenarios (SC-006) plus the 20-cycle soak mixing all four dismissal routes (SC-008), and confirm FR-014: behaviour outside the overview unchanged and the `inactive-alpha` plugin still dims correctly. **STOP** for sign-off
- [ ] T037 [P] Update the "Affected artifacts" list in `docs/adr/005-overview-camera-zoom.md` to what was actually built, and record whether the 1×1-grid and non-square-grid paths were exercised
- [ ] T038 [P] Add the camera to `FEATURES.md` under spread-overview: what changed, the hybrid roles, and that the overlay rect conversion came with it
- [ ] T039 [P] Update `plugins/spread-overview/README.md`: the animation section becomes one camera in two directions, note that `duration`/`exit_duration` drive it and that the veil's bright cell now fades (002's deferral closed)
- [ ] T040 Final report: files changed per increment, test output, deviations, and what remains. Merging and pushing are the user's decision — **ask before every push**

---

## Dependencies

```text
Phase 1 (T001-T011, I0)  ──►  Phase 2 (T012-T018, I1)  ──►  Phase 3 (T019-T025, US1)
                                                                      │
                                                                      ▼
                                                            Phase 4 (T026-T031, US2)
                                                                      │
                                                                      ▼
                                                            Phase 5 (T032-T034, US3)
                                                                      │
                                                                      ▼
                                                            Phase 6 (T035-T040)
```

- **T001 → T002 → T003** (model, then cells, then strips); **T004** is independent of them within
  the same file, so apply sequentially to avoid edit conflicts.
- **T005 before T020** (nodes must accept a camera before one is pushed into them).
- **T012 → T013 → T014…T017** (type, implementation, then tests).
- **T019 → T020 → T021 → T022 → T023** (state, tick, roles, close, pivot routes).
- **T026 → T027** (the camera must cover off-workspace entry before the old path is deleted).
- Phases are strictly sequential by the user's protocol: no increment starts before the previous
  gate is reported green.

## Parallel opportunities

- Phase 1: T008 and T009 alongside the code tasks (different files).
- Phase 2: T015, T016, T017 after T013 (independent test cases; same file, so apply in sequence).
- Phase 4: T029 alongside T026–T028 (log line only).
- Phase 6: T037, T038, T039 together.

## Implementation strategy

**MVP = Phase 1 + Phase 2 + Phase 3 (I0 + I1 + I2).** At the end of I2 the reported complaint is
fixed — the close zooms toward where you land — and I0 has already paid for itself in the drag path
and by closing 002's FR-011. Phase 4 (the mirrored open) is the half that *replaces* a liked 001
behaviour, so it ships separately and can be reverted alone if it reads worse.

## Deferred

*(Empty. Any deferral lands here with its reason, as 002's FR-011 did.)*
