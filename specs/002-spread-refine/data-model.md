# Phase 1 Data Model: 002-spread-refine (delta over 001)

001's data model (`specs/001-spread-overview/data-model.md`) stands. This file lists **only what 002
adds or changes**. All state remains transient and in-memory, owned by one per-output plugin
instance.

---

## Changed entities

### `layout_input_view` (pure layout input — contract §1)
| Field | Change | Notes |
|---|---|---|
| `id` | unchanged | still the enumeration index; after 002 it is only a **tie-breaker** for ordering, not the primary order |
| `source_ws` | unchanged | |
| `natural_size` | unchanged | |
| **`natural_pos`** | **NEW** `pointf{}` | top-left **inside its own workspace**. Filled by `render.cpp` as `view geometry − (source_ws − current_ws) · output_size`. Drives row membership (`cy`) and within-row order (`cx`). |

### `layout_options` (pure layout input — contract §1)
| Field | Change | Notes |
|---|---|---|
| `spacing`, `cluster_gap`, `outer_margin`, `max_scale`, `min_scale` | unchanged, same order and defaults | existing aggregate initializers must keep compiling |
| **`small_window_boost`** | **NEW** `double = 1.5` | β; `< 1.0` is treated as `1.0`. From the `small_window_boost` option. |

### `WorkspaceCluster`
`region` unchanged (invariant #9 — fixed, equal, density-independent). `over_dense` keeps its
meaning but changes its **derivation**: now "the smallest *per-window* scale in the cluster
< `min_scale`" instead of "the cluster's single uniform scale < `min_scale`".

### `ThumbnailRecord`
`target_rect` unchanged in type; it is now produced by the per-window sizing of contract §2 rather
than one cluster-wide scale. No new per-view field is needed for the layout.

---

## New concepts

### ThumbnailRow (pure layout, internal)
Exists only inside `layout()`; never crosses the module boundary and is not part of `layout_result`.
- `wins`: ordered list of members (left → right by `cx`, ties by `id`)
- `fw`, `fh`: accumulated emphasis-weighted width / max height (D2: own windows only)
- `add`: per-row fit factor `a_row ∈ (0, 1]`
- Rows are stacked top → bottom; empty rows are dropped before placement (D1).

### PressFreeze (session, from R-107)
Not a struct — a **state rule** on the existing `anim_state` map:
- On **press** on a thumbnail that has an entry in `anim_state`, that entry is **erased**. The
  transformer already holds the current interpolated values, so erasing freezes the thumbnail
  exactly where it is drawn. Every other clock keeps running.
- `drag_orig_tx/ty` then capture that frozen translation, so drag-follow displacement equals pointer
  displacement since the press (SC-005).
- Consequence: a thumbnail's **live rect** (centre = view centre + translation, size = view size ×
  scale) and its **slot rect** (`thumb_rects`, from `current_layout`) can now differ. Hit-testing and
  drop resolution MUST use the live rect; only the *animation target* uses the slot.

### SnapBackAnimation (session, from R-108)
A `thumb_anim_t` created for the dragged view on a cancelled drop:
- from: current transform (scale + translation) · to: the **layout slot** recomputed from
  `current_layout`
- `alpha.set(1.0, 1.0)` — mandatory: `animate_step` writes `tr->alpha` every frame and a fresh clock
  would otherwise fade the thumbnail out
- duration: `duration` (the open/reflow option), not `exit_duration`
- installs `anim_hook` if it is not already running; removed when the clock settles.

### Exit clock source (R-109)
`start_exit_anim`'s per-thumbnail clocks and the `overlay_fade` both switch from `duration` to
**`exit_duration`**. `overlay_fade` only ever runs during an exit, so this is a one-line source swap
with no other reachable state.

### HighlightFade (R-110, P3, deferrable)
The drop-target highlight becomes its own overlay element with an alpha ramp (fixed ≈120 ms):
- `target_cluster`: index of the cluster under the dragged thumbnail's centre (−1 = none)
- `alpha`: animated 0 → 1 on enter, 1 → 0 on leave
- The dim veil's bright-cell switch (`dim_node_t::set_active`) follows the same ramp.
- If this needs more than splitting one element out of `border_node_t`, FR-011 is deferred with the
  reason recorded in `tasks.md`.

---

## State transitions

Unchanged from 001 (`IDLE → ACTIVATING → ACTIVE ⇄ DRAGGING → DEACTIVATING → IDLE`), with two
refinements:

1. **ACTIVE/ACTIVATING → DRAGGING** no longer forces every thumbnail to its end state. Only the
   pressed thumbnail's clock is erased (PressFreeze); the rest keep animating. `finalize_entry_anim()`
   — the old "snap everything" helper — is **deleted**.
2. **DRAGGING → ACTIVE (cancelled drop)** starts a SnapBackAnimation instead of assigning the
   transform directly.

`DEACTIVATING` is unchanged except that its clocks are driven by `exit_duration`.

---

## Validation rules

- `small_window_boost < 1.0` → treated as `1.0` (no emphasis); metadata also declares `min="1.0"`,
  `max="4.0"`.
- `natural_pos` has no validity constraint — any finite value is acceptable; ordering only needs to
  be consistent within one workspace.
- Every `target_rect` must have positive width and height (invariant #10) — guaranteed by D4.
- View-lifetime (Principle VI) is unchanged and now also covers a view unmapping **during a
  snap-back**: `forget_view` erases its `anim_state` entry along with the rest of its state.
