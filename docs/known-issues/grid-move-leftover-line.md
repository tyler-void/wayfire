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

## Tearing hypothesis (raised 2026-10-31): ruled out in its scanout form

The reporter's instinct: *"not the geometry, it's the renderer — I used to have Wayfire on HDMI
with the 3060 before explicit sync and it tore; I think this is a tear."*

**The strongest part of that argument is right, and it corrects this document's original reasoning.**
A tear is only *visible* where two frames differ; in static screen areas it is invisible. During a
grid move the only thing changing is the moved window, so a tear would appear as a horizontal
discontinuity spanning exactly that window's width. "Length equals the window's width" is therefore
evidence *for* a tear, not against it.

It also invalidates the diagnostic this document originally led with. "Force a repaint; if the line
vanishes it is under-damage" does **not** discriminate: a tear on the final frame of the animation,
followed by no further repaints on a static desktop, leaves the torn front buffer on scanout
indefinitely and clears the moment anything repaints. Both hypotheses predict the same result.
Tearing also explains the intermittency more naturally — whether a flip lands mid-scan is a timing
race, where the damage story needs a rounding coincidence that the captured integral geometries
argue against.

**But a scanout tear is impossible in this build**, verified two ways:

- wlroots gates `DRM_MODE_PAGE_FLIP_ASYNC` on `conn_state->base->tearing_page_flip`
  (`backend/drm/drm.c:1002`), a flag only the compositor can set. Wayfire never sets it:
  `grep -rn "tearing_page_flip\|tearing_control\|set_tearing" src/ plugins/` returns nothing. Every
  flip is vsync'd.
- Client side, `wayland-info` on the live session lists `wp_linux_drm_syncobj_manager_v1`
  (explicit sync, present since `a4df7649`) and **no** tearing-control interface at all, so no
  client can request tearing either.

VRR is also off — the kanshi profile sets `mode preferred` with no `adaptive_sync`.

The 3060-on-HDMI experience was a genuinely different setup (different GPU, no explicit sync), so
reasoning by analogy from it was reasonable; the conditions have since changed.

**What survives of the hypothesis, and it may well be the answer**: "the renderer, not the
geometry". The prime renderer-side candidate is the grid **crossfade** itself. It captures the
window into an auxiliary buffer (`capture_current_contents`) and blends that snapshot against the
live window every frame. A seam at the snapshot's edge is a compositing artifact, has nothing to do
with view geometry, and would look exactly like this.

### The bisect that settles it

`grid/type` is currently at its default, `crossfade`. Set it to `none` (no animation at all) and
use the machine normally:

- **Artifact disappears** → it is the crossfade's capture-and-blend, i.e. renderer-side, inside
  `plugins/grid/wayfire/plugins/crossfade.hpp`.
- **Artifact persists** → it is the final commit / damage of the move itself, and the crossfade is
  innocent.

`wobbly` is a third setting worth comparing, since it animates without the snapshot blend.

## Sharper symptom (2026-10-31, second report) — and what it rules out

> "The glitch sometimes has the texts of the window inside it. It looks like a slice of the window
> that was moving before, and is now stuck and **blinking**."

Two things follow immediately, and between them they eliminate both earlier leading hypotheses:

- **Stale framebuffer pixels do not blink.** They are frozen until something overwrites them.
- **A one-shot tear does not blink.** It is a single bad scanout.

Something alternating between present and absent on successive frames points at **buffer age**.
`WLR_SWAPCHAIN_CAP` is 4 and wlroots renders with per-buffer damage accumulation
(`wlr_damage_ring_rotate_buffer`, `src/output/render-manager.cpp:369`): each frame repaints only
the damage accumulated *for the particular buffer being reused*. If a region was drawn into one
buffer and the later cleanup damage did not cover it for that buffer's age, the stale slice lives
in one buffer of the rotation and not the others — so it appears and disappears as the swapchain
rotates. **Blinking is the signature of a buffer-age damage bug**, and nothing else in this stack
produces it.

"A slice of the window as it was moving" fits: content from an intermediate animation frame,
captured into a buffer that never got cleaned.

### First discriminating measurement (2026-10-31)

The reporter marked a sighting **while the artifact was visible on screen**. The capture was clean:
the vacated half of the screen profiled as plain wallpaper with zero row anomalies, and visual
inspection of the frame found nothing.

