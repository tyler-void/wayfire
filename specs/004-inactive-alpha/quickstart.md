# Quickstart: verifying 004-inactive-alpha

Build/install/fallback mechanics and the tty2 protocol are as in
[002's quickstart](../002-spread-refine/quickstart.md).

## Build, test, install

```bash
cd ~/Documents/Projects/wayfire
ninja -C build
meson test -C build "Inactive alpha policy test"   # pure policy; no compositor needed
meson test -C build                                 # full suite — at J1 and J3
sudo ninja -C build install
```

Then the ABI-stamp check from [`docs/SYNCING-UPSTREAM.md`](../../docs/SYNCING-UPSTREAM.md): one
`plugins:` value equal to `pluginabi`. 004 adds a plugin but touches no core header, so the stamp
stays `20260801` — and being in-tree, the new plugin can never be stale.

> If the build fails on `glm/vec2.hpp`, the `glm` package was removed again: `sudo pacman -S glm`,
> then `meson setup build --reconfigure`, and check `buildtype` is still `debug`.

## Enabling it

```ini
[core]
plugins = … inactive-alpha      # add to the list

[inactive-alpha]
dim_alpha = 0.85                # opacity of non-focused windows; 1.0 disables dimming
```

## Per-increment gates

### J1 — governance + pure policy (no tty2)
Green on `meson test -C build` (full suite). The policy tests assert: the focused window is never
dimmed, suppression beats everything, out-of-range values clamp, and the function is total when
nothing is focused.

### J2 — the plugin (the script must be stopped first)
```sh
pkill -f inactive-alpha.py        # stop the old daemon for this session only
```
1. Focus each of three windows in turn. **Expect**: exactly one opaque, the others dimmed, every
   time. → SC-001
2. Open a new window. **Expect**: it is opaque, the previously focused one dims.
3. **The bug that motivated this**: open the overview (`super+G`). **Expect**: **every** thumbnail
   fully opaque, including the one that had focus. Close it → dimming returns. Repeat 5×. → SC-002
4. Do the same with `scale` and `expo`. **Expect**: identical — no plugin names are involved.
5. Click a panel / taskbar so keyboard focus leaves every window. **Expect**: the last focused
   window **stays opaque**; nothing dims wholesale. → FR-008
6. Change `dim_alpha` to `0.5`, then `1.0`, in the config. **Expect**: open windows change
   immediately, no restart; `1.0` leaves everything opaque. → SC-003
7. Remove `inactive-alpha` from the plugin list. **Expect**: every window returns to full opacity
   with none stranded. → SC-004
8. **Watch for the known risk**: switch workspaces a few times (`super+1/2/3`). If windows visibly
   brighten and re-dim on each switch, that is `vswitch` counting as a suppressing plugin — report
   it and the fallback is a debounce, **not** a plugin-name list.
9. Close a window while it is dimmed, and while the overview is open. **Expect**: no crash, nothing
   left behind.

**Failure**: a window dimmed inside any overview, a window stranded dim after disabling, a
wholesale dim when focus leaves to a panel, or a crash.

### J3 — retire the script
1. Remove the `[autostart]` line that launches `inactive-alpha.py` and enable the plugin in
   `wayfire.ini`.
2. Log out and back in (fresh session). **Expect**: `pgrep -f inactive-alpha.py` finds **nothing**,
   and J2 steps 1–3 still pass. → SC-005
3. 50 focus changes mixed with opening/closing the overview. **Expect**: no window stranded at the
   wrong opacity. → SC-006
4. Leave the desktop idle with focus unchanged. **Expect**: no additional repainting attributable
   to this plugin. → SC-007

**Rollback**: re-add the `[autostart]` line and remove `inactive-alpha` from the plugin list — the
script is still in `docs/ipc-scripts/` and at `~/.config/ipc-scripts/`, so the old behaviour is one
config edit away.

## Success-criteria map

| SC | Checked by |
|---|---|
| SC-001 | J2 step 1 |
| SC-002 | J2 steps 3–4 (5 attempts) |
| SC-003 | J2 step 6 |
| SC-004 | J2 step 7 |
| SC-005 | J3 step 2 |
| SC-006 | J3 step 3 |
| SC-007 | J3 step 4 |
