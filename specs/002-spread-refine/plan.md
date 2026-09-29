# Implementation Plan: Spread Overview Refinement — Legible Thumbnails & Motion Continuity

**Branch**: `002-spread-refine` | **Date**: 2026-09-29 | **Spec**: [spec.md](./spec.md)
**Input**: `specs/002-spread-refine/spec.md` + [design-brief.md](./design-brief.md) (NORMATIVE where
marked) + the executable oracle [`reference/layout_ref.py`](./reference/layout_ref.py)

## Summary

Three refinements to the shipped `spread-overview` plugin, on top of fork `master` @ `8b519e0b`:

1. **P1 — within-workspace layout.** Replace 001's "equal slots, one uniform scale per workspace"
   with a port of GNOME Shell's `UnalignedLayoutStrategy`: each window is sized individually, windows
   are packed into rows of variable width ordered by their real positions, the row count is chosen by
   GNOME's scale/space objective, and short windows get a configurable emphasis
   (`small_window_boost`, default 1.5). On the reference scene the smallest thumbnail grows from
   64×48 to 157×116 (area 3.1×) with no window shrinking. Recorded as
   [ADR-004](../../docs/adr/004-per-window-cluster-scale.md).
2. **P2 — no-jump motion.** A press during the opening/reflow animation freezes only the pressed
   thumbnail (erase its clock; the transformer already holds the drawn values) instead of snapping
   every thumbnail to its slot, and hit-testing/drop resolution move from slot rects to **live**
   rendered rects. A cancelled drop animates back to the layout slot instead of teleporting.
3. **P3 — close feel.** A separate `exit_duration` (default 225 ms) for the thumbnail settle/fade and
   the overlay dissolve, plus a ≈120 ms fade on the drop-target highlight (deferrable, SHOULD).

The pure layout core stays pure and is proven against an executable Python oracle: 54 golden
rectangles within 0.5 px, plus 18,009 fuzzed layouts with zero violations.

## Technical Context

