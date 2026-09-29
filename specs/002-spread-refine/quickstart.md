# Quickstart: verifying 002-spread-refine

Build/install/fallback mechanics are unchanged from
[001's quickstart](../001-spread-overview/quickstart.md) — this file adds only what 002 needs: the
new options, the per-increment tty2 gates, and how each Success Criterion is actually checked.

## Build, test, install

```bash
cd ~/Documents/Projects/wayfire
ninja -C build
meson test -C build "Spread overview layout test"     # pure core; no compositor needed
meson test -C build                                   # full suite — at I1 and I6
sudo ninja -C build install
```

After every install, run the ABI-stamp check from
[`docs/SYNCING-UPSTREAM.md`](../../docs/SYNCING-UPSTREAM.md) ("Verify before restarting"): exactly
one `plugins:` value, equal to `pluginabi` in `/usr/local/lib/pkgconfig/wayfire.pc`. 002 changes no
core header, so the stamp should stay `20260801` and `wayfire-plugins-extra` needs no rebuild — the
check is what proves that, rather than assuming it.

## Test session protocol (every tty2 gate)

1. Log in on **tty2** and run `/usr/local/bin/wayfire -d | tee /tmp/master-log`.
   **tty1 is the daily desktop — never restart it for a test.**
2. Run the numbered steps of the increment's gate.
3. To end the session: `loginctl list-sessions` → find the session whose TTY is tty2 →
   `loginctl terminate-session <id>` → confirm tty1's `wayfire` PID is unchanged.
   **Never kill by process name**: both sessions run `wayfire`.

Rollback for any increment: `git checkout -- <files>` (or `git reset --hard <last good commit>` on
`002-spread-refine`), then `ninja -C build && sudo ninja -C build install`. The Arch
`/usr/bin/wayfire` 0.10.1 remains bootable regardless (constitution Principle VIII).

## New configuration

```ini
[spread-overview]
# 002 additions
small_window_boost = 1.5   # β: 1.0 = no emphasis (pure row layout), 1.5 = GNOME default, max 4.0
exit_duration     = 225ms  # close animation; set to 300ms to restore 001 exactly

# unchanged
duration          = 300ms  # entry + reflow (the close now uses exit_duration)
spacing           = 20
```

Both take effect the next time the overview opens (or re-flows) — no compositor restart needed.

## Per-increment gates

### I1 — pure core (no plugin change)
Not a tty2 gate. Green on:
```bash
meson test -C build "Spread overview layout test"   # incl. golden parity, D4 repros, property test
meson test -C build                                  # full suite
```
Fail = any invariant assertion, or a golden rect off by > 0.5 px (SC-004).

### I2 — wiring (`natural_pos`, `small_window_boost`)
1. Recreate a reference-like workspace: one maximized window + a terminal + a small utility (volume
   mixer / calculator) on one workspace, plus windows on two other workspaces.
2. `<super>+G`. **Expect**: the small windows are clearly recognizable (compare
   `reference/layout-compare.png`, middle panel); the big window is not smaller than before;
   thumbnails sit roughly where their windows sit (higher windows in higher rows, left-to-right
   order kept). → SC-001, FR-003.
3. Set `small_window_boost = 1.0`, re-open. **Expect**: rows, but all windows in a row at one scale
   (no small-window emphasis). → FR-005, invariant #12.
4. Set `small_window_boost = 2.5`, re-open. **Expect**: small windows larger still, big one smaller.
5. Spot-check 001 behaviour: click-to-focus, drag-relocate, stay-open reflow, Esc restore.
**Failure**: any overlap, any thumbnail outside its workspace cell, a cell's position/size changing
with window count (invariant #9), or a thumbnail bigger than its real window.

### I3 — no-jump press + animated snap-back
1. `<super>+G` and **immediately** press a thumbnail that is still flying in, then drag.
   **Expect**: nothing jumps — the pressed thumbnail stops exactly where it was drawn and follows
   the pointer; the others finish their flight. → FR-009, SC-005.
2. While that drag is in progress, watch the drop-target highlight: it must mark the cell under the
   **thumbnail's centre**, and dropping must relocate to exactly that cell. → FR-006 mid-animation.
3. Drag a thumbnail and release it over **its own** workspace (and once outside every cell).
   **Expect**: it glides back to its slot over `duration`, no relocation, no fade-out. → FR-008.
4. Start a drag during the opening animation and cancel it. **Expect**: it animates to its **final**
   slot, not to the mid-flight spot where it was grabbed. → US2 scenario 3.
5. Close a dragged window from its application mid-drag. **Expect**: no crash, no ghost. → FR-013.
**Log check for SC-005**: `grep 'drag start' /tmp/master-log` — the line carries the press position,
the thumbnail's position at the press, and (at the first motion past the threshold) the pointer
displacement; the thumbnail's displacement must equal the pointer's within 1 px.
**Failure**: any visible teleport, a highlight that disagrees with where the drop lands, a
snap-back that fades the thumbnail out, or a thumbnail that returns to a mid-flight position.

### I4 — `exit_duration`
1. Open and close with defaults. **Expect**: the close is visibly quicker than the open (225 vs
   300 ms) and the thumbnails, veil, wallpaper and grid still dissolve **together**. → FR-010.
2. Set `exit_duration = 300ms`. **Expect**: 001's close exactly. → SC-006.
**Failure**: the overlay dissolve and the thumbnail settle running at different lengths.

### I5 — highlight fade (or recorded deferral)
1. Drag a thumbnail slowly across several workspace cells. **Expect**: the old cell's highlight fades
   out and the new one's fades in over ≈120 ms instead of flicking; the bright cell of the dim veil
   follows the same ramp. → FR-011.
**Failure**: flicker, a highlight that outlives the drag, or a fade that lags the drop decision.
If deferred, the reason is recorded in `tasks.md` and this gate is skipped.

### I6 — full regression + housekeeping
Re-run **all seven** of [001's acceptance scenarios](../001-spread-overview/quickstart.md#verify-the-acceptance-scenarios-spec-user-scenarios)
unchanged (spread, click-to-focus, drag-relocate, stay-open, restore, lifetime, fullscreen) →
SC-007 — explicitly including FR-012: the `inactive-alpha` helper still works and every window is
restored exactly on close. Then the remaining housekeeping (ADR-004 affected artifacts,
FEATURES.md, plugin README; the constitution amendment already landed in I1). User sign-off closes
the feature.

## Success-criteria map

| SC | Checked by | Where |
|---|---|---|
| SC-001 | golden parity numbers + I2 step 2 | unit test + tty2 |
| SC-002 | test-only 001 oracle on the fixture | unit test (I1) |
| SC-003 | reference fuzz (18,009 layouts) + the implementation's property test | `layout_ref.py` + unit test |
| SC-004 | 54 embedded golden rects, tolerance 0.5 px | unit test (I1) |
| SC-005 | I3 step 1 + the `drag start` log line | tty2 |
| SC-006 | I3 step 3 (snap-back) and I4 (close) | tty2 |
| SC-007 | 001's seven scenarios | tty2 (I6) |
