# Quickstart: verifying 003-spread-zoom

Build/install/fallback mechanics are unchanged from
[001's quickstart](../001-spread-overview/quickstart.md); the ABI-stamp check and tty2 protocol are
as in [002's](../002-spread-refine/quickstart.md). This file adds only what 003 needs.

## Build, test, install

```bash
cd ~/Documents/Projects/wayfire
ninja -C build
meson test -C build "Spread overview layout test"     # layout + camera, no compositor needed
meson test -C build                                   # full suite — at I1 and I4
sudo ninja -C build install
```

Then the ABI-stamp check from [`docs/SYNCING-UPSTREAM.md`](../../docs/SYNCING-UPSTREAM.md):
exactly one `plugins:` value, equal to `pluginabi`. 003 touches no core header, so it should stay
`20260801` and plugins-extra needs no rebuild — the check is what proves it.

> **If the build fails on `glm/vec2.hpp: No such file or directory`**, the `glm` package has been
> removed again (it is only a build dependency, so orphan cleanups take it): `sudo pacman -S glm`,
> then `meson setup build --reconfigure` — and check `buildtype` is still `debug` afterwards, since
> a reconfigure can reset it.

## Test session protocol

On **tty2**: `/usr/local/bin/wayfire -d | tee /tmp/master-log`. **tty1 is the daily desktop.**
To end a session: `loginctl list-sessions` → the tty2 session → `loginctl terminate-session <id>`,
then confirm tty1's `wayfire` PID is unchanged. **Never kill by process name.**

Rollback for any increment: `git reset --hard <last good commit>` on `003-spread-zoom`, then
`ninja -C build && sudo ninja -C build install`. The Arch `/usr/bin/wayfire` stays bootable.

## Per-increment gates

### I0 — overlays become rects (no new behaviour, except the veil fade)
1. Open the overview and **leave it still**. **Expect**: pixel-for-pixel what you see today —
   same veil darkness on inactive cells, same grid lines at the same thickness. Look especially at
   a boundary shared by two cells: it must be **one** line at normal thickness, not a doubled or
   darker one.
2. Set `cluster_gap = 40`, reopen. **Expect**: the gaps between cells are dimmed like the
   inactive cells, with **no darker cross** where a horizontal and a vertical gap meet.
3. Drag a thumbnail slowly across several cells. **Expect**: the bright cell now **fades** between
   workspaces (≈120 ms) instead of snapping — this closes 002's deferred FR-011 — and the drag
   feels at least as smooth as before.
4. Set `cluster_gap = 0` again and confirm step 1 still holds.
**Failure**: any visible change at rest, a doubled/darker shared gridline, a darker gap crossing,
or a drag that feels worse.

### I1 — pure camera (no tty2)
Green on `meson test -C build "Spread overview layout test"` and the full suite. The camera tests
assert exact identity at `t = 0`, cover at `t = 1`, preserved aspect, the 1×1 no-op, monotonicity
and clamping.

### I2 — the close zooms
1. `super+G`, then Esc. **Expect**: the grid, veil and wallpaper **grow** toward the current
   workspace's cell and fade; that workspace's windows settle to their real positions with no
   jump at the end; other workspaces' thumbnails scale away off-screen. → SC-001, SC-002.
2. Open, then click a window belonging to **another** workspace. **Expect**: the zoom converges on
   **that** workspace's cell and you land there. → SC-004, 5/5 attempts.
3. Open, then click an **empty** cell. **Expect**: the zoom converges on that cell and you land
   there.
4. Open, then toggle with `super+G`. **Expect**: same as Esc.
5. Set `vwidth = 1`, `vheight = 1` temporarily, open and close. **Expect**: no zoom, clean
   settle/fade, no glitch. Restore 3×3 afterwards.
6. Set `exit_duration = 1000ms` to watch the motion slowly, then put it back to `225ms`.
**Failure**: lines fading at a fixed size instead of growing, a visible settle *after* the zoom,
the zoom converging on the wrong cell, or anything stretched.

### I3 — the open mirrors it
1. From a workspace with windows on others, `super+G`. **Expect**: the motion starts with your
   workspace filling the screen and pulls back to the whole grid; other workspaces' thumbnails
   scale in from beyond the screen edges; nothing jumps at the end. → US2.
2. Open from each of three different workspaces. **Expect**: the motion always starts from the one
   you were on.
3. **Mid-zoom grab** (this is the 002 guarantee under a moving camera): open and **immediately**
   press a thumbnail that is still flying in, then drag. **Expect**: the thumbnail visibly under
   the pointer is the one grabbed, it stops where it is drawn, the drop lands on the highlighted
   cell. → SC-005; `grep 'drag start' /tmp/master-log` for the 1 px check.
4. Close a window from its app during both the open and the close. **Expect**: no crash, no ghost.
**Failure**: any thumbnail travelling independently of the camera other than your own workspace's,
a trace of the old directional slide-in, or a mid-zoom press grabbing the wrong window.

### I4 — regression + housekeeping
Re-run all seven [001 acceptance scenarios](../001-spread-overview/quickstart.md) unchanged
(SC-006), including FR-014: the inactive-window dimming helper still behaves and every window is
restored exactly on close. Then 20 consecutive open/close cycles mixing all four dismissal routes
(SC-008).

## Success-criteria map

| SC | Checked by | Where |
|---|---|---|
| SC-001 | I2 step 1 | tty2 |
| SC-002 | I2 step 1 (no settle after the zoom) | tty2 |
| SC-003 | I2 step 6 / I3 with a long duration | tty2 |
| SC-004 | I2 step 2, 5 attempts | tty2 |
| SC-005 | I3 step 3 + the `drag start` log line | tty2 |
| SC-006 | 001's seven scenarios | tty2 (I4) |
| SC-007 | I0 step 3 + I2/I3 feel; no 19.8 MB uploads left in the drag path | tty2 + code |
| SC-008 | I4, 20 cycles | tty2 |