That is a positive result, not a failed capture. `wlr_screencopy_v1.c:283,313`
(`frame_handle_output_commit` → `src_buffer = event->state->buffer`) copies the buffer of the
**next output commit**. So:

- If the slice were stale content present in *every* slot of the swapchain, it would appear in
  every capture. It did not, while the reporter was looking at it.
- Therefore what is on scanout and what goes into the next committed buffer **differ** — which is
  the definition of a per-buffer problem.
- It also argues against "something is actively drawing the slice", since that would land in every
  buffer and show in every capture.

Caveat: one clean capture could be timing luck. It corroborates the blinking report rather than
proving it alone.

**Consequence for tooling**: a single screenshot is the wrong instrument — it samples one slot of
four. The marker now fires a **burst of 8 captures over ~1.2 s**, which makes the next sighting
decisive three ways: the slice visible in *some but not all* captures proves per-buffer staleness
outright; in *all* captures means something is drawing it; in *none* means the capture path cannot
observe it and only `-R` or an off-machine camera will.

### The experiment that settles it — built in, no instrumentation needed

```sh
/usr/local/bin/wayfire -R -d | tee /tmp/master-log-tty2     # on tty2/tty3, NOT the daily session
```

`-R` / `--damage-rerender` sets `runtime_config.no_damage_track`, which ORs the full buffer extents
into the damage every frame (`render-manager.cpp:372`). Every buffer is always fully repainted, so
no stale content can survive in any slot of the swapchain. Then do a few dozen grid moves:

- **Cannot reproduce under `-R`** → conclusively damage tracking / buffer age. With the blinking,
  that is a complete diagnosis.
- **Still reproduces under `-R`** → it is not stale content at all; something is actively drawing
  that slice on the frames where it shows. Prime suspect then is the crossfade snapshot node
  outliving its animation (`capture_current_contents` / `crossfade_render_instance_t::render`).

Expect `-R` to feel sluggish: measured paint cost already exceeds the 6.06 ms budget at 165 Hz in
96% of frames (`tools/render-bench/`), and `-R` removes every damage optimisation on top of that.

`-D` / `--damage-debug` is the complementary visual run: it paints every *undamaged* region yellow
each frame (`render-manager.cpp:1407`), so whether the slice's area receives damage becomes
directly visible. It cannot confirm a buffer-age bug — it forces a full-screen repaint as a side
effect — but it is good at showing which regions get damage during the move.

## Outcome of the `-R` run (2026-10-31): NOT reproducible, and therefore inconclusive

A session was run on tty4 with damage tracking disabled — confirmed from `/proc`:
`/usr/local/bin/wayfire -R -d`. The artifact did not appear.

**This does not confirm the buffer-age hypothesis.** The reporter could not reproduce the artifact
*at all* that day, including on a plain session (tty2 was running `/usr/local/bin/wayfire -d`
concurrently, with no `-R`). A negative result under `-R` only means something when the artifact is
reproducible **without** `-R` on the same day — that plain session is the control, and without it
the `-R` run carries no information. The hypothesis is still the leading one, on the strength of
the blinking report and the clean-capture-while-visible measurement, but it is **unconfirmed**.

### An observation worth keeping for the next attempt

Every sighting so far was on the **long-running daily session** (tty1, 9+ hours of uptime, many
windows, many workspace switches). Neither fresh test session reproduced it. That hints the
artifact depends on accumulated session state rather than on the move itself — swapchain
behaviour after long uptime, memory pressure, or output state churn. So:

- **Try to reproduce on the daily session first**, not a fresh one. If it only ever happens there,
  that is itself a strong clue.
- Freshly started sessions may simply be the wrong place to look, which would explain both the
  non-reproduction and why an afternoon of deliberate grid moves produced nothing.

A build change is not the explanation: the full rebuild that day touched only `plugins/spread-overview`;
`grid` and `src/` were untouched, and the rebuild used the same configuration.

### When it comes back

1. Reproduce on the **daily** session, then hit `super`+`shift`+`G` — the marker now fires a burst
   of 8 captures over ~1.2 s, which is decisive three ways (see above).
2. Only then run the `-R` control, having established same-day reproducibility first.

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
