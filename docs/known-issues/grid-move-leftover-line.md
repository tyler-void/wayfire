# Known issue: a grid move sometimes leaves a horizontal line behind

**Status:** open, **unconfirmed mechanism**. Reported 2026-10-31. Not caused by any fork code as
far as the paths below go; `src/` and `plugins/grid/` are unmodified upstream.

## Symptom

Moving a window to a slot with `<super>` + numpad (the `grid` plugin) *sometimes* leaves a visual
artifact: **a straight horizontal line whose length equals the moved window's width.** Intermittent.

## Configuration in play

- `grid` with `slot_*` on the numpad; `grid/type` is left at its default, **`crossfade`**
- `decoration` (server-side decorations), `animate`, `move`, `resize` enabled
- **`blur` is NOT enabled** — worth stating up front, because blur's damage padding is the usual
  cause of exactly this artifact and it is not available as an explanation here

## Hypotheses ruled out, with evidence

Each of these was checked in source, not assumed. They are recorded so the next person does not
re-walk them.

| Hypothesis | Why it is not that |
|---|---|
| Blur damage padding | Plugin not enabled, no `[blur]` section |
| Adding/removing a transformer under-damages | `_add_transformer` / `_rem_transformer` (`src/view/view-3d.cpp`) both call `damage_node(get_bounding_box())` **before and after** the restructure, so the old transformed box is damaged |
| Grid's per-frame crossfade update under-damages | It wraps the write in `begin_transform_update()` / `end_transform_update()` (`plugins/grid/wayfire/plugins/crossfade.hpp:364-367`) — the correct damage-old-then-new pattern |
| Float → int damage truncation | `wf::containing_box()` (`src/geometry.cpp:256`) does `floor` on the origin and `ceil` on the far edge, i.e. the **enclosing** box. Damage conversion errs outward, so it cannot lose a row |

A related observation that weakens any fractional-geometry story further: a real grid move captured
live produced only integral geometry (`1720,0,1720,720` → `1720,0,1720,1440`, `tiled-edges 9 → 11`)
on a 3440×1440 output, where slots divide evenly. Fractional view rects may not arise here at all.

## Still open

- The `decoration` plugin's frame. Its damage on resize has not been examined, and a stale
  decoration edge would look exactly like this.
- Whether the strip is at the window's **old** rect (the vacated area was not cleaned) or its
  **new** one (an edge artifact at the destination).

## The diagnostic that splits the space

Next sighting, before anything else: **force a repaint over the line** — sweep the pointer across
it, or drag another window over it.

- **It disappears** → stale framebuffer content, i.e. under-damage somewhere. Hunt the missing
  damage.
- **It persists** → something is actively drawing there every frame: a node with a wrong geometry
  (most likely the decoration frame, or a leftover crossfade snapshot). A different hunt entirely.

Two details that narrow it a lot either way: whether the line sits at the old or the new rect, and
whether it happens with client-side-decorated apps (GTK) as well as server-decorated ones. If only
the latter, that is close to a diagnosis on its own.

`showrepaint` is in the available plugin set and tints damaged regions per frame — a region that
never gets tinted while its neighbours do *is* the under-damage, visibly.

## Why this is not patched here

Same reasoning as [ADR-006](../adr/006-core-patches-and-the-commit-failure-loop.md): the affected
code is upstream (`grid`, the scene graph), every fork change so far is a plugin directory plus one
appended line in `plugins/meson.build`, and that is what keeps upstream syncs mechanical. Diagnose,
report upstream, and patch locally only if it becomes a daily obstruction.
