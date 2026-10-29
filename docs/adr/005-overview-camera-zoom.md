# ADR-005: the overview opens and closes as one camera zoom, folded into the existing transforms

**Status:** Accepted
**Date:** 2026-10-29
**Feature:** 003-spread-zoom
**Amends:** 001's entry animation (the per-cluster directional slide-in, `spread_anim::ENTRY`) and
002's exit animation (fade at fixed geometry). 001 contract invariants and 002's layout contract
are **unchanged** — this ADR is about motion only.

## Context

The overview's two animations were designed independently: 001 slid thumbnails in from each
window's real position, with a per-cluster direction for off-workspace windows; 002 added a
destination-aware exit that settles the destination workspace's windows and fades everything else
in place. The grid, veil and wallpaper faded at a **fixed size** in both.

The user's report: the close should "zoom out and scale like on expo", where the whole grid scales
toward the workspace being entered rather than dissolving where it sits. Expo's close is visibly
one coherent camera move; ours read as several independent fades.

## Decision

**One camera, two directions.** Opening pulls back from the current workspace's cell until the
whole grid is visible; closing converges on the cell the user is about to land on. The camera is
the pure function in `specs/003-spread-zoom/contracts/camera.md`:

```
k(t) = lerp(1, max(O.w/cell.w, O.h/cell.h), t)      A(t) = lerp(cell_centre, O_centre, t)
camera(p, t) = (p − cell_centre) · k(t) + A(t)
```

anchored so that `t = 0` is **exactly** the identity (the naive `Oc`-anchored form is a
translation at `t = 0`), with one scalar scale for both axes so nothing stretches.

**Hybrid roles.** The pivot cell's windows settle to/from their real desktop positions — today's
seamless hand-off; every *other* cell's thumbnails and all the chrome follow the camera and fade.
Every node gets exactly one motion.

**Implemented by folding the camera into what each node already computes** — each view's existing
`view_2d_transformer_t`, and each overlay node's destination rects.

**Prerequisite:** the dim veil and grid border stop baking an output-sized cairo texture and draw
solid rects instead.

## Alternatives considered

- **Reuse expo's mechanism** (`workspace_wall_t` + an animated viewport, `expo.cpp:333-342`) —
  rejected: the wall composes every workspace into one surface, which is exactly the scene-capture
  buffer traffic ADR-002 refused on this hybrid Intel+NVIDIA machine, and it is incompatible with
  ADR-001's drag of the live views.
- **Full camera zoom including the pivot cell's windows** — rejected: 002 packs thumbnails into
  rows, not at proportional desktop positions, so mapping the cell onto the screen would deposit
  windows at scaled row slots; they would then need a second, competing interpolation to their
  real geometry.
- **Chrome-only zoom** — rejected as the primary: cheapest and lowest-risk, but windows and chrome
  then move along different paths, which reads as two motions rather than one camera.
- **Anisotropic scale** (map the cell exactly onto the screen) — rejected: stretches windows on
  non-square grids. Uniform "cover" keeps aspect ratios; the overflow runs off-screen, where
  everything is fading anyway.
- **Keeping the baked overlay textures and re-rasterising per frame** — rejected: 19.8 MB of
  re-upload per frame at 3440×1440 on a 32-EU iGPU with a 6.06 ms budget.

## Consequences

**Upsides**
- Open and close read as one gesture, and the chrome scales instead of fading at a fixed size.
- The camera lands *inside* each view's transformer, so 002's live-rect hit-test keeps reporting
  true on-screen rects mid-zoom — the "the thumbnail you see is the one you grab" guarantee
  (002 FR-009) survives a moving camera with no extra machinery.
- The rect conversion removes ~40 MB of memory traffic per cell crossing during a drag (the veil
  previously re-rasterised and re-uploaded the whole output on `set_active`), and **collects
  002's deferred FR-011**: the veil's bright-cell transition becomes a per-cell alpha ramp.
- No new GPU surface, no capture, no new configuration option, no change to `layout()`.

**Costs**
- 001's directional slide-in is **gone**. It was liked; the camera subsumes it (off-workspace
  thumbnails still arrive from their grid direction, now because the camera is pulling back rather
  than because of per-cluster direction logic), but it is a real behaviour change, which is why
  the mirrored entry was a separate explicit decision and ships as its own increment.
- A camera-driven thumbnail's effective scale exceeds `max_scale` while it zooms off-screen. That
  is intended: invariant #5 constrains `layout()`, not the animation.
- The veil's non-overlapping cover of gaps and margins is new arithmetic that only matters when
  `cluster_gap > 0` — a configuration the default does not exercise, so it carries test risk the
  default path does not.
- The pivot cell's chrome zooms while its windows settle. Expected to read fine because the chrome
  is fading, but it is a judgement call rather than a derivation.

**Revisit if:** the mirrored entry is worse than 001's slide-in in daily use (revert I3 alone — the
close is independently valuable), or a non-square grid makes the uniform "cover" scale feel like it
dives too far past the screen edges.

## Affected artifacts

- `plugins/spread-overview/src/layout.{hpp,cpp}` — the pure camera beside `layout()`.
- `plugins/spread-overview/src/overlay.{hpp,cpp}` — veil + grid as rects, per-cell alpha,
  `set_camera` on every overlay node.
- `plugins/spread-overview/src/render.cpp` — camera state, per-frame compose, entry rewrite.
- `plugins/spread-overview/src/input.cpp` — pivot selection per dismissal route; drag damage via
  `begin/end_transform_update`.
- `plugins/spread-overview/src/overview.hpp` — camera session state; removal of the ENTRY path.
- `test/plugins/spread-overview-layout.cpp` — camera invariants #C1–#C7.
- `specs/003-spread-zoom/contracts/camera.md` — the normative definition.
- `specs/002-spread-refine/tasks.md` — the FR-011 deferral is closed by I0.
- *(Update this list to what was actually built before closing 003.)*
