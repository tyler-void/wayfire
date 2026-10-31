# spread-overview

An all-workspace window overview for [Wayfire](https://github.com/WayfireWM/wayfire) that merges
the best of **expo** and **scale**: it spreads every window across a zoomed-out grid of *all*
workspaces at once, and you can **drag a window onto another workspace to relocate it**.

- **expo** side — the whole workspace grid at a glance, per-workspace dim veil, an expo-style grid
  border, and optional per-cell wallpaper tiles.
- **scale** side — each window is an individual, crisp thumbnail (not a shrunk workspace snapshot),
  so windows stay readable and directly draggable.

Inside a workspace, windows are packed into **rows of variable width** and **sized individually**:
a maximized window no longer dictates the scale of everything sharing its workspace, and short
windows get an extra nudge (`small_window_boost`) so a volume mixer next to a browser stays
recognizable. Rows follow the windows' real positions — higher windows land in earlier rows,
left-to-right order is preserved — so the overview reads as a map of your desktop. The
arrangement is a port of GNOME Shell's row layout; see
[ADR-004](../../docs/adr/004-per-window-cluster-scale.md) and the executable reference
[`layout_ref.py`](../../specs/002-spread-refine/reference/layout_ref.py), which the C++ must match
within 0.5 px.

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
| `duration` | `300ms` | Entry / reflow animation length + easing; also the glide back when a drag is cancelled |
| `exit_duration` | `225ms` | Close animation length (thumbnails + overlay dissolve). Set it to `duration` for a symmetric open/close |
| `drag_threshold` | `8` | Pointer movement (px) past which a press becomes a drag, not a click |

### Layout
| Option | Default | Meaning |
|---|---|---|
| `spacing` | `20` | Gap (px) between thumbnails within a workspace cluster |
| `small_window_boost` | `1.5` | How much the shortest windows are enlarged relative to full-height ones before packing (1.0 = off, max 4.0) |
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

Since 003 the open and the close are **one camera in two directions**, not two animations. The
camera pivots on a workspace cell and interpolates `t` between *the whole grid is visible*
(`t = 0`) and *the pivot cell fills the screen* (`t = 1`). The open runs `t: 1 → 0` over
`duration`; the close runs `t: 0 → 1` over `exit_duration` (both default `circle` ease-out).
There is **no separate option** for the zoom.

Each window takes one of two roles, derived every frame rather than stored:

- **The pivot cell's windows settle** — they interpolate between their slot and their real desktop
  position. The camera is deliberately *not* applied to them: a slot is a *packed* position, so
  zooming it would land a window where the layout put it, blown up, instead of where it lives.
- **Every other window rides the camera** — its slot rect is mapped through the camera, so it
  scales away past the screen edges on the close and arrives from beyond them on the open. This
  subsumes 001's per-cluster directional slide-in, which was only ever the side effect of starting
  an off-workspace window at its real (off-screen) position.

- **Close** — pivots on where you are going: the clicked window's workspace, the clicked empty
  cell, or the current workspace for Esc / toggle. If that is a different workspace, the viewport
  is handed over **instantly** first (`set_workspace`, so vswitch never slides against the
  camera) and every thumbnail is re-expressed against its moved geometry, so nothing jumps.
  Clicking a window or an empty cell used to tear the overview down with no animation at all;
  both are animated now.
- **Open** — pivots on the workspace you were on, so the overview pulls back from it.
- **Reflow** — after a relocate, thumbnails glide from where they were to their new slots. A
  reflow deliberately does not re-zoom.
- **Chrome** — the veil, grid and drop highlight fade as `1 - t`, so the overview dissolves as
  one piece on the same clock as the zoom. The **wallpaper is exempt and stays opaque**: fading it
  blended a scaled copy of the picture over the unscaled desktop copy of the same image, which
  read as a ghost. Instead its per-cell tile is the largest output-aspect rect centred in the
  cell, which lands exactly on the output at `t = 1`, and a full-screen backdrop keeps the live
  desktop from showing through the gaps. The hand-off to the real wallpaper is a swap of identical
  pixels.
- **Drag** — pressing a thumbnail while it is still flying in catches it exactly where it is
  drawn, mid-zoom included; the others keep going. A cancelled drop glides back to its slot
  instead of jumping, and the drop-target highlight cross-fades between workspaces over a fixed
  120 ms — **as does the veil's bright cell**, which closes 002's deferral of FR-011 (it could
  only snap while the veil was one baked texture).

## Building (in-tree)

Built in-tree against Wayfire **master (currently 0.12-dev)** and installed to `/usr/local`:

```sh
ninja -C build
sudo ninja -C build install
```

The pure `layout()` function is unit-tested with doctest (configure with `-Dtests=enabled`, then `meson test -C build`).

## Design & governance

- Implementation plans and contracts: [`specs/001-spread-overview/`](../../specs/001-spread-overview/)
  (the original feature) and [`specs/002-spread-refine/`](../../specs/002-spread-refine/) (the
  layout + motion refinement, whose contract supersedes 001's invariant #10)
- Architecture decisions: [`docs/adr/`](../../docs/adr/) — ADR-001 (self-managed drag),
  ADR-002 (wallpaper file-load), ADR-003 (snap-into-workspace), ADR-004 (per-window cluster
  scale).
- Verified Wayfire API surface: [`docs/API-MAP-verified.md`](../../docs/API-MAP-verified.md)
