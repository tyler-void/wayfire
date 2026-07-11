# Quickstart: building, running, and testing spread-overview

Target: Wayfire **master** built in-tree and installed to `/usr/local` (constitution Principle VIII).
The Arch 0.10.1 package stays installed as a TTY fallback — do not purge it.

## Build & install

```bash
cd ~/Documents/Projects/wayfire
ninja -C build                 # builds the compositor + the new in-tree plugin
sudo ninja -C build install    # installs to /usr/local (needs your password)
```

The plugin `.so` lands at `/usr/local/lib/wayfire/libspread-overview.so` and its metadata at
`/usr/local/share/wayfire/metadata/spread-overview.xml`.

## Enable it

In `~/.config/wayfire.ini`:

```ini
[core]
plugins = ... spread-overview        # append to your existing plugin list

[spread-overview]
toggle = <super> KEY_G
# spacing, cluster_gap, drag_threshold, background, show_ws_labels,
# close_on_bg_click, duration, include_minimized  — see contracts/config-options.md
```

Restart into master (`/usr/local/bin/wayfire`, resolved first on PATH) from a TTY, or reload config.

## Verify the acceptance scenarios (spec §User Scenarios)

1. **Spread (US1)**: open windows on ≥3 workspaces incl. one maximized → press `<super>+G` →
   all visible, none overlapping, grouped + labeled by workspace. (SC-001, SC-005)
2. **Click-to-focus**: click a thumbnail (no drag) → it focuses and the overview closes. (SC-006)
3. **Drag-relocate (US2)**: press-hold a thumbnail, drag onto another workspace's cluster, release →
   close overview, switch to that workspace → the window is there; source workspace → gone. (SC-003)
4. **Stay open (US3)**: after a relocate the overview remains open and the moved window sits under its
   new cluster; move a second window in the same session.
5. **Restore**: close via Esc → every window back to its exact prior position/size/stacking/opacity.
   (SC-004)
6. **Lifetime**: close a window (from its app) while the overview is open → no crash, no ghost
   thumbnail. (SC-007)
7. **Fullscreen**: fullscreen a window, open the overview → it appears scaled in place; relocate it →
   it is fullscreen on the target workspace.

Debugging: launch `wayfire -d` from a TTY (or a nested `WLR_BACKENDS=headless wayfire -c cfg.ini` for
smoke tests) and watch the log.

## Run the pure-layout unit tests

```bash
meson configure build -Dtests=enabled   # feature option (enabled/disabled/auto), not =true; needs the `doctest` package
ninja -C build
meson test -C build "Spread overview layout test"
```

The layout tests assert the invariants in `contracts/layout.md` (cluster coverage, grouping, zero
overlap, hit-test round-trip, determinism) with no compositor running.

## Keeping out-of-tree plugins in sync (constitution Principle VIII)

`spread-overview` is in-tree, so it rebuilds with the compositor. But **out-of-tree** plugins you
depend on — `wayfire-plugins-extra` (`follow-focus`, `focus-change`) — link the master ABI
(`2026'07'09`) and must be rebuilt against the master install and reinstalled to `/usr/local`
**every time master is rebuilt** (the ABI date can bump). In the plugins-extra clone:

```bash
# wayfire.pc must resolve to the /usr/local (master) install, not the Arch package
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig meson setup build --prefix=/usr/local
meson install -C build
```

The Arch-packaged 0.10.x copies in `/usr/lib/wayfire` are ABI-incompatible with master and are
ignored by it — harmless, but don't rely on them.

## Fallback

If master won't start, from a TTY run `/usr/bin/wayfire` (the untouched Arch 0.10.1 package).
