# spread-overview

An all-workspace window overview for [Wayfire](https://github.com/WayfireWM/wayfire) that merges
the best of **expo** and **scale**: it spreads every window across a zoomed-out grid of *all*
workspaces at once, and you can **drag a window onto another workspace to relocate it**.

- **expo** side — the whole workspace grid at a glance, per-workspace dim veil, an expo-style grid
  border, and optional per-cell wallpaper tiles.
- **scale** side — each window is an individual, crisp thumbnail (not a shrunk workspace snapshot),
  so windows stay readable and directly draggable.

## Usage

| Action | Result |
|---|---|
| `super` + `G` (default) | Toggle the overview |
| Click a thumbnail | Focus + raise that window (switching workspace if needed), then close |
| Drag a thumbnail onto another workspace cell | Move that window to that workspace (stays open to move more) |
| Click an empty workspace cell | Switch to that workspace and close |
| `Esc` / toggle again | Dismiss |

Dropping a window **snaps it fully into** the target workspace (clamped to fit; centered if it is
larger than a workspace), so "which workspace is this window on?" is always unambiguous — see
[ADR-003](../../docs/adr/003-drop-snaps-into-workspace.md). Maximized/tiled and fullscreen windows
are re-pinned onto the target workspace; sticky (on-all-workspaces) windows are left alone.

## Configuration

Set under `[spread-overview]` in `~/.config/wayfire.ini`. Colors are **`R G B A` — four** floats in
`0..1` (a missing alpha silently falls back to the default).

### Behavior
| Option | Default | Meaning |
|---|---|---|
| `toggle` | `<super> KEY_G` | Activator to open/close |
| `duration` | `300ms` | Entry / exit / reflow animation length + easing |
| `drag_threshold` | `8` | Pointer movement (px) past which a press becomes a drag, not a click |

### Layout
| Option | Default | Meaning |
|---|---|---|
| `spacing` | `20` | Gap (px) between thumbnails within a workspace cluster |
| `cluster_gap` | `0` | Gap (px) between workspace regions (`0` = edge-to-edge grid) |
| `show_ws_labels` | `false` | Draw the workspace number on each cluster |
| `include_minimized` | `false` | **Stubbed** — parsed but has no effect yet (see research.md R13) |

### Appearance
| Option | Default | Meaning |
|---|---|---|
| `background` | `0.0 0.0 0.0 1.0` | Dim-veil tint (RGB) + full-strength opacity (A) |
| `inactive_brightness` | `0.7` | Brightness of non-active workspaces (`1.0` = no dim); mirrors expo |
| `border_size` | `2` | Grid border stroke width (px) |
| `border_color` | `0.9 0.9 0.9 1.0` | Grid border color |
| `highlight_color` | `0.3 0.6 1.0 1.0` | Drop-target highlight border color (shown while dragging) |
| `highlight_size` | `4` | Drop-target highlight border width (px) |
| `wallpaper_path` | *(empty)* | Absolute path to a **PNG** drawn, scaled, into each cell below the thumbnails. Empty = disabled. Point it at the same file your wallpaper daemon uses. |

The wallpaper is loaded from the file directly (not captured from the live scene) and is fully
fail-soft — a missing/undecodable file just disables the feature. See
[ADR-002](../../docs/adr/002-wallpaper-file-load.md).

## Animations

Entry, exit, and reflow are driven by `duration` (default `circle` ease-out):
- **Entry** — thumbnails converge in from each window's real position (off-workspace windows slide
  in from their grid direction).
- **Reflow** — after a relocate, thumbnails glide from where they were to their new slots.
- **Exit** — the destination workspace's windows settle back to full size while every other
  workspace's windows fade out in place, and the veil / wallpaper / grid dissolve together (rather
  than the grid snapping away).

## Building (in-tree)

Built in-tree against Wayfire **master (currently 0.12-dev)** and installed to `/usr/local`:

```sh
ninja -C build
sudo ninja -C build install
```

The pure `layout()` function is unit-tested with doctest (configure with `-Dtests=enabled`, then `meson test -C build`).

## Design & governance

- Implementation plan and contracts: [`specs/001-spread-overview/`](../../specs/001-spread-overview/)
- Architecture decisions: [`docs/adr/`](../../docs/adr/) — ADR-001 (self-managed drag),
  ADR-002 (wallpaper file-load), ADR-003 (snap-into-workspace).
- Verified Wayfire API surface: [`docs/API-MAP-verified.md`](../../docs/API-MAP-verified.md)
