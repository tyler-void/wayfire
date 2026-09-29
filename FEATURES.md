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
