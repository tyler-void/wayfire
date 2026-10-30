# Phase 0 Research: 003-spread-zoom

Numbering continues 001 (R1–R16) and 002 (R-101–R-115) from **R-201** so the three sets never
collide. Tags: **VERIFIED** = read in this checkout · **MEASURED** = numbers from this machine ·
**INFERRED** = reasoned, confirm while implementing.

Source material: `spec.md`, `design-notes.md` (the verified expo mechanism and the recorded
decisions), and the hot-path audit done on 2026-10-29.

---

## R-201 — Expo's mechanism is not reusable; the camera must fold into the existing transforms (VERIFIED)

Expo owns a `wf::workspace_wall_t` and animates a viewport rectangle over it
(`plugins/single_plugins/expo.cpp:41-46`, `:333-342`; wall API `workspace-wall.hpp:56,69,102`).
The wall composes every workspace into one surface — precisely the scene-capture traffic ADR-002
refused on this hybrid Intel+NVIDIA machine, and incompatible with ADR-001's live-view drag.

**Decision**: implement the camera as **arithmetic folded into what each node already computes** —
each view's existing `view_2d_transformer_t` scale/translation, and each overlay node's
destination rects. No wall, no capture, no new GPU surface.

**Consequence worth stating up front**: because the camera is composed *into* each view's
transformer rather than applied as a separate pass, 002's live-rect hit-test
(`live_thumb_rect` reads the transformer) keeps working mid-zoom **with no change** — which is
how FR-010 is satisfied almost for free.

---

## R-202 — Camera transform — NORMATIVE

One pure function. `C` = the pivot cell's centre, `O` = the output rect, `t ∈ [0,1]` the zoom
position (`t = 0` whole grid visible, `t = 1` pivot cell fills the screen):

```
s      = max(O.w / C_cell.w, O.h / C_cell.h)      // uniform "cover" scale (spec Assumptions)
k(t)   = lerp(1, s, t)                            // scale factor
A(t)   = lerp(C, centre(O), t)                    // anchor
camera(p, t) = (p − C) · k(t) + A(t)              // overview point -> screen point
```

Properties the unit tests assert (invariants in `contracts/camera.md`):

- `t = 0` ⇒ `camera(p, 0) = p` for every `p` — **exact identity**, no drift. (The naive form
  `(p − C)·k + centre(O)` is the classic bug: it is a translation at `t = 0`, not identity.)
- `t = 1` ⇒ the pivot cell's centre maps to the output's centre and the cell covers the output.
- `k` is a single scalar for both axes, so aspect ratios are preserved (FR-007).
- A 1×1 grid gives `s = 1`, hence `k ≡ 1` and `A ≡ C`: the camera is the identity at every `t`,
  so the animation degrades to today's settle/fade with no special-casing and no division by zero
  (spec Edge Cases).

**Alternatives rejected**: anisotropic scale (maps the cell exactly to the screen but stretches
windows on non-square grids — violates FR-007); `min()` instead of `max()` (letterboxes, so the
grid never leaves the screen and the zoom reads as shrinking rather than diving in).

---

## R-203 — Hybrid roles: who follows the camera (VERIFIED decision)

| Element | Close | Open |
|---|---|---|
| Destination / origin cell's thumbnails | settle to real positions — **not** camera-driven (today's path) | converge from real positions to their slots — **not** camera-driven |
| Every other cell's thumbnails | camera from `t=0 → 1`, scaling away off-screen, fading | camera from `t=1 → 0`, scaling in from beyond the edges |
| Chrome (grid lines, veil, wallpaper tiles) | camera `0 → 1` + fade out | camera `1 → 0`, fading in |

**Rationale**: the destination cell's windows must land at their real geometry (FR-005), but 002
packs thumbnails into rows, not at proportional desktop positions — so a camera that maps the cell
onto the screen would deposit them at scaled row slots instead. Every node therefore gets exactly
one motion: either the camera or the settle, never both.

**Already true today**: 001's entry starts on-workspace windows at `scale 1, translation 0` (their
real position) — identical to the role this table gives the origin cell. The camera therefore
*replaces* the directional slide-in only for the **other** workspaces, which is exactly where
001's hand-rolled per-cluster direction logic lived (→ FR-015, ADR-005).

---

## R-204 — I0 prerequisite: baked output-sized textures must become rects (MEASURED)

Audit of the overlay nodes on 2026-10-29:

| Node | Today | Cost | When |
|---|---|---|---|
| `dim_node_t` | one baked ARGB32 surface the size of the output, with the active cell punched out | **19.8 MB** alloc + 4.95 Mpixel CPU fill + **19.8 MB** texture upload (3440×1440×4) | `set_active()` → `rerender()`, i.e. **every time a drag crosses a cell boundary** |
| `border_node_t` | same shape, grid strokes baked in | 19.8 MB + upload | once per `build_spread()` / `reflow()` |
| `highlight_node_t` | solid rects via `add_rect` | none | — (converted in 002) |
| `wallpaper_node_t` | one image uploaded once, blitted per cell | negligible | once |

