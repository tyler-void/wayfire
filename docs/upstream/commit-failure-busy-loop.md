# Upstream report: a persistently failing output commit becomes an unbounded busy loop

Ready to file at <https://github.com/WayfireWM/wayfire/issues>. Everything below was verified
against the source in this tree; the one open question is flagged as such. Raw evidence from the
occurrence that prompted it is in `~/Documents/wayfire-dp3-hotloop-2026-10-31/` (not part of the
repo — attach selectively if the maintainers ask).

---

## Title

`render-manager`: a persistently failing output commit retries via an idle source, producing an
unbounded busy loop and an unthrottled error log

## Environment

- Wayfire `0.12.0` (master, `1b8e309e` — a fork, but see "Not fork-specific" below)
- bundled wlroots `0.20.2`
- kernel `7.2.9-zen1-1-zen`, mesa `1:26.2.4-1`, Intel TigerLake-H iGPU (i915) + NVIDIA (unused)
- Output: 3440×1440 @ 165 Hz over Thunderbolt → DisplayPort

### Not fork-specific

The fork adds plugins under `plugins/` only. Every commit touching `src/` or `subprojects/` is
upstream's own — the last five commits to `src/output/render-manager.cpp` are all yours
(`01609f08`, `6450157a`, `27090244`, `a4a8d862`, `a4df7649`). The code paths below are unmodified
upstream code.

## Summary

When `wlr_output_commit_state()` fails **persistently**, Wayfire retries immediately and forever.
The retry is armed through `wlr_output_schedule_frame()`, which — because the commit failed —
falls through to a `wl_event_loop_add_idle()` source rather than waiting for vblank. Each
iteration performs a full render pass and writes two log lines.

Measured on a live session: **~860 iterations per second** against a 165 Hz output (2586 failed
commits in a 3-second `strace -e trace=write`), saturating one CPU core and the iGPU, with the
log lines going to the controlling tty.

The trigger in our case was hardware — a DisplayPort sink vanished with a page-flip in flight, so
wlroots' safeguard at `backend/drm/drm.c:992` rejected every subsequent non-blocking commit. But
the amplification is independent of the cause: *any* permanently failing commit reaches it.

## The loop

1. `paint()` → `swap_buffers()` → `wlr_output_commit_state()` fails. wlroots logs
   `Failed to page-flip output: a page-flip is already pending` (`backend/drm/drm.c:992`).
2. `swap_buffers()` logs `Output commit failed!` and returns false
   (`src/output/render-manager.cpp:354`) — unconditionally, with no rate limit.
3. `paint()`'s failure branch (`src/output/render-manager.cpp:1543`):
   ```cpp
   if (!committed)
   {
       damage_manager->damage_whole();
       return;
   }
   ```
   `damage_whole()` is `damage_buffer(get_buffer_extents(), /*repaint=*/true)`, and that `true`
   calls `schedule_repaint()` → `wlr_output_schedule_frame()`. **The failure handler re-arms the
   frame that just failed.**
4. In wlroots (`types/output/output.c:848`):
   ```c
   void wlr_output_schedule_frame(struct wlr_output *output) {
       wlr_output_update_needs_frame(output);
       if (output->frame_pending || output->idle_frame != NULL) {
           return;
       }
       output->idle_frame = wl_event_loop_add_idle(output->event_loop,
           schedule_frame_handle_idle_timer, output);
   }
   ```
   `frame_pending` is what normally throttles a repaint request to vblank, and it is only set on
   the **success** path — a failed commit leaves it false. So the early return does not trigger
   and an **idle** source is armed.
5. The idle fires on the next event-loop pass → `wlr_output_send_frame()` → Wayfire's `on_frame`
   → `paint()`. Back to 1.

Two consequences follow from step 4, and both were observed:

- The loop runs at **event-loop rate, not refresh rate**.
- A pending idle source makes libwayland poll with a zero timeout, so the loop never blocks. Every
  other event source then competes with a full render pass per iteration, which is why IPC and
  wlr-output-management round trips timed out (`wlr-randr` hung indefinitely).

`eu-stack` samples of the main thread, ~0.7 s apart, are all in the same path:

```
wl_display_run → wl_event_loop_dispatch → wl_event_loop_dispatch_idle
  → wlr_output_send_frame
    → render_manager::impl::impl(...)::{lambda(void*)#4}
      → render_manager::impl::paint(repaint_schedule_t const&)
        ├ render_sw_cursors → wlr_render_pass_submit → wlr_egl_create_sync   (3 of 5 samples)
        └ swapchain_damage_manager_t::swap_buffers
          ├ wlr_output_test_state → drmModeAtomicCommit
          └ wf::log::log_plain → write(tty)
```

Note that a **full repaint, including software cursors, happens on every iteration even though
the commit cannot succeed**.

## Reproducing without the hardware fault

We have not built a synthetic reproducer. The loop needs only "every commit on an enabled output
fails", so forcing `wlr_output_commit_state()` to return false for an enabled output (a temporary
`return false` in the DRM backend's commit, or a connector whose CRTC allocation is lost while
enabled) should show it. Happy to test a patch against the real fault if it recurs.

## Suggested fix

The essential property: **the retry after a failed commit must go through a timer, never an idle
source.** An idle retry is unthrottleable by construction, because `frame_pending` — the only
thing that would throttle it — is by definition not set when the commit failed.

Concretely, in `swapchain_damage_manager_t` / `paint()`:

- track consecutive commit failures; reset to 0 on success;
- log `Output commit failed!` only on the first failure and then on powers of two, so a permanent
  failure does not write megabytes to the tty;
- in `paint()`'s `!committed` branch, damage **without** re-arming
  (`damage_buffer(extents, /*repaint=*/false)`) and schedule the retry on `repaint_timer` at one
  refresh interval × `min(2^(n-1), 64)`.

That keeps a transient failure recovering on the very next frame, while a permanent one decays to
roughly 2 Hz, leaving the event loop responsive — so IPC and output management keep working and
the session stays usable (and recoverable) instead of hanging.

Skipping the render entirely while a commit is known to be impossible would also help, since the
render is the expensive part of each iteration, but it is a separate optimisation and not needed
for correctness.

## Open question, for the wlroots side

Our output stayed alive with its CRTC bound for ~40 minutes after the sink vanished, with
`/sys/class/drm/card1-DP-3/status` reading `disconnected` while `enabled` stayed `enabled`. We
could not establish why.

Two things we *did* verify, which rule out the obvious explanation:

- `drm_connector_destroy_output()` clears the stuck flip **unconditionally**
  (`backend/drm/drm.c:1252`), so the teardown does not wait for a flip that will never arrive.
- `scan_drm_connectors()` does handle the CONNECTED → disconnected transition by calling
  `disconnect_drm_connector()` (`backend/drm/drm.c:1867-1871`).

So the destroy path looks correct; it appears simply never to have run. The deciding evidence
would be whether wlroots ever logged `'DP-3' disconnected` — and that line was lost, because the
session's output went only to the controlling tty and was never captured. We will capture it next
time. Flagging it here in case it is a known wlroots issue rather than something to chase.

Recovery attempts that did **not** clear it, for completeness:

| Attempt | Result |
|---|---|
| `echo detect > /sys/class/drm/card1-DP-3/status` (real kernel hotplug uevent) | main-thread CPU 61% → 20%; output still bound to a CRTC, IPC still timing out |
| VT switch away and back (session deactivate/reactivate → connector rescan) | still looping (34%), IPC still timing out |
| `wlr-randr --output eDP-1 --on` | hangs; the output-management round trip never completes |
