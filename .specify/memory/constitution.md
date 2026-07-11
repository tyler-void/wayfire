<!--
SYNC IMPACT REPORT
==================
Version change: 1.0.0 → 1.1.0 → 1.1.1
Rationale: 1.1.0 MINOR — two principles added (VII, VIII) and Principle IV materially expanded to
encode the three recon-surfaced constraints as first-class, inheritable rules. 1.1.1 PATCH —
corrected the wlroots/wf-config dependency wording (they are meson **subprojects** selected via
`use_system_wlroots=auto`, built as shared libs installed to `/usr/local/lib` and linked
dynamically — "vendored" was imprecise; ldconfig presence is not system-linking) and struck the
stale "master vs wlroots 0.19.3" framing. No change in obligations.

Principles (8):
  I.    Pure, Testable Layout Core
  II.   Thin API-Contact Wrappers
  III.  Incremental, Independently-Verifiable Build-Up
  IV.   Verify-Before-Build — Master Source Is the Sole Signature Authority   [EXPANDED in 1.1.0]
  V.    Non-Regression & Coexistence
  VI.   View-Lifetime Safety
  VII.  Architecture Decisions Are Recorded (ADR Discipline)                  [NEW in 1.1.0]
  VIII. Locked Build Target & Preserved Fallback                             [NEW in 1.1.0]

Sections:
  - Technology Constraints & Scope   [EXPANDED: build target + fallback + ABI macro]
  - Development Workflow & Quality Gates
  - Governance

Templates reviewed for consistency:
  ✅ .specify/templates/plan-template.md   — "Constitution Check" derived dynamically; the plan MUST
       now also assert ADR compliance (VII) and build-target/ABI (VIII).
  ✅ .specify/templates/spec-template.md   — unaffected; specs stay implementation-agnostic.
  ✅ .specify/templates/tasks-template.md   — unaffected; tasks inherit VII/VIII via the plan.

Referenced artifacts:
  - docs/adr/001-self-managed-drag.md      (Principle VII / the drag pivot)
  - docs/API-MAP-verified.md               (Principle IV — re-verified against master, no /usr/include)

Deferred / TODO: none.
-->

# Spread Overview Constitution

Governs the `spread-overview` Wayfire plugin: an all-workspace window overview that spreads
every window (unoverlapped, including maximized) and relocates a window to another workspace
when the user drags and drops its thumbnail onto that workspace's region.

## Core Principles

### I. Pure, Testable Layout Core

The window packing and per-workspace clustering MUST be implemented as a pure function with
zero dependencies on Wayfire rendering, input, or scene-graph types.

- Signature contract: input = list of `{view_id, source_ws (x,y), natural_size}` + workspace
  grid dimensions + output logical size + spacing/gap options; output = map
  `view_id → {target_rect, cluster_id}`.
- This function MUST be unit-testable in isolation and MUST carry unit tests, because it is the
  only component with genuine design decisions (how to pack windows without overlap, how to map
  a drop coordinate back to a target workspace).
- The inverse mapping (drop coordinate → target workspace cluster) MUST live in or beside this
  pure module and share its geometry, so hit-testing and layout can never disagree.

Rationale: isolating the one algorithmic piece from hard-to-test glue lets the packing algorithm
iterate fast and be proven correct without a running compositor.

### II. Thin API-Contact Wrappers

All contact with Wayfire APIs (rendering/scene transforms, input grab, view relocation) MUST be
confined to thin, single-purpose wrapper modules (e.g. `render`, `input`, `move`). Core state and
layout logic MUST NOT call compositor APIs directly.

Rationale: Wayfire's scene/render/input layer is the top maintenance risk and churns across
versions. When the API moves, only the wrappers change — logic and layout stay untouched.

### III. Incremental, Independently-Verifiable Build-Up

The plugin MUST be built in ordered increments, each of which compiles, runs, and is revertable on
its own. The canonical order is: (1) skeleton + input grab + clean teardown (zero rendering) →
(2) static spread render → (3) click-to-focus (scale parity) → (4) drag + snap-back →
(5) drop-to-relocate → (6) polish (labels, highlight, animation, alpha coexistence, edge cases).
Steps MUST NOT be skipped or merged to reach the payoff faster.

Rationale: each increment is a checkpoint that isolates failures and preserves a working fallback.

### IV. Verify-Before-Build — Master Source Is the Sole Signature Authority

No plugin logic may be written against an assumed API, and **the only authority for API signatures
is this checkout's master source tree** — `src/api/wayfire/**` and
`plugins/common/wayfire/plugins/common/**`, plus in-tree plugin sources.

- Citations to the installed Arch headers (`/usr/include/wayfire`, the 0.10.1 ABI) are **forbidden**
  in design artifacts: those describe a different ABI than the one being built against.
- Every signature the plugin depends on MUST be confirmed against master and recorded in
  `docs/API-MAP-verified.md` with a `file:line` that resolves inside this tree.
- Any 0.10.1↔master signature difference discovered MUST be listed explicitly in the API map (the
  living delta table), so no stale assumption survives.

Rationale: guessing at compositor APIs — or trusting headers from a version you are not building
against — produces code that compiles against imagination and fails against reality.

### V. Non-Regression & Coexistence

The plugin MUST NOT alter or break the behavior of any existing plugin, and MUST coexist with the
user's `inactive-alpha` pywayfire daemon. It MUST capture each affected view's pre-session state
(alpha, transform, geometry, z-order) at activation and restore it exactly at deactivation —
including for a view that was relocated during the session. Forced deactivation (`cancel`) MUST
also fully restore state.

Rationale: an overview is a transient lens over the desktop; leaving any residue behind is a defect.