**Why this blocks the camera**: a node that draws rects is scaled for free — new destination
rects. A node that blits a baked *output-sized* texture cannot be scaled without re-rasterising
19.8 MB **per frame**. So the conversion is a prerequisite, not cleanup.

**Decision**: I0 converts `dim_node_t` and `border_node_t` to solid rects (`render_pass_t::add_rect`,
verified in 002, `render.hpp:659`), which also:
- collects **002's deferred FR-011** — the veil's bright-cell fade becomes a per-cell alpha ramp,
  and the deferral recorded in `specs/002-spread-refine/tasks.md` § Deferred can be closed;
- removes ~40 MB of memory traffic per cell crossing mid-drag.

**Two geometry details that must be got right** (INFERRED, confirm visually at the I0 gate):

1. **The veil cannot be drawn as "fill everything, punch a hole"** — alpha compositing cannot
   subtract. Draw the **inactive cells** as rects instead, plus, when `cluster_gap > 0`, the gap
   strips: full-width horizontal strips between rows, and vertical strips **clipped to the row
   bands** so strip intersections are not painted twice (double-darkening). At the defaults
   (`cluster_gap = 0`, `outer_margin` hardcoded `0` in `render.cpp:39`) the cells tile the output
   exactly and no strips are needed.
2. **The grid must be drawn as lines, not per-cell boxes.** The cairo version stroked each cell
   and relied on adjacent strokes coinciding into one line at `gap = 0`
   (`overlay.cpp` comment). Four rects per cell would paint shared edges twice — visibly darker
   and wrong at any border alpha < 1. Draw instead one full-width rect per row boundary and one
   full-height rect per column boundary, including the outer edges.

---

## R-205 — Third damage fix in I0: the drag repaints the whole output (VERIFIED)

`input.cpp:258` writes `tr->translation_x/y` directly and then calls
`output->render->damage_whole()` — a full 4.95 Mpixel repaint **per pointer-motion event** while
dragging. `animate_step` already does this correctly: it wraps its writes in
`begin_transform_update()` / `end_transform_update()`, which lets the scene damage exactly the
node's old and new boxes.

**Decision**: wrap the drag write the same way and drop `damage_whole()`. Small, isolated, and it
improves the interaction most sensitive to latency on a 6.06 ms frame budget.

---

## R-206 — Where the camera state lives, and what drives it (INFERRED)

- State: pivot cell index, `t`, and a direction, held by the session next to the existing
  `anim_state` / `overlay_fade` clocks in `overview.hpp`.
- Clock: the existing `duration` (open) and `exit_duration` (close) — **no new option** (FR-008).
  The camera is a scalar `t`, so a `simple_animation_t` suffices; no `geometry_animation_t`.
- Tick: the existing per-frame `animate_step` hook, which already starts/stops itself
  (`ensure_anim_hook`, 002). Each frame it writes `camera(slot_centre, t)` into the
  camera-driven views' transformers and pushes the transformed rects into the overlay nodes.
- Pivot selection (FR-002/FR-003): **close** = the workspace the session will leave the user on —
  the clicked view's workspace, the clicked empty cell, or the current workspace for a plain
  dismissal; **open** = the workspace that was current at activation. The three close routes are
  the branches in `input.cpp`'s release handler, the same ones the `ensure_visible` bug lived in
  (see [[overview-workspace-switch-pitfall]] — resolve the workspace explicitly, never via
  `focus_raise_view(.., true)`).

---

## R-209 — The viewport change must be INSTANT, or vswitch fights the camera (VERIFIED)

**Found at the I0 gate, 2026-10-31**, from the user's report: closing the overview onto another
workspace "feels like I'm being dragged to another workspace instead of that workspace zooming in
to me".

`switch_workspace()` (`move.cpp`) calls `wf::workspace_set_t::request_workspace()`, which emits a
change request that **vswitch** picks up and *animates* — it slides the viewport sideways
(`plugins/vswitch/vswitch.cpp:528,539`: `add_direction(new_viewport - old_viewport)`). 001 chose
that deliberately and its comment still says "(with vswitch animation)". With a camera zoom added,
that slide becomes a **second, conflicting motion**: the cell grows while the viewport pans.

**Expo does not do this.** Its close is:

```cpp
start_zoom(false);                          // expo.cpp:348 — its own zoom
output->wset()->set_workspace(target_ws);   // expo.cpp:349 — INSTANT, not request_
```

`set_workspace()` changes the viewport with no animation, because the plugin is already providing
the motion.

**Decision**: for a close that changes workspace, the overview MUST set the viewport instantly and
let the camera carry the motion — `set_workspace`, not `request_workspace`. This is what makes
FR-002 read as "the workspace zooms in to me" rather than a pan.

**Careful**: `switch_workspace()` is also the empty-cell-click path, and a clicked thumbnail's
workspace change goes through `request_workspace` too (the `46f31453` fix). Both are "the overview
is closing onto a workspace", so both become instant. Nothing else in the plugin changes the
viewport.

