# Features in this fork

A running index of what this personal fork of [Wayfire](https://github.com/WayfireWM/wayfire)
adds or changes on top of upstream. Custom work is developed in-tree, spec-driven, with ADRs and
(where it makes sense) unit tests.

## Features

### 🪟 spread-overview · _complete_

An *expo × scale* hybrid window overview: every window across every workspace shown as an
individual thumbnail, grouped into a workspace grid, with drag-to-relocate between workspaces and
a stay-open reflow so you can rearrange several windows at once.

- **Code:** [`plugins/spread-overview/`](plugins/spread-overview/)
- **Docs:** [README](plugins/spread-overview/README.md) ·
  [ADR-001 self-managed drag](docs/adr/001-self-managed-drag.md) ·
  [ADR-002 wallpaper file-load](docs/adr/002-wallpaper-file-load.md) ·
  [ADR-003 snap-into-workspace](docs/adr/003-drop-snaps-into-workspace.md) ·
  [spec & contracts](specs/001-spread-overview/)
- **Highlights:** self-managed scene-graph drag; snap-fully-into-workspace drop semantics;
  per-cell wallpaper loaded cross-GPU-safe; research-driven entry / reflow / exit animations;
  use-after-free-safe view lifetime; pure, unit-tested layout core.
- **Status:** functionally complete — validated against the spec's 7 acceptance scenarios; the
  pure-layout doctest suite passes.

#### 🔎 spread-refine (002) · _legible thumbnails & motion continuity_

A refinement pass on the overview, on top of the shipped feature.

- **Per-window sizing.** A workspace's windows are no longer all shrunk to fit its largest one.
  Windows are packed into rows of variable width, ordered by where they actually sit, each sized
  individually, with a configurable emphasis for short windows (`small_window_boost`, default
  1.5). On the reference scene the smallest thumbnail goes from 64×48 to 157×116 — 3.1× the area —
  with nothing else shrinking. A port of GNOME Shell's `UnalignedLayoutStrategy` with four
  documented deviations ([ADR-004](docs/adr/004-per-window-cluster-scale.md)).
- **Motion continuity.** Pressing a thumbnail mid-animation freezes just that one where it is
  drawn instead of snapping every thumbnail to its slot; hit-testing and drop resolution read the
  thumbnail's live on-screen rect in stacking order, so the window you see is the one you grab; a
  cancelled drop glides back to its slot instead of teleporting.
- **Close feel.** `exit_duration` (default 225 ms) times the close independently of the open, and
  the drop-target highlight cross-fades between workspaces over 120 ms.
- **Proof.** The pure layout matches an executable Python oracle within 0.5 px across 54 golden
  rectangles, plus ~3.9 M assertions over 2,100 randomized clusters and three degenerate cases.
- **Docs:** [spec & contracts](specs/002-spread-refine/) ·
  [ADR-004](docs/adr/004-per-window-cluster-scale.md) ·
  [the oracle](specs/002-spread-refine/reference/layout_ref.py)

#### 🔭 spread-zoom (003) · _one camera, two directions_

The open and the close stopped being two animations and became one camera, the way expo feels.

- **The camera.** A pure function beside `layout()`: it maps overview space to the screen as `t`
  moves between *the whole grid is visible* and *the pivot cell fills the screen*. The close runs
  `t: 0 → 1` and pivots on **where you are going** — the clicked window's workspace, the clicked
  empty cell, or the current one for Esc. The open runs `t: 1 → 0` and pivots on where you were.
  No new option: the existing `duration` and `exit_duration` drive it.
- **Hybrid roles, derived per frame.** The pivot cell's windows *settle* to their real desktop
  positions; everything else *rides the camera* out past the screen edges. The camera is
  deliberately not applied to the settling windows — a layout slot is a *packed* position, so
  zooming one would land a window where the packer put it rather than where it lives. This also
  subsumes 001's directional slide-in, which was only ever the side effect of starting an
  off-workspace window at its real, off-screen position.
- **Two orderings that had to be right.** A close that changes workspace hands the viewport over
  **instantly** before it zooms (`set_workspace`, never `request_workspace` — otherwise vswitch
  slides the viewport while the cell zooms, which is what "being dragged sideways" was), and
  re-expresses every thumbnail against its moved geometry so nothing jumps. And because the
  camera is composed *into* each view's transformer, anything that reads a rendered transform
  back as an animation base must divide the camera out: screen space for input, base space for
  clocks.
- **A seamless wallpaper hand-off.** The per-cell tile is the largest output-aspect rect centred
  in its cell, which provably lands exactly on the output at `t = 1`, and it never fades — fading
  it blended a scaled copy of the picture over the unscaled desktop copy, a double exposure that
  read as a ghost. With a full-screen backdrop behind the grid, the teardown swaps identical
  pixels.
- **Chrome as rects.** The veil and grid stopped baking an output-sized cairo texture every frame
  (19.8 MB per rebuild) and became solid rects, which is what made per-cell alpha animatable —
  closing 002's deferred half of FR-011, where the veil's bright cell could only snap.
- **Proof.** Camera invariants #C1–#C8 in the pure suite (identity at rest, cover at full zoom,
  one scale for both axes, 1×1 degeneracy, monotonicity, clamping, round-trip), ~4.0 M assertions
  total, with 002's golden parity untouched — the camera never touches `layout()`.
- **Docs:** [spec & contracts](specs/003-spread-zoom/) ·
  [ADR-005](docs/adr/005-overview-camera-zoom.md)

### 🔅 inactive-alpha · _complete_

Dims every window except the focused one, natively — replacing an IPC helper script with an
in-tree plugin (no Python process, no socket, no autostart entry).

- **Code:** [`plugins/inactive-alpha/`](plugins/inactive-alpha/) ·
  **Docs:** [README](plugins/inactive-alpha/README.md) ·
  [spec & contracts](specs/004-inactive-alpha/)
- **The point of the rewrite:** the script decided when to stand aside by *plugin name*, so adding
  a second overview plugin silently reintroduced the bug it was written to avoid — the focused
  window appeared dimmed inside the overview until its name was added to a hardcoded list. The
  plugin asks the compositor whether **any** plugin holds the output instead, which needs no
  maintenance for plugins that do not exist yet.
- **Highlights:** pure, unit-tested decision core (~10 lines, 1250 assertions); writes the same
  per-window opacity the stock `alpha` plugin uses, so nothing can disagree about a window's
  opacity; restores exactly the windows it dimmed on unload; retains the last focused window when
  keyboard focus moves to a panel, so clicking a bar does not dim everything.
- **Status:** complete — verified on a throwaway session, including that short plugin activations
  (workspace-switch animations) cause no visible flicker.

## Optimizations

_None yet — this section will track performance / behavior tweaks to upstream code._

## Planned / ideas

_Scratchpad for future features and experiments._

## How this fork is organized

- **In-tree plugins** under `plugins/<name>/` build with the compositor (no out-of-tree ABI drift).
- **Design decisions** are recorded as ADRs in [`docs/adr/`](docs/adr/).
- **Specs & contracts** live in [`specs/`](specs/) (a lightweight spec-driven workflow).
- **Staying current with upstream:** [`docs/SYNCING-UPSTREAM.md`](docs/SYNCING-UPSTREAM.md) — the
  merge runbook, including the date-stamped plugin ABI that requires rebuilding
  `wayfire-plugins-extra` in lockstep, and how to verify no plugin was left stale.
- Kept curated on top of upstream `master`; not intended to be merged upstream as-is.
