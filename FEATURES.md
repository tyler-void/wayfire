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

## Optimizations

_None yet — this section will track performance / behavior tweaks to upstream code._

## Planned / ideas

_Scratchpad for future features and experiments._

## How this fork is organized

- **In-tree plugins** under `plugins/<name>/` build with the compositor (no out-of-tree ABI drift).
- **Design decisions** are recorded as ADRs in [`docs/adr/`](docs/adr/).
- **Specs & contracts** live in [`specs/`](specs/) (a lightweight spec-driven workflow).
- Kept curated on top of upstream `master`; not intended to be merged upstream as-is.
