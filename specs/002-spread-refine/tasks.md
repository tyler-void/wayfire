# Tasks: Spread Overview Refinement — Legible Thumbnails & Motion Continuity

**Feature**: `002-spread-refine` | **Plan**: [plan.md](./plan.md) | **Contract**:
[contracts/layout.md](./contracts/layout.md) (NORMATIVE) | **Gates**:
[quickstart.md](./quickstart.md)

Phases map 1:1 onto the increments I1–I6 of `design-brief.md` §F. **Every increment ends with a
build + test task and, from I2 on, a tty2 gate task that stops work until the user reports back**
(constitution Principle III; the user's risk-isolation protocol).

Tests are mandatory here — the contract §4 requires them and Principle I makes the layout the one
unit-tested component.

**Legend**: `[P]` = parallelizable (different file, no incomplete dependency) · `[US1/2/3]` = user
story from `spec.md`.

---

## Phase 1 — Setup & governance (I1a)

**Why first**: `natural_pos` contradicts the constitution's Principle I signature text the moment it
lands, so the amendment goes in *before* the code, not with the I6 housekeeping.
*(Deviation from brief §F, which put all of §E in I6 — approved by the user in the planning review.)*

- [ ] T001 Amend `.specify/memory/constitution.md` to **v1.3.0 (MINOR)**: Principle I's signature contract (`:55`) gains `natural_pos` and the emphasis option; add a Sync Impact entry at the top recording 1.2.0 → 1.3.0 and its rationale
- [ ] T002 In `.specify/memory/constitution.md`, refresh every stale build fact, each re-verified in this tree first: `:138-140` (0.11-dev → 0.12-dev; ABI `2026'07'09` → `2026'08'01` per `src/api/wayfire/plugin.hpp:110`), `:145`, `:167-168` (wlroots 0.20.1 → submodule tag 0.20.2; wf-config 0.11.0 → required `>=0.12.0,<0.13.0` per `meson.build:111`, submodule declares 0.12.0), `:173`; **keep** the historical boot-evidence line `:149` and **add** the 2026-09-29 upstream sync as current evidence (built, 35/35 tests, verified on tty2)
- [ ] T003 [P] Update `docs/API-MAP-verified.md:139` so the layout input reads `{view_id, source_ws, natural_size, natural_pos}`, and add entries for `wf::animation::duration_t(std::shared_ptr<option_t<animation_description_t>>)` (`subprojects/wf-config/include/wayfire/util/duration.hpp:81`) and `simple_animation_t` (same file `:188`), citing this tree only

---

## Phase 2 — Foundational: the pure layout core (I1b) 🔴 BLOCKS EVERYTHING

**Goal**: the new arrangement is provably correct **before** any plugin code changes. No plugin
behaviour changes in this phase.

**Independent test**: `meson test -C build "Spread overview layout test"` plus the full suite.

### Types

- [ ] T004 In `plugins/spread-overview/src/layout.hpp`, add `struct pointf { double x = 0, y = 0; };` beside `ivec2`/`rectf`/`dimf`; **append** `pointf natural_pos{};` to `layout_input_view` (braces required — without them g++13/clang warn `-Wmissing-field-initializers` on the existing test initializers) and **append** `double small_window_boost = 1.5;` to `layout_options`; change no existing member, order or default (contract §1)

### Algorithm

- [ ] T005 In `plugins/spread-overview/src/layout.cpp`, implement the row layout of contract §2 as the cluster packer — `b(v)` emphasis with the D3 clamp, `compute_layout(k)` with D1/D2, `scale_space`, `better`, the `choose k` search, and `place` — replacing `pack_cluster`'s sizing while leaving the cluster-region code (invariant #9) untouched; sizes clamped via `W(v)/H(v) = max(1, ·)`, emphasis and centres on raw values
- [ ] T006 In `plugins/spread-overview/src/layout.cpp`, add the D4 feasibility guard (`s > 0`, row/column spacing fits) plus the "retry with spacing 0" fallback, and set `over_dense` from the **smallest per-window scale** `< min_scale` (contract §3 #10)

### Tests (contract §4)

- [ ] T007 Generate the golden constants: run `python3 specs/002-spread-refine/reference/layout_ref.py --golden /tmp/g.json`, confirm `cmp` against `reference/golden-fixture.json`, and emit the 18 input windows + 54 expected rects (β ∈ {1.0, 1.5, 2.5}) as C++ constants into `test/plugins/spread-overview-layout.cpp` — the test must never parse JSON or invoke Python
- [ ] T008 Add the golden-parity test (invariant #13, SC-004) to `test/plugins/spread-overview-layout.cpp`: every rect within **0.5 px** at the three β values
- [ ] T009 Add the SC-002 test to `test/plugins/spread-overview-layout.cpp` using a **test-only** copy of 001's packer named `pack_cluster_001_oracle` — the test TU `#include`s `layout.cpp` directly (`:8`), so reusing the name `pack_cluster` would be a redefinition in the same TU; assert no fixture window shrinks at the three β
- [ ] T010 [P] Add the three D4 dense repros from `layout_ref.py::dense_repros()` to `test/plugins/spread-overview-layout.cpp` (480×540 region, spacing 40: 1920×1080 + 11×60×60, + 12×60×60, and 85×800×600): positive sizes, no overlap, inside the region
- [ ] T011 [P] Add the randomized property test to `test/plugins/spread-overview-layout.cpp`: `std::mt19937{11}`, ≥ 2,000 clusters × 3 β, spacing including 0, 1–100 windows, sides down to 1 px → invariants #2, #3, #4, #5, #10 positivity, #11; **must not** assert "smallest ≥ 001" (research R-105)
- [ ] T012 [P] Add the invariant #12 test (β = 1.0 ⇒ every uncapped window in a row shares one scale) to `test/plugins/spread-overview-layout.cpp`
- [ ] T013 Add an epsilon-1e-6 comparison helper for the **new** tests in `test/plugins/spread-overview-layout.cpp` (at spacing 0 adjacent rects touch and the existing strict `overlaps()` reports ~1e-12 px false overlaps); leave the existing `overlaps()` and the existing spacing-20 tests untouched
- [ ] T014 Confirm the existing over-dense test (`test/plugins/spread-overview-layout.cpp:127-144`) still passes unchanged, as contract §4.6 / brief §B require (its 0.0389 min-scale depends on the test's `outer_margin = 20.0`). It is **NORMATIVE that it holds**: if it does not, **STOP and report the evidence** (the new arithmetic, and why) — do not relax, rewrite or delete the assertion

### I1 gate

- [ ] T015 Run `ninja -C build`, `meson test -C build "Spread overview layout test"` and the **full** `meson test -C build`; report files changed and full test output. **No install, no tty2** — this increment cannot affect the desktop

---

## Phase 3 — US1 (P1): every window on a crowded workspace is legible (I2)

**Goal**: the new layout reaches the screen, both options are live.
**Independent test**: quickstart §I2 — reference-like scene matches `layout-compare.png` panel 2;
β = 1.0 and 2.5 visibly differ; 001 behaviour spot-checks clean.

- [ ] T016 [US1] In `plugins/spread-overview/src/render.cpp` (`build_spread`, the input loop at `:52-63`), fill `natural_pos` workspace-locally as `view geometry − (source_ws − current_ws) · output_size`, using the basis already proven in `move.cpp:86`
- [ ] T017 [P] [US1] In `plugins/spread-overview/src/overview.hpp`, append `wf::option_wrapper_t<double> opt_small_window_boost{"spread-overview/small_window_boost"};` **after the option block that ends at `:134`** (the brief's `:122-126` is a stale range — verified)
- [ ] T018 [US1] In `plugins/spread-overview/src/render.cpp` (`current_layout_options()`, `:34-41`), pass `small_window_boost` into `layout_options`, clamping values `< 1.0` to `1.0`
- [ ] T019 [P] [US1] In `metadata/spread-overview.xml`, declare `small_window_boost` (`type="double"`, default `1.5`, `min="1.0"`, `max="4.0"`) with a `_short`/`_long` describing the small-window emphasis
- [ ] T020 [US1] Build, install (`sudo ninja -C build install`) and run the ABI-stamp check from `docs/SYNCING-UPSTREAM.md` — one `plugins:` value equal to `pluginabi` (expected unchanged at `20260801`, so plugins-extra needs no rebuild)
- [ ] T021 [US1] 🛑 **tty2 gate** — present quickstart §I2 as a numbered plan (what to do, what to see, what counts as failure, how to roll back) and **STOP**; wait for the user's result before Phase 4

---

## Phase 4 — US2 (P2): dragging and dropping never teleports (I3)

**Goal**: no jump on press during animation; cancelled drops glide back; no thumbnail is ever left
frozen; the thumbnail the user *sees* under the pointer is the one grabbed.
**Independent test**: quickstart §I3 steps 1–7, including the `drag start` log check for SC-005.

- [ ] T022 [US2] In `plugins/spread-overview/src/render.cpp`, factor the live-rect math out of `snapshot_thumb_screen_rects()` (`:488-505`) into one reusable helper (centre = view centre + translation, size = view size × scale), declared in `overview.hpp`
- [ ] T023 [US2] In `plugins/spread-overview/src/render.cpp`, factor the duplicated `anim_hook` installation (`:245-255` and `:442-450`) into a single `ensure_anim_hook()` helper — the snap-back would otherwise add a third copy
- [ ] T024 [US2] In `plugins/spread-overview/src/render.cpp`, factor the "animate this thumbnail from its current transform back to its layout slot" logic (slot scale/translation as `:109-112`, `alpha.set(1.0, 1.0)`, `ensure_anim_hook()`) into one helper — used by both the cancelled-drop snap-back (T028) and the frozen-press release (T029), so the two paths cannot drift apart
- [ ] T025 [US2] In `plugins/spread-overview/src/input.cpp`, on **press** (not at the drag threshold) erase `press_view`'s entry from `anim_state` if present, freezing that thumbnail at its drawn position while the other clocks keep running (research R-107)
- [ ] T026 [US2] In `plugins/spread-overview/src/render.cpp` (`build_spread`/`reflow`), capture the session's view order **once** via `output->wset()->get_views(WSET_SORT_STACKING | ...)` and store it as the hit-test order — **top-most first** (verified: the sort is ascending by child index and `scene::raise_to_front` inserts at `children.begin()`, so index 0 is the front). The flag is documented as slow (`src/api/wayfire/workspace-set.hpp:40-43`), so it MUST NOT be called per event; stacking cannot change during the overview because raising is descoped. Record the API in `docs/API-MAP-verified.md` with these `file:line`s (Principle IV)
- [ ] T027 [US2] In `plugins/spread-overview/src/input.cpp`, make `thumb_at()` (`:22-34`) and `dragged_thumb_center()` (`:36-50`) use the live rect helper (T022) instead of the slot rects in `thumb_rects`, and iterate candidates in the **stacking order captured by T026, top-most first**, returning the first hit — mid-animation the live rects overlap (entry starts from real desktop positions, where a maximized window covers others) and `thumb_rects` is a pointer-keyed `std::map`, whose order is arbitrary. Fixes FR-009's "the thumbnail visibly under the pointer"; with no overlap the result is unchanged
- [ ] T028 [US2] In `plugins/spread-overview/src/input.cpp` (snap-back branch `:269-277`), replace the instant `drag_orig_tx/ty` assignment with the T024 helper, driven by `duration` (FR-008)
- [ ] T029 [US2] In `plugins/spread-overview/src/input.cpp`, close the **frozen-thumbnail leak**: any release that neither relocates nor closes the overview MUST return a thumbnail frozen by that press to its layout slot via the T024 helper. This covers the release paths at `:100-122` that currently do nothing — a sub-threshold release **outside** the pressed thumbnail's rect or **over a different** thumbnail (`press_view && press_view != rel_view`), and any other non-click, non-drag, non-relocate release. Without this the thumbnail stays frozen mid-flight until the next reflow or close
- [ ] T030 [US2] In `plugins/spread-overview/src/input.cpp:173`, remove the `finalize_entry_anim()` call, then delete its definition (`render.cpp:456-482`) and declaration (`overview.hpp:219`) — verified to have exactly one call site, so no dead code is left
- [ ] T031 [P] [US2] In `plugins/spread-overview/src/input.cpp`, extend the existing `LOGI("spread-overview: drag start")` with the press position, the thumbnail's position at press and the pointer displacement, so SC-005's 1-px check is readable from `/tmp/master-log`
- [ ] T032 [US2] Verify view-lifetime safety for the new clocks (Principle VI): confirm `forget_view()` erases `anim_state` entries so a window closing mid-drag, mid-snap-back or mid-frozen-release leaves no dangling state, and that the T026 stacking order is scrubbed too; add the scrub if it is missing
- [ ] T033 [US2] Build, install, run the ABI-stamp check; run `meson test -C build "Spread overview layout test"` to confirm the pure core is untouched
- [ ] T034 [US2] 🛑 **tty2 gate** — present quickstart §I3 as a numbered plan and **STOP**; wait for the user's result before Phase 5

---

## Phase 5 — US3 (P3): snappier close, softer highlight (I4, I5)

**Goal**: independent close duration; highlight fades between workspaces.
**Independent test**: quickstart §I4 and §I5.

### I4 — `exit_duration`

- [ ] T035 [P] [US3] In `plugins/spread-overview/src/overview.hpp`, append `wf::option_wrapper_t<wf::animation_description_t> opt_exit_duration{"spread-overview/exit_duration"};` after the option block (ends `:134`) and change `overlay_fade{opt_duration}` (`:161`) to `overlay_fade{opt_exit_duration}` — it only ever runs during an animated close (`:160`)
- [ ] T036 [US3] In `plugins/spread-overview/src/render.cpp`, change `start_exit_anim`'s clock source (`:412` `anim_state.try_emplace(v, opt_duration)`) to `opt_exit_duration`
- [ ] T037 [P] [US3] In `metadata/spread-overview.xml`, declare `exit_duration` (`type="animation"`, default `225ms`) and update `duration`'s `_long` at `:16` from "Entry/exit and reflow animation length." to "Entry and reflow animation length (the close uses exit_duration)."
- [ ] T038 [US3] Build, install, run the ABI-stamp check
- [ ] T039 [US3] 🛑 **tty2 gate** — present quickstart §I4 as a numbered plan (including `exit_duration = 300ms` restoring 001) and **STOP**; wait for the user's result

### I5 — highlight fade (SHOULD; deferrable)

- [ ] T040 [US3] Assess splitting the drop-target highlight out of `border_node_t`'s shared cairo texture (`overlay.cpp` `rerender()` `:36-81`, highlight stroke `:69-73`, single upload `:76`) into its own node/texture with an alpha ramp. **If it needs more than that one split**, stop and record the deferral (reason + what it would take) in this file under "Deferred", per FR-011's MAY
- [ ] T041 [US3] Implement the ≈120 ms fixed alpha ramp for the highlight element and make `dim_node_t::set_active`'s bright-cell switch follow the same ramp (research R-110); the constant stays a constant, **not** a new option
- [ ] T042 [US3] Build, install, run the ABI-stamp check
- [ ] T043 [US3] 🛑 **tty2 gate** — present quickstart §I5 as a numbered plan and **STOP**; wait for the user's result

---

## Phase 6 — Polish, regression, housekeeping (I6)

- [ ] T044 Run the **full** `meson test -C build` and confirm the ABI-stamp check one final time after a clean `ninja -C build && sudo ninja -C build install`
- [ ] T045 🛑 **tty2 gate** — re-run all seven 001 acceptance scenarios from `specs/001-spread-overview/quickstart.md` (SC-007), explicitly covering FR-012 (the `inactive-alpha` helper still works; every window's position/size/stacking/opacity is restored exactly on close), and **STOP** for the user's sign-off
- [ ] T046 [P] Update the "Affected artifacts" list in `docs/adr/004-per-window-cluster-scale.md` to the files actually changed, and record whether FR-011 shipped or was deferred
- [ ] T047 [P] Add the refinement to `FEATURES.md` under spread-overview (what changed, the measured effect, links to ADR-004 and `specs/002-spread-refine/`)
- [ ] T048 [P] Update `plugins/spread-overview/README.md`: `small_window_boost` and `exit_duration` in the options tables, and a short note that within-workspace arrangement is row-based with per-window sizing
- [ ] T049 Final report to the user: files changed per increment, test results, deviations, and what remains (merging is the user's decision; **ask before every push** — the push policy is unchanged)

---

## Dependencies

```text
Phase 1 (T001-T003)  ──► Phase 2 (T004-T015)  ──► Phase 3 (T016-T021, US1)
                                                        │
                                                        ▼
                                              Phase 4 (T022-T034, US2)
                                                        │
                                                        ▼
                                        Phase 5 (T035-T039 I4, T040-T043 I5, US3)
                                                        │
                                                        ▼
                                              Phase 6 (T044-T049)
```

- **T004 blocks everything else in Phase 2** (types come first).
- **T005 → T006** (the guard wraps the search), **T007 → T008/T009** (constants before the tests
  that use them).
- **Phase 4 internals**: T022 → T027 (live rects), T023 → T024 (hook helper first),
  T024 → T028 and T029 (one snap-back helper, two callers), T026 → T027 (order before it is used),
  T025 → T029 (freezing is what creates the leak).
- **Phases are strictly sequential** by the user's protocol: no increment starts before the previous
  one's tty2 gate is reported green. Parallel `[P]` applies *within* a phase only.
- US1/US2/US3 are independent in the spec, but the increments are ordered P1 → P2 → P3 so the
  highest-value slice ships first and each gate isolates one class of failure.

## Parallel opportunities

- Phase 1: T003 alongside T001/T002 (different files).
- Phase 2: T010, T011, T012 after T007 (independent test cases, same file — apply sequentially if
  editing conflicts).
- Phase 3: T017 and T019 alongside T016 (`overview.hpp` / XML / `render.cpp`).
- Phase 4: T031 alongside the behavioural tasks (log line only).
- Phase 5: T035 and T037 together.
- Phase 6: T046, T047, T048 together.

## Implementation strategy

**MVP = Phase 1 + Phase 2 + Phase 3 (I1 + I2)** — that is the whole reported problem (P1) and is
independently valuable even if US2/US3 are never built. Phases 4 and 5 are polish, each behind its
own gate; I5 is explicitly deferrable.

## Deferred

*(Empty. T040 writes here if FR-011 is deferred.)*
