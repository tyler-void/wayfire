# Phase 1 Data Model: Spread Overview

All state is transient and in-memory (no persistence). Entities below describe the *runtime* model
owned by one per-output plugin instance.

## Entities

### OverviewSession
The active state from trigger to dismissal. One per output; exists only while active.
- `state`: enum `{ IDLE, ACTIVATING, ACTIVE, DRAGGING, DEACTIVATING }`
- `thumbnails`: map `view → ThumbnailRecord`
- `clusters`: list of `WorkspaceCluster` (one per grid cell)
- `grid`: `{ width, height }` workspace grid dimensions
- `current_ws`: the workspace that was current at activation (restore focus context)
- `drag`: optional `DragOperation` (present only in DRAGGING)
- Invariant: on leaving ACTIVE/DRAGGING → DEACTIVATING, every `ThumbnailRecord.pre_session` is
  restored exactly (Principle V), including on forced `cancel`.

### ThumbnailRecord (per view)
The scaled stand-in for one real window plus everything needed to restore it.
- `view`: `wayfire_toplevel_view` handle (validity checked before each use — Principle VI)
- `source_ws`: `{x, y}` from `get_view_main_workspace(view)` at capture time
- `pre_session`: captured at ACTIVATING — `{ alpha, transformer_state?, geometry, z_order }`
- `transformer`: the `view_2d_transformer_t` node added for the session (removed at teardown)
- `target_rect`: current on-screen rectangle (from `layout()`), animated toward
- `cluster_id`: which `WorkspaceCluster` this thumbnail belongs to (== source_ws while not dragged)
- `visibility`: enum `{ VISIBLE, HIDING, HIDDEN }` (for animation bookkeeping)
- Lifecycle: created at ACTIVATING or when a view maps mid-session; destroyed on teardown or when
  the view unmaps (drop + reflow).

### WorkspaceCluster
The on-screen region representing one workspace; both a visual group/label and a drop target.
- `ws`: `{x, y}` workspace coordinate
- `region`: rectangle on the output that this cluster occupies — the **fixed-equal grid cell** at
  position `ws` (R14 / layout invariant #9); independent of content density
- `label`: display string (e.g. workspace number/index)
- `over_dense`: bool — set when the cluster's uniform thumbnail scale fell below `min_scale`
  (informational for the UI; never breaks zero-overlap)
- Role: the inverse hit-test maps a drop coordinate → the `ws` of the containing cluster.

### DragOperation
A single press→move→release interaction. Present only during DRAGGING.
- `view`: the dragged `wayfire_toplevel_view`
- `press_origin`: pointer position at button-down
- `phase`: enum `{ MAYBE_DRAG, DRAGGING }` (threshold disambiguation)
- `grab_offset`: pointer-to-thumbnail offset so the thumbnail tracks under the cursor naturally
- `source_cluster`: the cluster the view started in (for same-cluster snap-back)
- Resolution on release: target cluster from `layout` hit-test → relocate if `target ≠ source`,
  else snap back.

## State Machine

```
        trigger                 layout+grab+animate-in
IDLE ─────────────► ACTIVATING ───────────────────────► ACTIVE
  ▲                                                     │  ▲ │
  │ restore done                                        │  │ │ button-down on thumbnail
  │                                                     │  │ ▼
DEACTIVATING ◄─────────────────────────────────────────┘  (MAYBE_DRAG)
  ▲   reverse-animate + ungrab + restore state             │
  │                                                         │ moved ≥ threshold
  │ trigger / Esc / bg-click / cancel                       ▼
  └──────────────────────────────────────────────────►  DRAGGING
                                                            │
                    release: hit-test → relocate|snapback   │
        ACTIVE ◄────────────────────────────────────────────┘
        (stay open + reflow)     [CLARIFIED 2026-07-11]
```

Transitions:
- **IDLE → ACTIVATING** (trigger): `activate_plugin`; capture views + `pre_session`; grab input;
  compute `layout()`; add transformers; start entry animation.
- **ACTIVATING → ACTIVE**: entry animation complete; hover highlights a thumbnail.
- **ACTIVE → (MAYBE_DRAG)** (button-down on a thumbnail): record `press_origin`; do **not** select.
- **(MAYBE_DRAG) → ACTIVE** (button-up below threshold): scale-parity click → focus view + close.
- **(MAYBE_DRAG) → DRAGGING** (motion ≥ `drag_threshold`): bind thumbnail to cursor.
- **DRAGGING → DRAGGING** (motion): update thumbnail transform translation; highlight target cluster.
- **DRAGGING → ACTIVE** (button-up): hit-test → `move_to_workspace` if target≠source (+ fullscreen
  re-request), then **stay open** and reflow; else snap back. *(Clarified: not → DEACTIVATING.)*
- **ACTIVE/DRAGGING → DEACTIVATING** (trigger / Esc / background click / `cancel`): reverse animate;
  ungrab; remove transformers; restore every `pre_session`; `deactivate_plugin`.
- **DEACTIVATING → IDLE**: restore complete.

Safety (Principle VI): a view unmapping in any state removes its `ThumbnailRecord` and reflows; if it
was the drag subject, the drag ends as a snap-back with no relocation.

## Relationship to the pure layout function

`layout()` (see `contracts/layout.md`) consumes a projection of these entities — `{view_id,
source_ws, natural_size}` + `grid` + `output_size` + options — and returns `target_rect` +
`cluster_id` per view and the `region` per cluster. The plugin then binds those results back onto
`ThumbnailRecord`/`WorkspaceCluster`. The inverse (`hit_test(point) → ws`) is the same module, so the
drag's drop resolution and the visual clusters can never disagree (Principle I).
