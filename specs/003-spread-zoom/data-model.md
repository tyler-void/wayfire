# Phase 1 Data Model: 003-spread-zoom (delta over 001 + 002)

Everything remains transient per-output session state. This lists only what 003 adds or changes.

---

## New

### `camera_t` (pure, `contracts/camera.md`)
Plain value type beside `layout_input_view` / `layout_options`: `pivot` (the cell rect), `output`
(logical size), `t ∈ [0,1]`. No Wayfire types; unit-tested in isolation.

### CameraSession (session state, `overview.hpp`)
| Field | Meaning |
|---|---|
| `camera` | the `camera_t` for this session; `pivot` is set when a direction starts, `t` written every frame |
| `camera_clock` | `simple_animation_t` driven by `duration` (open) or `exit_duration` (close) — **no new option** |
| `camera_pivot_index` | which cluster the pivot rect came from, so the chrome and the "is this view camera-driven?" test agree |

Lifetime: set at ACTIVATING (pivot = workspace current at trigger) and at the start of
DEACTIVATING (pivot = the destination route, R-206). Cleared by `clear_spread()`.

### Per-view role (derived, not stored)
Each session view is, for the duration of one animation, either:
- **settling** — it belongs to the pivot cell: interpolates slot ⇄ real position, the existing
  path; or
- **camera-driven** — every other view: its slot rect is mapped through the camera each frame.

Derived from `get_view_main_workspace(v)` versus the pivot cell, so nothing new is persisted and a
view that is relocated mid-session cannot hold a stale role.

---

## Changed

### `dim_node_t` (I0)
| | Before | After |
|---|---|---|
| Content | one baked ARGB32 texture the size of the output, active cell punched with `CAIRO_OPERATOR_CLEAR` | no texture; one `add_rect` per covered region |
| Update | `set_active()` re-rasterises + re-uploads 19.8 MB | `set_active()` retargets a per-cell alpha ramp; no allocation |
| New state | — | `std::vector<double> cell_alpha` (per cluster), `fade` progress, plus the gap/margin strips computed once in `set_content` |
| Camera | — | `set_camera(camera_t)`: destination rects mapped per frame |

### `border_node_t` (I0)
| | Before | After |
|---|---|---|
| Content | baked texture, per-cell cairo strokes | no texture; one rect per row boundary and per column boundary (incl. outer edges) — a shared edge drawn **once** |
| Camera | — | `set_camera(camera_t)` |

### `wallpaper_node_t`, `highlight_node_t`
Keep their current rendering (one uploaded image; solid rects). Both gain `set_camera(camera_t)`
so their destination rects follow the zoom.

### Thumbnail transform (`render.cpp`)
`animate_step` composes the camera for camera-driven views:
`scale = slot_scale × camera_scale`, and the translation is chosen so the rendered centre equals
`camera_apply(slot_centre, t)`. Settling views are untouched.

**Consequence (FR-010)**: because the camera lands *inside* each view's transformer, 002's
`live_thumb_rect` — which reads the transformer — reports the true on-screen rect mid-zoom with no
change, so hit-testing, the drop target and the press-freeze keep working.

### Entry path (`spread_anim::ENTRY`, FR-015)
001's per-cluster directional slide-in is **removed**: off-workspace windows now arrive via the
camera, and on-workspace windows already started at their real position, which is exactly the
settling role. The `spread_anim` enum keeps `REFLOW` and `NONE`.

---

## Unchanged (explicitly)

`layout()`, `hit_test_cluster()`, every layout invariant 1–13, cluster region geometry, drop
resolution, `over_dense`, the stay-open re-flow, capture/restore of pre-session view state, and
the view-unmap scrubbing (`forget_view`) — which gains the camera role only implicitly, since the
role is derived rather than stored.

---

## State transitions

`IDLE → ACTIVATING → ACTIVE ⇄ DRAGGING → DEACTIVATING → IDLE` as before, with:

1. **ACTIVATING** sets `pivot = current workspace's cell`, `t: 1 → 0` over `duration`.
2. **DEACTIVATING** sets `pivot = destination cell` (clicked view's workspace / clicked empty cell
   / current workspace), `t: 0 → 1` over `exit_duration`.
3. **ACTIVE/DRAGGING** hold `t = 0`; the camera is the identity, so nothing it touches moves.
4. **Re-flow** does not alter the camera (FR-009).

## Validation rules

- `t` is clamped to `[0,1]` (#C7); the clock never drives it outside.
- A pivot of zero width/height is impossible (cells are clamped `≥ 1` by `cluster_region`), but
  `camera_scale` divides by `max(1, pivot.w/h)` regardless.
- A 1×1 grid yields the identity camera at every `t` (#C4), so the animation degrades to the
  settle/fade with no branch.
- Per-cell veil alpha stays within `[0, background.a · (1 − inactive_brightness)]`.
