# Tasks: Spread Overview with Drag-to-Workspace

**Input**: Design documents from `specs/001-spread-overview/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests policy**: The pure `layout()` unit tests are **REQUIRED** (constitution Principle I —
non-negotiable), so they appear as first-class tasks. All other verification is **on-compositor per
increment** (Principle III) — each user story ends with an independent test run from `quickstart.md`,
not an automated integration suite.

**Format**: `- [ ] [ID] [P?] [Story?] Description with file path` — `[P]` = parallelizable
(different files, no incomplete deps); `[US#]` only on user-story tasks.

**Paths**: in-tree under `plugins/spread-overview/` on the Wayfire master checkout; unit test under
`test/plugins/`; metadata in `metadata/`.

---

## Phase 1: Setup (build wiring — it compiles and loads on master)

- [x] T001 Create `plugins/spread-overview/meson.build` as a `shared_module('spread-overview', …)` mirroring `plugins/scale/meson.build` (`all_include_dirs`, `all_deps`, `install_dir: conf_data.get('PLUGIN_PATH')`); no `link_with: move_drag_interface` (ADR-001)
- [x] T002 Register the plugin by adding `'spread-overview'` to the `plugins = [...]` list in `plugins/meson.build:55`
- [x] T003 [P] Create `metadata/spread-overview.xml` with all 9 options per `contracts/config-options.md`, and register it via `install_data('spread-overview.xml', install_dir: conf_data.get('PLUGIN_XML_DIR'))` in `metadata/meson.build`
- [x] T004 Create a minimal `plugins/spread-overview/src/plugin.cpp` (`wf::per_output_plugin_instance_t` + global `per_output_plugin_t` wrapper + `DECLARE_WAYFIRE_PLUGIN`, empty `init/fini` with `LOGI`); `ninja -C build && sudo ninja -C build install`; confirm it loads on `/usr/local/bin/wayfire` (appears in `-d` log, ABI `2026'07'09` accepted) — ✅ **loads on master** (log: `Loaded plugin /usr/local/lib/wayfire/libspread-overview.so`, `init on DP-3` + `init on HDMI-A-1`)

---

## Phase 2: Foundational (blocking prerequisites — pure core + lifecycle skeleton)

**⚠️ CRITICAL**: No user story can begin until this phase completes. Per Principle I the pure layout
tests are written before/with the implementation.

- [x] T005 [P] Define pure value types + function signatures in `plugins/spread-overview/src/layout.hpp` per `contracts/layout.md` (`layout_input_view`, `layout_options` incl. `min_scale`, `cluster_out` w/ `over_dense`, `view_out`, `layout_result`) — zero Wayfire includes
- [x] T006 [P] Write the doctest suite in `test/plugins/spread-overview-layout.cpp` asserting invariants 1–10 from `contracts/layout.md`, including the **density-invariant regions** case (#9) and the **over-dense flag** case (#10) and the **hit-test round-trip** (#6); ensure it FAILS before T007
- [x] T007 Implement pure `layout()` + `hit_test_cluster()` in `plugins/spread-overview/src/layout.cpp` — fixed-equal grid-cell regions (R14/#9), uniform within-cluster scale via compiz-style row/col packing (#10, borrow `plugins/scale/scale.cpp:920+` logic), zero-overlap always wins — until every T006 assertion passes
- [x] T008 Wire the layout unit test into the `test/` meson tree so `meson configure build -Dtests=enabled && ninja -C build && meson test -C build` runs it green — ✅ **PASS: 7 cases / 1902 assertions**; `meson test` → `1/1 ... OK` (note: option is `-Dtests=enabled`, a feature, not `=true`)
- [x] T009 Implement session lifecycle in `plugins/spread-overview/src/{plugin,overview}.cpp`: activator binding (`toggle`), `activate_plugin`/`deactivate_plugin`, `wf::input_grab_t` `grab_input(layer::OVERLAY)`/`ungrab_input`, Esc + re-trigger exit, clean teardown — **zero rendering** (build step 1) — ✅ **verified on master**: toggle → `ACTIVE - input grabbed`, pointer events captured (BTN 272), toggle again → `IDLE - input released`, repeatable across cycles with clean teardown. Constitution build-step-1 checkpoint cleared.
- [x] T010 Create the state-machine scaffold in `plugins/spread-overview/src/overview.{hpp,cpp}` (`session_state` enum `IDLE/ACTIVATING/ACTIVE/DRAGGING/DEACTIVATING` + the `spread_overview_t` session class) — ✅ compiled. Note: the per-view containers (`ThumbnailRecord`/`WorkspaceCluster`/`DragOperation`) are added when T011 needs them, to avoid dead structs

**Checkpoint**: layout proven by unit tests; plugin toggles + grabs + tears down cleanly on master.

---

## Phase 3: User Story 1 — Spread & switch (Priority: P1) 🎯 MVP

**Goal**: press the key → every window across all workspaces visible, unoverlapped, grouped +
labeled by workspace; click a thumbnail to focus + close; clean restore on exit.

**Independent Test**: `quickstart.md` scenarios 1 (spread + maximized), 2 (click-to-focus), 5 (restore).

- [x] T011 [US1] Implement view enumeration + inputs (in `src/render.cpp::build_spread`): `wset()->get_views(WSET_MAPPED_ONLY|WSET_EXCLUDE_MINIMIZED)`, `get_view_main_workspace`, `get_workspace_grid_size` → build the `layout_input_view` list (enum index = layout id) and call `layout()` — ✅ compiled in-tree; **spread visible pending restart**
- [x] T012 [US1] **[Known-Open R10 — INSPECT]** — ✅ **RESOLVED**: `view_2d_transformer_t::alpha` **MULTIPLIES** the view's own opacity (`view-transform.hpp:350`, header says so verbatim). Naive transformer-alpha=1.0 would leave thumbnails dimmed under the inactive-alpha daemon → T015 must set the view's **own** alpha. Recorded in research.md R10; render (T013) scoped to geometry so no rework
- [x] T013 [US1] Implement the render wrapper **geometry** in `src/render.cpp`: `view_2d_transformer_t` per view; `scale = target_w/geom_w`, `translation = target_center − view_center` (global coords auto-handle the workspace offset, per scale's math); `rem_transformer` on teardown. Alpha out of scope (T015) — ✅ compiled in-tree; **spread visible pending restart**
- [x] T014 [US1] Restore pre-session state exactly at DEACTIVATING and on `cancel` (Principle V, FR-012) — ✅ compiled. **alpha** captured/restored (T015); **geometry/transform** restored by removing the spread transformer; **z-order preserved by construction** (per-view transformers don't touch scene stacking — nothing reorders except the intentional click-raise); **`cancel`** wired (`grab_interface.cancel → deactivate → clear_spread`). Live-verify restore incl. forced cancel (screen lock)
- [x] T015 [US1] **[R10 — IMPLEMENT]** — ✅ compiled. T012 finding = **multiply**, so the fallback path: at ACTIVATING read each view's `"alpha"`-named transformer (the daemon's own-alpha channel), capture its value, set it to 1.0 (opaque); at DEACTIVATING restore exactly (`render.cpp`, FR-014). Flips plan row-V `PASS*` → `PASS` **on live confirmation** of daemon coexistence
- [~] T016 [US1] Overlay: ✅ **labels** (`simple_text_node_t` per cluster) + ✅ **workspace borders** (`border_node_t` in `overlay.{hpp,cpp}` — cairo stroke around each **cluster region** = expo-style grid separating the workspaces, the same regions `hit_test_cluster()` uses; `border_size`/`border_color` options; **highlight-as-parameter hook** `set_highlight(cluster)` built for US2/T021 drop-target, dormant now). Both in the OVERLAY layer, torn down identically in `clear_spread`. **Deferred:** **background dim** (below-thumbnails node) + animations (T025). Compiled; live-pending restart
- [x] T017 [US1] **[Known-Open R9]** — ✅ **RESOLVED by code evidence** (research.md R9): `get_views(WSET_MAPPED_ONLY)` includes fullscreen toplevels (fullscreen is a toplevel state not a layer; core iterates fullscreen views out of `get_views` at `workspace-impl.cpp:233`). No force-composition fallback needed. Live-confirm the fullscreen thumbnail renders
- [x] T018 [US1] Click-to-focus in `src/input.cpp` (build step 3): release < `drag_threshold` over a thumbnail → `focus_raise_view(target, **true**)` + close; never relocates (FR-005/FR-008). Structured so the T020 drag split is additive. **Bugfix**: hit-test was always correct (log: every click `HIT`); the cross-workspace failure was `focus_raise_view` defaulting `allow_switch_ws=false` → clicking a window on another workspace couldn't focus it. Now passes `true` to switch to the view's workspace
- [x] T019 [US1] Wire config via `wf::option_wrapper_t`: `spacing`/`cluster_gap` → `current_layout_options()`; `show_ws_labels` → labels; `close_on_bg_click` → input; `drag_threshold` (already). `duration`/`background` are read when the animation (T025) + dim land — ✅ compiled

**Checkpoint**: US1 fully functional and demoable — a working all-workspace overview + switcher.

---

## Phase 4: User Story 2 — Drag a window to another workspace (Priority: P2)

**Goal**: drag a thumbnail onto another workspace's cluster and drop → the window really moves there
and persists.

**Independent Test**: `quickstart.md` scenario 3 (drag from ws1 to ws4, verify persisted).

- [ ] T020 [US2] Self-managed drag in `plugins/spread-overview/src/input.cpp` (ADR-001, build step 4): button-down → `MAYBE_DRAG`; motion ≥ `drag_threshold` → `DRAGGING`; each motion updates the dragged thumbnail's `view_2d_transformer_t` translation to follow the cursor; snap back on same-cluster/gap release
- [ ] T021 [US2] **Ordinary relocate (happy path)**: on release call `layout::hit_test_cluster()` → target ws; if `target ≠ source`, `plugins/spread-overview/src/move.cpp` calls `wset()->move_to_workspace(view, target_ws)`. Verify a **non-fullscreen** window persists on the target after the overview closes (build step 5, R4). Fullscreen is explicitly out of scope here → T022
- [ ] T022 [US2] **Fullscreen relocate (separate edge checkpoint)** in `plugins/spread-overview/src/move.cpp`: for a fullscreen view, after the move re-issue `fullscreen_request(view, output, true, target_ws)`; handle the move/fullscreen ordering so the view lands fullscreen on the **correct** target ws. Verify fullscreen-relocate **independently** of T021 (R5)
- [ ] T023 [US2] Highlight the cluster region under the cursor as the drop target during DRAGGING in `plugins/spread-overview/src/render.cpp` (FR-007)
- [ ] T024 [US2] Wire `drag_threshold` option and harden click-vs-drag disambiguation so a gesture is never both (FR-005/FR-006) in `plugins/spread-overview/src/input.cpp`

**Checkpoint**: US1 + US2 both work independently — the headline drag-to-workspace is live.

---

## Phase 5: User Story 3 — Reorganize several in one session (Priority: P3)

**Goal**: after a relocate the overview stays open and reflows, so several windows can be moved in
one pass.

**Independent Test**: `quickstart.md` scenario 4 (move A, overview stays open + A under new cluster,
move B, both persist).

- [ ] T025 [US3] On a successful relocate, transition `DRAGGING → ACTIVE` (stay open), re-run `layout()`, and animate the moved thumbnail into its new cluster in `plugins/spread-overview/src/overview.cpp` (FR-010, clarified 2026-07-11)
- [ ] T026 [US3] Verify reflow stability across repeated relocations in one session (no residual transform, no stale cluster membership) in `plugins/spread-overview/src/overview.cpp`

**Checkpoint**: all three stories independently functional.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T027 [P] Entry / exit / reflow animations driven by `duration` in `plugins/spread-overview/src/render.cpp` (60 fps target, SC-001)
- [ ] T028 [P] **[Principle VI]** View-lifetime safety in `plugins/spread-overview/src/overview.cpp`: on a mid-session view unmap, remove its `ThumbnailRecord` and reflow; if it was the drag subject, end the drag as a snap-back — no crash, no ghost (FR-013, SC-007)
- [ ] T029 [P] Edge cases in `plugins/spread-overview/src/{overview,render}.cpp`: empty-workspace cluster stays a valid drop target; single-window session; surface the `over_dense` flag in the UI (data-model)
- [ ] T030 [P] **[Known-Open include_minimized]** Keep `include_minimized` parsed-but-no-op and document it as stubbed in `metadata/spread-overview.xml` (long description) until the `natural_size` source is resolved (research.md R13)
- [ ] T031 Final exit-path pass in `plugins/spread-overview/src/input.cpp`: `close_on_bg_click`, Esc, and re-trigger all fully restore state (FR-011)
- [ ] T032 Run the full `quickstart.md` validation (all 7 scenarios) on master and confirm SC-001…SC-008 (fullscreen scenario exercises T022; alpha uses T015; fullscreen-enum uses T017); record results
- [ ] T033 [P] Update `docs/` (plugin README / notes) if the implementation surfaced anything worth recording

---

## Dependencies & Execution Order

### Phase dependencies
- **Setup (P1)**: no deps — start immediately.
- **Foundational (P2)**: after Setup — **blocks all user stories**. Within it: T005→T006→T007→T008
  (layout is a strict TDD chain); T009, T010 can proceed in parallel with the layout chain.
- **US1 (P3)**: after Foundational. This is the MVP.
- **US2 (P4)**: after US1 (drag needs rendered thumbnails to grab). Depends on US1's render (T013) +
  the pure hit-test (T007).
- **US3 (P5)**: after US2 (reflow-after-relocate needs the relocate path T021).
- **Polish (P6)**: after the desired stories.

### Critical intra-US1 ordering (risk isolation)
- **T012 (R10 inspect) GATES T013 (render geometry) and T015 (alpha implement)** — decide
  replace-vs-multiply *before* writing render/alpha, so it is coded once. T013 is scoped to geometry
  only, so even if the inspect lands late it never forces a render rework.
- **T021 (ordinary relocate) precedes T022 (fullscreen relocate)** — the happy path lands and is
  verified persisted before the fullscreen edge (re-fullscreen on target ws) is added, so a
  fullscreen-relocate bug is isolable from ordinary relocate.

### Story independence note
Unlike a typical web feature, the stories are **inherently layered** (you cannot drag thumbnails that
aren't rendered), so US2/US3 build on US1 rather than being fully parallel. Each still ends in an
independently demoable increment.

### Parallel opportunities
- Setup: T003 ∥ (T001→T002).
- Foundational: the layout chain (T005→T006→T007→T008) runs ∥ the lifecycle skeleton (T009, T010).
- US1: **T017 (R9) runs ∥ T013–T016**. **T012 (R10 inspect) does NOT run parallel to render — it
  gates it.** T011 (enumeration) is independent and can precede/parallel T012.
- Polish: T027, T028, T029, T030, T033 are all `[P]`.

---

## Parallel Example: Foundational phase

```text
# The pure-core TDD chain and the lifecycle skeleton touch different files — run concurrently:
Track A (pure core):     T005 → T006 (failing tests) → T007 (make green) → T008 (wire test build)
Track B (compositor):    T009 (grab lifecycle)  ∥  T010 (state-machine scaffold)
# Both must finish before US1 (T011) starts.
```

---

## Implementation Strategy

### MVP first (US1 only)
1. Phase 1 Setup → plugin builds + loads on master.
2. Phase 2 Foundational → layout unit-tested green; toggle+grab+teardown proven.
3. Phase 3 US1 → **STOP and validate** quickstart scenarios 1/2/5. This alone is a usable
   all-workspace overview + switcher (ships value with zero drag code).

### Incremental delivery
- US1 (MVP) → US2 (drag-to-relocate, the headline) → US3 (multi-move) → Polish. Each increment builds,
  runs on master, and is revertable (Principle III per-increment gate).

### Known-opens to close on the way
- **T012 (R10 inspect) → T015 (R10 implement)** before claiming Principle V's alpha guarantee
  (flips row-V `PASS*` → `PASS`).
- **T017 (R9)** before claiming the fullscreen-visible scenario.
- **T022** verifies fullscreen-relocate as its own checkpoint (kept out of T021's happy path).
- **T030 (include_minimized)** stays stubbed until its data source is specified.

---

## Notes
- `[P]` = different files, no incomplete deps. `[US#]` = traceability to the spec's user stories.
- Commit after each task or logical group; every increment must build + run on
  `/usr/local/bin/wayfire` before moving on.
- The pure `layout()` is the only unit-tested module (Principle I); everything else is verified on the
  running compositor (Principle III). Do not add compositor mocks.
- No `core_drag_t`, no out-of-tree build, no `/usr/include` API citations (constitution IV, VII, VIII).