---

## R-211 — Composing the camera into the transformer creates an inverse obligation (I3)

The camera is composed into each view's `view_2d_transformer_t` rather than applied by a separate
node. That is what makes FR-010 free — `live_thumb_rect` reads the transformer, so the rect it
reports is the true on-screen rect even mid-zoom, and press hit-testing, the drop target and the
press-freeze need no camera awareness at all (T028 holds by construction).

The cost is the mirror of that benefit: **a rendered transform is not a base transform.**
`animate_step` composes the camera onto whatever base a clock interpolates, so any path that
reads the rendered values back and reuses them as a base applies the camera twice on the next
frame. Two paths do exactly that, and both are reachable *during an opening zoom*:

1. `animate_thumb_to_slot` — the drag snap-back and the release of a press-frozen thumbnail. It
   takes the current transform as the start and the slot as the end. Reachable by pressing a
   thumbnail within the 300 ms entry, which is the most natural thing a user does after opening.
2. `snapshot_thumb_screen_rects` — the pre-relocate snapshot a REFLOW animates from.

Fixed by adding the pure inverse (`camera_unapply` / `camera_unrect`, contract #C8) and dividing
the camera out at those two sites, under the single predicate `camera_is_composed_into(view)` —
by construction the same condition `animate_step` used to compose it, so the two cannot disagree.
`live_thumb_rect` is deliberately left in screen space. **Screen space for input, base space for
clocks.**

Found by reading the path while wiring I3, not on screen; it would have presented at the Phase 5
gate (T034) as "press mid-zoom then release makes the thumbnail leap outward".

---

## R-210 — The close must switch the viewport FIRST, then zoom (INFERRED, found while wiring I2)

FR-005 says the destination workspace's windows arrive at their **real desktop positions**. The
exit path expresses that as "settle to identity" (scale 1, no translation) — which was correct in
002 only because the destination was always the **current** workspace.

With FR-002, the destination can be a *different* workspace. A view's `get_geometry()` is in the
**current viewport's** frame, so a window belonging to another workspace has real geometry that is
off-screen (±output size). Settling it to identity while the viewport is still on the old workspace
would animate it **off the screen** — the opposite of a seamless hand-off.

**Required order for a close that changes workspace:**

1. Snapshot every thumbnail's on-screen rect (`snapshot_thumb_screen_rects`, already exists).
2. `set_workspace(destination)` — instant, no vswitch (R-209).
3. **Re-express** each thumbnail's transform against its NEW geometry so the rendered rect is
   unchanged — nothing moves visually. This is exactly the trick the REFLOW path already uses
   ("re-expressed against the view's NEW geometry so the rendered rect starts unchanged").
4. Recompute `current_ws_index` (the veil's bright cell) and pivot the camera on the destination
   cell, which is now the current workspace's cell.
5. Run the camera `t: 0 → 1`; the pivot cell's windows settle to identity, now meaning on-screen.

**Second consequence**: the two routes the user cares about most — clicking a window on another
workspace, and clicking an empty cell — currently call `deactivate(false)`, i.e. they tear down
**instantly with no animation at all**. The zoom would never play on them. They must become
animated closes (`deactivate(true)`), with the workspace switched up front per the order above.
That is a deliberate behaviour change from 001/002, and the reason those routes feel abrupt today.

---

## R-207 — Reflow stays as it is (VERIFIED scope boundary)

The stay-open re-flow after a relocate keeps gliding each thumbnail from its pre-relocate
on-screen rect to its new slot (`spread_anim::REFLOW`, 002 R-108 machinery). It is **not** a
camera move (FR-009): there is no workspace being entered or left, so there is no pivot cell and
no zoom to speak of.

---

## R-208 — Increment order and gates (Principle III)

| # | Increment | Gate |
|---|---|---|
| **I0** | Veil + grid → rects; collect 002's FR-011 veil fade; drag damage via `begin/end_transform_update` | build + layout suite; tty2: **no visual change at rest**, veil bright-cell now *fades*, drag feels no worse; no 19.8 MB uploads remain in the drag path |
| **I1** | Pure camera math + unit tests (no plugin behaviour change) | layout test + full suite; identity at `t=0`, cover at `t=1`, 1×1 degeneracy, aspect preserved |
| **I2** | Camera on **close**: chrome + non-destination thumbnails; destination keeps settling; pivot per dismissal route | build/install/stamp; tty2 §I2 |
| **I3** | Camera on **open** (mirror); **delete** 001's directional slide-in (FR-015) | build/install/stamp; tty2 §I3 |
| **I4** | ADR-005, docs, FEATURES.md, close 002's FR-011 deferral note; full 001 regression (SC-006) | user sign-off |

I1 is deliberately test-only — like 002's I1, the riskiest reasoning (the transform) is proven
before anything reaches the screen. I0 comes first because I2/I3 are impossible to do cheaply
until the chrome stops being a baked texture.
