# Groundwork notes: expo-style close for spread-overview (candidate 003)

Verified 2026-10-29 against this checkout (0.12-dev, master @ 46f31453).

## How expo actually does it (VERIFIED)

- `plugins/single_plugins/expo.cpp` owns a `wf::workspace_wall_t` (`wall`) and a
  `wf::geometry_animation_t zoom_animation{zoom_duration}` (`:41-46`).
- Close: `start_zoom(false)` sets the animation from the current viewport to
  `wall->get_workspace_rectangle(target_ws)` (`:333-336`) and feeds it to
  `wall->set_viewport(zoom_animation)` (`:342`).
- The wall renders every workspace tiled into one big surface; the viewport rect selects
  which part of that surface fills the screen. **The zoom is a camera move over real
  workspace content — no per-node scaling at all.**
- Wall API: `set_viewport()` (`workspace-wall.hpp:69`), `get_workspace_rectangle()` (`:102`),
  `set_gap_size()` (`:56`).

## Why we cannot just reuse it

spread-overview transforms **individual views** (`view_2d_transformer_t`) and draws its own
overlay nodes. ADR-001 (self-managed drag over the live views) and ADR-002 (no scene capture,
to avoid cross-GPU aux-buffer traffic on the Intel+NVIDIA box) are exactly the decisions that
rule out a wall. Adopting `workspace_wall_t` would replace the plugin's rendering model and
reintroduce the capture surface ADR-002 avoided.

## The design tension (this is the decision to make)

Expo's zoom lands every window exactly at its real desktop position, because the wall's
content IS the real workspaces at proportional positions. **002's thumbnails are packed into
rows, not at proportional positions.** So zooming the destination cell to fill the screen would
deposit windows at scaled *row slots*, not where they really are — and today's exit instead
interpolates each thumbnail from its slot to identity (its real position), which is what makes
the hand-off seamless.

Three ways out:

| | What zooms | Destination windows | Risk |
|---|---|---|---|
| **A** chrome-only | grid lines, veil, wallpaper tiles scale toward the destination cell and fade | settle to real positions (as today) | lowest; windows and chrome move on different paths, may read as two motions |
| **B** full camera | everything incl. thumbnails scales toward the cell | would land at scaled row slots — needs a second, conflicting interpolation to identity | highest; two competing motions on the same node |
| **C** hybrid | chrome + *other* workspaces' thumbnails scale outward and fade | destination cell's thumbnails settle to real positions | moderate; closest to expo's read, one motion per node |

Recommendation: **C**. Every node gets exactly one motion, the destination resolves seamlessly,
and the surrounding grid "pulls away" the way expo's does.

## DECIDED (user, 2026-10-29)

- **Model C (hybrid)**: destination cell's windows settle to their real positions as today;
  every OTHER cell's thumbnails scale outward and fade; chrome (grid, veil, wallpaper) scales
  toward the destination cell and fades.
- **Scope: exit AND a mirrored entry.** The entry becomes the inverse camera move — chrome and
  other cells zoom IN from the current workspace's cell while that cell's windows converge from
  their real positions. This REPLACES 001's directional slide-in, so the gate must judge both,
  and the old entry path should be removed rather than left dead (the way finalize_entry_anim
  was in 002/I3).
- Consequence for the spec: entry and exit become one camera concept with two directions, so
  `spread_anim::ENTRY` and the exit path should share the camera and differ only in direction
  and in which cell is the pivot (current workspace on entry, destination on exit).

## Implementation shape (if C)

One shared camera, driven by the existing per-frame `animate_step`:

- A `geometry_animation_t`-style zoom from the full output rect to the destination cell rect,
  or equivalently (scale, pivot) with scale = output/cell per axis. At the defaults
  (`cluster_gap = 0`, `outer_margin` hardcoded 0 in `render.cpp:39`) that is exactly
  `grid.x` × `grid.y` — uniform for a square grid, anisotropic for e.g. 4×2. **Open question:
  uniform (letterboxed) or anisotropic (cell→screen exactly)?** Expo sidesteps this by sizing
  its wall with `maxdim`.
- Overlay nodes need a `set_camera(scale, offset)` and must apply it to their *destination*
  rects only:
  - `border_node_t`, `dim_node_t`: one baked output-sized texture blitted at `geometry` →
    blit into the transformed rect instead. No re-rasterization; the baked grid lines scale
    with it, which is the wanted effect.
  - `wallpaper_node_t`: already blits per-cell rects (`overlay.hpp`), transform each.
  - `highlight_node_t`: draws rects via `add_rect`, transform them (not visible during close).
- Thumbnails: compose the camera with each view's slot transform inside `animate_step`
  (`render.cpp`), i.e. final scale = camera_scale × slot_scale, translation mapped through the
  pivot. Destination-cell views keep today's settle-to-identity path untouched.

## Scope question

Exit only, or mirror it on entry (zoom out from the current workspace cell)? Entry currently
converges each thumbnail from its real position, which is well liked — a camera zoom-in would
replace that, so it should be a separate, explicit decision.

## Cost / risk notes

- No new GPU surface, no capture, no extra texture: the camera is destination-rect arithmetic.
  Consistent with ADR-002.
- `min_scale`/over-dense, hit-testing and drop resolution are untouched: the camera only exists
  during DEACTIVATING, when input is already ignored.
- The pure layout core does not change at all → no golden-fixture or oracle impact.
