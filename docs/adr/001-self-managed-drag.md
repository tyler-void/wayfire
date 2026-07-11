# ADR-001: v1 uses a self-managed per-frame drag transform, not `core_drag_t`

**Status:** Accepted
**Date:** 2026-07-11
**Feature:** 001-spread-overview
**Amends:** the "drag engine is largely a library call" finding in `docs/API-MAP-verified.md` §1(2)

## Context

Two recon findings were used to choose SPEC B (standalone plugin) over SPEC A (fork scale) and to
claim "SPEC B is cheaper than its own spec feared":

1. scale's all-workspace layout is **flat-packed** (no per-workspace regions) — this kills SPEC A's
   cheap path and stands unchanged.
2. `wf::move_drag::core_drag_t` (`plugins/common/move-drag-interface`) already implements
   drag + relocate, so the "hard 20%" is **"largely a library call."**

This ADR revisits finding (2). During the deep read, `core_drag_t` turned out to model a different
interaction than an overview drag:

- **Wrong drop model.** Its drop→workspace resolution (`adjust_view_on_output`) computes the target
  workspace from *physical output-edge geometry* (`grab.x / output_geometry.width`) — it assumes a
  real window dragged toward a screen edge while the workspace scrolls. In an overview **every
  workspace is on screen at once**, so "which workspace" is a *layout-cluster* question, not an
  edge question. `adjust_view_on_output` is unusable for our drop decision.
- **Wrong subject.** `core_drag_t` moves the *real view* through real output space (wobbly, snap-off,
  cross-output handoff). In the overview the draggable is a *scaled thumbnail* we already own via a
  scene transform; we do not want the real view moving under the grab.
- **Signature drift.** `handle_motion` is `wf::point_t` on 0.10.1 but `wf::pointf_t` on master
  (`move-drag-interface.hpp:198`). Binding to this API couples us to a surface that already changed.

## Decision

For **v1**, the overview implements its **own drag**. The dragged thumbnail is the *same*
`wf::scene::view_2d_transformer_t` used for the spread; on each pointer-motion event the plugin
updates that transformer's `translation_{x,y}` so the thumbnail follows the cursor. On release, the
plugin runs its **own layout-cluster hit-test** and, if the target workspace differs from the
source, calls `wf::workspace_set_t::move_to_workspace(view, target_ws)`.

`core_drag_t` is **not used in v1**.

## Consequences

**Honest cost:** the "drag is largely a library call" saving from the API-MAP **no longer applies.**
v1 must implement drag-follow itself: a per-frame transform translate, the click-vs-drag threshold,
and snap-back. This is a modest amount of code, but it is *our* code, not reused library code. The
net "SPEC B is cheaper" claim now rests on finding (1) (owning a clustered layout) plus the fact
that relocation is still a single call (`move_to_workspace`), **not** on reusing `core_drag_t`.

**Upsides:**
- The drop→workspace mapping is defined by *our* layout geometry — the only correct model for an
  all-on-one-screen overview — and shares geometry with the pure layout function, satisfying
  constitution Principle I ("hit-test and layout can never disagree").
- No coupling to `core_drag_t`'s changing signature or its edge-snap semantics.
- Real views never move until the final `move_to_workspace` on drop → simpler lifetime and restore.

**Revisit if:** we later want wobbly physics or cross-output drag (v2+, multi-output). At that point
`core_drag_t` (or parts of it) may be reintroduced behind the `move` wrapper.

## Affected artifacts
- `docs/API-MAP-verified.md` — §1 finding (2) downgraded; drag-engine section and build-up step 5
  rewritten to the self-managed model.
- `plan.md` / `tasks.md` (when generated) MUST describe the self-managed drag, not `core_drag_t`.
