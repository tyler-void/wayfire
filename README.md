# Wayfire — Personal Fork

A personal fork of [Wayfire](https://github.com/WayfireWM/wayfire) — a 3D Wayland compositor
built on [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) — extended with my own
plugins and optimizations. Custom work is developed **in-tree**, spec-driven, with architecture
decision records and unit tests, and kept curated on top of upstream `master`.

> **Featured build:** `spread-overview` — an *expo × scale* hybrid window overview.

---

## ✨ spread-overview

A window overview that merges two ideas which usually live in separate plugins:

- **expo** (Compiz) — see *all* workspaces at once, as a zoomed-out grid.
- **scale** (GNOME / macOS Exposé) — see every window as an individual, crisp, directly-draggable thumbnail.

…and adds what neither does well: **drag a window from one workspace onto another to move it there**,
with the overview staying open so you can rearrange several in one pass.

<!-- Record a short screencast (open overview → drag a window across workspaces → close),
     save it as docs/media/spread-overview.gif, and uncomment:
     ![spread-overview demo](docs/media/spread-overview.gif) -->
> _Demo GIF coming — see **Recording a demo** below._

**What it does**
- Every window across every workspace, grouped into an expo-style grid with a per-workspace dim veil and optional per-cell wallpaper.
- Drag-to-relocate with a "snap the window *fully* into the target workspace" drop model.
- Correct handling of maximized / tiled / fullscreen / sticky windows.
- Entry / reflow / exit animations, including a research-driven "fade, don't fly" close.
- `super + G` to toggle; fully themeable (colors, animation, wallpaper) — see the [plugin README](plugins/spread-overview/README.md).

### Engineering highlights

The interesting work is under the hood:

| Area | What & why |
|---|---|
| **Scene-graph rendering** | Thumbnails, dim veil, wallpaper tiles and grid are custom `wf::scene` nodes with per-node opacity; the drag is a self-managed `view_2d_transformer_t` translate rather than the built-in drag engine ([ADR-001](docs/adr/001-self-managed-drag.md)). |
| **UX decided by research** | The drop model (snap-into-workspace vs. the viewport model's straddle) and the exit animation ("fade, don't fly") were chosen from researched prior art — Compiz, GNOME, macOS — and documented ([ADR-003](docs/adr/003-drop-snaps-into-workspace.md)). |
| **Graphics robustness** | Per-cell wallpaper loads the image file directly instead of capturing the live scene — deliberately avoiding cross-GPU buffer transfer on a hybrid Intel + NVIDIA setup ([ADR-002](docs/adr/002-wallpaper-file-load.md)); every decode/upload path is fail-soft and never crashes the compositor. |
| **Memory safety** | Views are non-owning `observer_ptr`s; a window closing mid-overview is scrubbed from all state *before* it is destroyed (no use-after-free), then triggers a reflow. |
| **Transaction timing** | Relocating a maximized window required re-pinning its tile state on the target workspace and reasoning about Wayfire's synchronous transaction-commit path. |
| **Tested pure core** | The layout is a pure, deterministic function unit-tested with doctest; render and hit-test share the same geometry, so what you see is exactly what receives a drop. |
| **Process** | Spec-driven (`specs/`), decisions captured as ADRs, and a risk-isolation workflow: every increment build-checks, then is verified live from a throwaway TTY before it lands. |

**Stack:** C++17 · Wayfire 0.12-dev · wlroots · Wayland · Cairo · Meson / Ninja.

**Read more:** [plugin README](plugins/spread-overview/README.md) ·
[design decisions (ADRs)](docs/adr/) ·
[spec & contracts](specs/001-spread-overview/) ·
[layout unit test](test/plugins/spread-overview-layout.cpp).

---

## Custom features in this fork

See **[FEATURES.md](FEATURES.md)** for the running index of everything this fork adds on top of
upstream Wayfire.

---

## Building

Built from source against Wayfire master; in-tree plugins (like `spread-overview`) build with the
compositor:

```sh
meson setup build
ninja -C build
sudo ninja -C build install    # installs to the configured prefix
```

Run `wayfire` from a TTY. Build the pure-layout tests with `-Dtests=enabled`, then
`meson test -C build`. Configuration lives in `~/.config/wayfire.ini` — add `spread-overview` to
the `[core] plugins` list; options are documented in the [plugin README](plugins/spread-overview/README.md).

### Recording a demo

To capture the demo GIF referenced above, record with [`wf-recorder`](https://github.com/ammen99/wf-recorder)
while you open the overview, drag a window between workspaces, and close it, then convert:

```sh
wf-recorder -f demo.mp4                 # Ctrl-C to stop
ffmpeg -i demo.mp4 -vf "fps=20,scale=960:-1" docs/media/spread-overview.gif
```

Keep it to ~5–8 seconds, then uncomment the image line in the spread-overview section above.

## About the base project

This fork is based on **[Wayfire](https://github.com/WayfireWM/wayfire)** by the Wayfire
contributors — a customizable, lightweight 3D Wayland compositor inspired by Compiz and built on
wlroots. For the full dependency list, packaging options, and upstream documentation, see the
[upstream repository](https://github.com/WayfireWM/wayfire), the
[wiki](https://github.com/WayfireWM/wayfire/wiki), and [wayfire.org](https://wayfire.org).
Licensed under the terms in [LICENSE](LICENSE).