**Language/Version**: C++17 (`meson.build:9` `cpp_std=c++17`)
**Primary Dependencies**: Wayfire **0.12.0-dev** (`meson.build:5`), plugin ABI
`WAYFIRE_API_ABI_VERSION_MACRO = 2026'08'01` (`src/api/wayfire/plugin.hpp:110`); wlroots submodule at
tag **0.20.2**; wf-config required `>=0.12.0,<0.13.0` (`meson.build:111`), submodule declares
**0.12.0** (`subprojects/wf-config/meson.build:4`). Feature-specific API surface: scene transforms
(`wf::scene::view_2d_transformer_t`), `wf::animation::duration_t` /
`simple_animation_t` from an `animation_description_t` option
(`subprojects/wf-config/include/wayfire/util/duration.hpp:81,188`), `wf::option_wrapper_t`,
`wf::owned_texture_t` (highlight node). **No new dependency, no new core header touched.**
**Storage**: N/A — transient session state only.
**Testing**: doctest, same binary `Spread overview layout test`
(`test/plugins/meson.build:12`), `-Dtests=enabled`; plus the reference oracle
(`reference/layout_ref.py`) offline, and tty2 sessions for the interactive gates.
**Target Platform**: self-built Wayfire at `/usr/local` on tty1 (daily) / tty2 (test); Arch 0.10.1 at
`/usr/bin/wayfire` untouched as fallback.
**Project Type**: in-tree Wayfire plugin (`shared_module` under `plugins/spread-overview/`).
**Performance Goals**: 60 fps entry/drag/reflow unchanged; layout is at most `n` candidate row counts
× `O(n log n)` per cluster — negligible at "a few dozen windows"; layout runs on open/reflow, never
per frame.
**Constraints**: zero overlap and in-region containment always win over emphasis; every
`target_rect` strictly positive (D4); no change to cluster region geometry (001 invariant #9); exact
restore on close (Principle V); view-lifetime safety across a new animation path (Principle VI).
**Scale/Scope**: single output, ≤ a few dozen windows, grid typically 3×3.

## Constitution Check

*GATE: must pass before Phase 0; re-checked after Phase 1 (below).* Constitution v1.2.0 →
**v1.3.0 (MINOR) required by this feature** — see row I and VIII.

| # | Principle | Status | How the design satisfies it |
|---|---|---|---|
| I | Pure, testable layout core | **PASS after amendment** | The new packing is entirely inside `layout.cpp`, still pure, still plain value types, still owns `hit_test_cluster`, tests expanded (contract §4). **But** the principle's signature-contract text (`constitution.md:55`) enumerates the input as `{view_id, source_ws, natural_size}`; 002 adds `natural_pos` + an option. Amendment 1.3.0 is a **task in I1**, before the code lands. |
| II | Thin API-contact wrappers | PASS | Layout change is pure-core only. Plugin contact stays in `render.cpp` (options, `natural_pos`, animation clocks), `input.cpp` (press/drag/live-rect helper), `overlay.{hpp,cpp}` (highlight node). No compositor call moves into `layout.cpp`. |
| III | Incremental, verifiable build-up | PASS | Six increments I1–I6 (below); each builds, installs and has a tty2 gate or a test gate; I1 touches no plugin code at all, so P1 is provable before anything can regress the desktop. |
| IV | Verify-before-build (master is the only signature authority) | PASS | Every file:line the brief relies on was re-verified in this checkout (table below); the two animation signatures are cited from `subprojects/wf-config/.../duration.hpp`, not from `/usr/include`. New entries go into `docs/API-MAP-verified.md` in I1/I2. |
| V | Non-regression & coexistence | PASS | No change to capture/restore; `inactive-alpha` coexistence untouched. The one risk — a snap-back clock driving `tr->alpha` to 0 — is pinned by `alpha.set(1.0, 1.0)` (R-108) and gated by I3. 001's seven scenarios are re-run whole at I6 (SC-007). |
| VI | View-lifetime safety | PASS | The new snap-back clock lives in the existing `anim_state` map, so the existing `forget_view()` scrub covers it; I3's gate explicitly closes a window mid-drag and mid-snap-back. |
| VII | ADR discipline | PASS | ADR-004 ships with the pack and records the reversal of R14's second half, including the honest cost (R-105, "not never-worse-than-001"). Its affected-artifacts list is updated to what was built (I6). |
| VIII | Locked build target & fallback | **PASS after amendment** | In-tree, `/usr/local`, Arch fallback untouched; no core header changed so the ABI stamp stays `20260801` and plugins-extra needs no rebuild (verified by the stamp check, not assumed). **But** the principle's stated facts are stale (0.11-dev, ABI `2026'07'09`, wlroots 0.20.1, wf-config 0.11.0) — corrected by the same 1.3.0 amendment, each fact re-verified in the tree (Technical Context above). |

**Technology constraints**: single output (unchanged), native C++ (unchanged), standalone plugin
(unchanged), option-gated new behaviour with both options declared in `metadata/spread-overview.xml`
(Upstreamability). All honored.

**Gate result: PASS, conditional on the v1.3.0 amendment landing in I1** (a docs-only change, no
code risk). Complexity Tracking is empty — the one substantial complexity (a row-search layout
instead of equal slots) is the feature itself, justified by ADR-004 and contained in a pure,
golden-tested function.

### Principle IV verification (re-read in this checkout at `8b519e0b`)

| Claim | Location | Result |
|---|---|---|
| 001 packing: equal slots + one cluster scale | `layout.cpp:34-80` (cols `:46`, single scale `:56-62`) | verified |
| Layout option assembly; `layout_options` default-constructed | `render.cpp:34-41` (`:36`) | verified — only `spacing`/`cluster_gap` set; `outer_margin` **hardcoded 0.0** at `:39`; `max_scale`/`min_scale` come from `layout.hpp:31-32` |
| `build_spread` input vector; id = enumeration index | `render.cpp:43-66`, id at `:59` | verified |
| Slot → scale/translation | `render.cpp:105`, `:109-112` | verified |
| Entry animation pins alpha | `render.cpp:152` | verified |
| `anim_hook` install pattern | `render.cpp:245-255`, again `:442-450` | verified — **two** existing copies; D-1 adds a third → factor into one helper |
| `animate_step` writes transforms + alpha per frame | `render.cpp:336-390`, alpha `:353` | verified |
| `start_exit_anim` clock source | `render.cpp:395`, `:412` | verified |
| `finalize_entry_anim` | def `render.cpp:456-482`, decl `overview.hpp:219`, **one** call site `input.cpp:173` | verified — safe to delete |
| `snapshot_thumb_screen_rects` live-rect math | `render.cpp:488-505` | verified — the helper to factor out |
| `thumb_at` hit-tests slot rects | `input.cpp:22-34` | verified |
| `dragged_thumb_center` = slot centre + cursor delta | `input.cpp:36-50` | verified |
| Snap-back is an instant assignment | `input.cpp:269-277` | verified |
| Workspace-local basis for `natural_pos` | `move.cpp:86` (`:81`, `:78-80`) | verified |
| Plugin option block | `overview.hpp:123-134` | **brief said `:122-126`** — the block actually ends at `:134`; new options append after it |
| `overlay_fade{opt_duration}`, exit-only | `overview.hpp:161`, comment `:160` | verified |
| Highlight baked into the shared border texture | `overlay.hpp:71`, `:85`; `overlay.cpp:25-34` (`set_highlight`), baking in `rerender()` `:36-81` (highlight `:69-73`, single upload `:76`) | verified |
| `duration`'s metadata text; spacing/cluster_gap defaults | `metadata/spread-overview.xml:16`, `:29-40` | verified |
| API map entries to extend | `docs/API-MAP-verified.md:100`, `:139` | verified |
| Constitution signature contract; stale facts | `constitution.md:55`; `:138-140`, `:144-146`, `:149`, `:154`, `:163`, `:167-168`, `:173`, `:175` | verified — the stale-fact set is **wider** than the brief's `:167-168` |
| ABI stamp; wf-config requirement | `src/api/wayfire/plugin.hpp:110`; `meson.build:111` | verified |
| Animation from an option | `subprojects/wf-config/include/wayfire/util/duration.hpp:81` (`duration_t`), `:188` (`simple_animation_t`) | verified — new API-map entries |
| Stacking order for the hit-test | `src/api/wayfire/workspace-set.hpp:40-43` (`WSET_SORT_STACKING`, "may be slow"), sort `src/output/workspace-impl.cpp:450-465` (ascending child index), top end `src/api/wayfire/scene-operations.hpp:35-41`, `:63-79` (`add_front`/`raise_to_front` insert at `begin()`) | verified — **index 0 = top-most**; new API-map entry (R-115) |
| Existing test helpers/cases | `test/plugins/spread-overview-layout.cpp:12-16` (`overlaps`, strict), over-dense `:127-144`, 8 cases | verified |

## Project Structure

### Documentation (this feature)

```text
specs/002-spread-refine/
├── spec.md              # input (final; not regenerated)
├── design-brief.md      # input, NORMATIVE where marked
├── plan.md              # this file
├── research.md          # Phase 0 — R-101…R-114
├── data-model.md        # Phase 1 — delta over 001
├── quickstart.md        # Phase 1 — per-increment tty2 gates
├── contracts/layout.md  # Phase 1 — NORMATIVE algorithm + invariant delta
├── reference/           # oracle: layout_ref.py, golden-fixture.json, layout-compare.png
└── tasks.md             # Phase 2 (/speckit-tasks)
```

### Source code (repository root)

```text
plugins/spread-overview/src/
├── layout.hpp      # + pointf, natural_pos, small_window_boost            [I1]
├── layout.cpp      # + row layout (replaces pack_cluster's sizing)        [I1]
├── render.cpp      # natural_pos input, options, snap-back clock,
│                   #   exit_duration, anim-hook helper, live-rect helper  [I2,I3,I4]
├── input.cpp       # press-freeze, live-rect hit-test/centre, snap-back   [I3]
├── overview.hpp    # + opt_small_window_boost, opt_exit_duration,
│                   #   - finalize_entry_anim decl                         [I2,I3,I4]
└── overlay.{hpp,cpp} # highlight as its own element + fade                [I5]

metadata/spread-overview.xml          # + small_window_boost, exit_duration; duration text  [I2,I4]
test/plugins/spread-overview-layout.cpp  # golden parity, 001 oracle, D4, property tests    [I1]
docs/API-MAP-verified.md              # natural_pos input; duration_t/simple_animation_t     [I1,I4]
docs/adr/004-per-window-cluster-scale.md # affected-artifacts list finalized                 [I6]
.specify/memory/constitution.md       # v1.3.0 MINOR amendment                               [I1]
FEATURES.md                           # refinement entry                                     [I6]
plugins/spread-overview/README.md     # both new options + row-layout note                   [I6]
```

**Structure Decision**: no new module. The feature is a rewrite of one pure function plus four thin
wrapper edits, which is what keeps Principle II intact.

## Increments (Principle III)

| # | Scope | Gate |
|---|---|---|
| I1 | Constitution 1.3.0; types (`pointf`, `natural_pos`, `small_window_boost`); row layout in `layout.cpp`; all new tests; API-map input line | layout test + **full suite** green; golden parity ≤ 0.5 px; no plugin behaviour change yet |
| I2 | `render.cpp` fills `natural_pos`; both options read + declared in metadata | build + install + ABI stamp; tty2 gate (quickstart I2) |
| I3 | Press-freeze, live-rect helper, top-most-first hit-test (R-115), delete `finalize_entry_anim`, animated snap-back **and frozen-release return** (one shared helper), anim-hook helper | build + install + stamp; tty2 gate (quickstart I3, 7 steps) |
| I4 | `exit_duration` for thumbnails + overlay dissolve; metadata text | build + install + stamp; tty2 gate (quickstart I4) |
| I5 | Highlight as its own element + ≈120 ms fade, **or** a recorded deferral | build + install + stamp; tty2 gate (quickstart I5) |
| I6 | 001 quickstart re-run (SC-007); ADR-004 affected artifacts; FEATURES.md | user sign-off; only then merge/push (user decides) |

I3 keeps the two motion changes together on purpose: once a press can freeze a mid-flight thumbnail,
the snap-back must target the **slot** rather than the grab position, so splitting them would ship a
knowingly wrong intermediate state.

## Complexity Tracking

*No constitution violations to justify.* The amendment in row I/VIII is a governance update, not a
waiver: the design does not deviate from any principle, the principle text is being brought up to
date with facts verified in this tree.

## Known opens (resolved at implementation, tracked in tasks)

1. **`natural_pos` for exotic view classes** (R-106, INFERRED): the workspace-local basis is assumed
   correct for maximized/fullscreen/straddling views. Ordering only needs within-workspace
   consistency, so the blast radius is "a thumbnail sorts a row earlier/later". Confirmed at the I2
   gate.
2. **Test-only 001 oracle naming**: the test TU `#include`s `layout.cpp` directly
   (`test/plugins/spread-overview-layout.cpp:8`), so a copy of 001's packer **must not** reuse the
   name `pack_cluster` or it will silently resolve to the new implementation. Name it
   `pack_cluster_001_oracle`.
3. **The existing over-dense test is arithmetic-sensitive**: its 0.0389 min-scale depends on the test's
   `opts()` using `outer_margin = 20.0`. If the new sizing changes the derived area, that assertion
   must be re-derived rather than relaxed.
4. **SC-005 needs evidence**: the existing `LOGI("spread-overview: drag start")` carries no
   coordinates. I3 extends that one line with press position, the thumbnail's position at press and
   the pointer displacement, so the 1-px check is readable from `/tmp/master-log`.
5. **FR-011 may be deferred** (SHOULD, P3) if the highlight cannot be split out of `border_node_t`
   with a contained change; the reason is recorded in `tasks.md`.