### VI. View-Lifetime Safety

Every per-view operation MUST guard against a client destroying its view mid-session. Views MUST be
held via a handle whose validity is checked (mapped/valid) immediately before each use. A view
disappearing during activation, drag, drop, or teardown MUST NOT crash the compositor or leave a
dangling thumbnail.

Rationale: clients close windows at arbitrary times; the overview runs with an input grab and cannot
afford undefined behavior.

### VII. Architecture Decisions Are Recorded (ADR Discipline)

Any decision that reverses or materially changes a recon-justified architecture choice MUST be
written up as an Architecture Decision Record under `docs/adr/`, not buried in a reasoning trace or
a code comment. The ADR MUST state what changed, why, and the honest cost. Specs, plans, and tasks
MUST describe the architecture that will **actually be built**, matching the accepted ADRs.

- **v1 drag is self-managed** (`docs/adr/001-self-managed-drag.md`): the dragged thumbnail is the
  same `view_2d_transformer_t` used for the spread, moved by per-frame translation; `core_drag_t`
  is NOT used in v1. Its "reuse the drag library" saving is explicitly retired.

Rationale: the two recon pillars that chose SPEC B were load-bearing; silently dropping one of them
without a record makes the design untrustworthy. ADRs keep the paper trail honest and reviewable.

### VIII. Locked Build Target & Preserved Fallback

The plugin is built **in-tree on Wayfire master (0.11-dev, wlroots 0.20.x)**, installed to the
`/usr/local` prefix, and inherits the master ABI version (`WAYFIRE_API_ABI_VERSION_MACRO =
2026'07'09`). Consequences that MUST be honored:

- The in-tree plugin loads **only** into the self-built master compositor at
  `/usr/local/bin/wayfire`. It will be **rejected** by the Arch stable build
  (`/usr/bin/wayfire`, 0.10.1, ABI `2025'08'22`). Therefore the daily-driver compositor is
  master (0.11-dev).
- The Arch `wayfire` package (0.10.1) MUST be **kept installed and untouched** as a TTY fallback.
  **Do NOT purge it.** If master fails to start, `/usr/bin/wayfire` remains bootable.
- Master MUST be confirmed to build **and boot** on the target machine before relying on it (done:
  headless smoke test on 2026-07-11, commit `8603d187`).

Rationale: the ABI macro is the concrete gate that makes "in-tree on master" and "daily driver
becomes master" the same decision; preserving the 0.10.1 fallback keeps that decision reversible.

## Technology Constraints & Scope

- **Target compositor**: Wayfire **master (0.11-dev)**, built from this checkout, installed to
  `/usr/local`. **wlroots 0.20.1** and **wf-config 0.11.0** are provided by meson **subprojects**
  (git submodules under `subprojects/`), selected via `use_system_wlroots=auto` because no matching
  system package is present; they build as **shared** libraries installed to `/usr/local/lib` and
  are linked **dynamically** (their appearance in `ldd`/`ldconfig` is expected, not system-linking).
  Pin `-Duse_system_wlroots=disabled` to force the subproject deterministically. Master requires
  wlroots **0.20** — 0.19.x is not a build option. Plugin lives **in-tree** under
  `plugins/spread-overview/` and inherits the parent build's dep objects (Principle VIII).
- **Fallback**: the Arch `wayfire` 0.10.1 package stays installed at `/usr/bin/wayfire` as an
  untouched TTY fallback (Principle VIII).
- **Architecture**: standalone plugin (SPEC B), **not** a fork of `scale`. `scale`'s all-workspace
  layout is flat-packed with no per-workspace regions, so drop-to-workspace cannot be added to it
  without invasive layout surgery in a core plugin; `spread-overview` owns a workspace-clustered
  layout instead. Drag is self-managed (Principle VII / ADR-001).
- **Native C++ only**: Wayfire IPC does not expose in-overview drag position events, so a pywayfire
  implementation is a non-solution for the drag interaction.
- **Scope v1**: single output. The layout function MUST take an output parameter so multi-output is a
  later addition, but cross-output drag MUST NOT be implemented in v1. Direct-scanout/fullscreen
  clients MUST be enumerated or have a defined fallback.

## Development Workflow & Quality Gates

- **Spec-driven**: work flows constitution → specify → clarify → plan → tasks → analyze → implement.
  Implementation MUST NOT begin before an approved plan and tasks exist.
- **Constitution Check**: the plan's Constitution Check gate MUST evaluate the design against every
  principle above — including ADR compliance (VII) and the locked build target/ABI (VIII).
  Violations MUST be justified in the plan's Complexity Tracking or the design changed.
- **Per-increment gate**: each increment (Principle III) MUST build and run on the master compositor
  and demonstrate its checkpoint behavior before the next increment starts.
- **Upstreamability**: features SHOULD be option-gated and minimal-surface so the plugin remains a
  candidate for upstream contribution. New options MUST be declared in the plugin's metadata XML.

## Governance

This constitution supersedes ad-hoc practice for the `spread-overview` project. All plans, task
lists, and reviews MUST verify compliance with the principles above.

- **Amendments**: proposed as an edit to this file with a rationale; adopted once recorded here with
  an incremented version and updated amendment date.
- **Versioning policy** (semantic): MAJOR = principle removed or redefined incompatibly; MINOR = a
  principle or section added or materially expanded; PATCH = clarification or wording with no change
  in obligations.
- **Compliance review**: every increment and every spec-kit phase transition is a compliance
  checkpoint. Complexity that violates a principle MUST be justified against a concrete need or
  removed.

**Version**: 1.1.1 | **Ratified**: 2026-07-11 | **Last Amended**: 2026-07-11
