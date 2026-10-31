# ADR-006: a persistently failing output commit must back off — and how this fork carries core patches

**Status:** Accepted
**Date:** 2026-10-31
**Feature:** none (infrastructure / robustness)
**Scope:** `src/output/render-manager.cpp`, i.e. an **upstream** file. This is the first time the
fork has considered editing core rather than adding a plugin, which is why there is an ADR at all.

## Context

On 2026-10-31 the tty1 session hung with one CPU core and the iGPU saturated at 90 °C. The
collected evidence is in `~/Documents/wayfire-dp3-hotloop-2026-10-31/` (strace, five `eu-stack`
samples, DRM/sysfs state, kernel journal). The G5 on DP-3 dropped off the Intel GPU with a
page-flip in flight; every subsequent commit was rejected, and Wayfire answered each rejection by
repainting immediately, producing **~860 failed commits per second** — 2586 in a 3-second strace,
each one a full render pass plus two log lines to tty1.

The incident's trigger was hardware. The amplifier was ours, and it is reproducible in principle
without any hardware fault: *any* commit that fails persistently produces the same loop.

### The loop, verified in source

1. `paint()` → `swap_buffers()` → `wlr_output_commit_state()` fails. wlroots logs
   `"Failed to page-flip output: a page-flip is already pending"`
   (`subprojects/wlroots/backend/drm/drm.c:992`, a deliberate safeguard).
2. `swap_buffers` logs `"Output commit failed!"` — unconditionally, no rate limit — and returns
   false (`src/output/render-manager.cpp:354`).
3. `paint()`'s failure path (`render-manager.cpp:1543`) calls `damage_manager->damage_whole()`,
   which is `damage_buffer(extents, /*repaint=*/true)` → `schedule_repaint()` →
   `wlr_output_schedule_frame()`. **The failure handler re-arms the frame that just failed.**
4. In wlroots, `wlr_output_schedule_frame()` returns early only `if (output->frame_pending ||
   output->idle_frame != NULL)`. `frame_pending` is the normal throttle to vblank, and it is only
   set on the **success** path — a failed commit leaves it false. So wlroots arms a
   `wl_event_loop_add_idle` source, which fires on the next event-loop pass rather than at vblank.
5. The idle calls `wlr_output_send_frame()` → Wayfire's `on_frame` → `paint()`. Back to 1.

Two consequences follow from step 4 alone, and both were observed: the loop runs at **event-loop
rate, not refresh rate** (~860 Hz against a 165 Hz output), and because a pending idle source
makes libwayland poll with a zero timeout, the loop never blocks — so every other event source is
contending with a full render pass per iteration, which is why IPC and wlr-output-management
requests timed out.

### A correction to the incident report

The report inferred that wlroots "defers the destroy of a disconnected output until its pending
page-flip completes", leaving the output alive forever. That is **not** what the source does:
`drm_connector_destroy_output()` clears it unconditionally
(`drm.c:1252`, `drm_connector_set_pending_page_flip(conn, NULL)`), and `scan_drm_connectors()`
does handle the CONNECTED → disconnected transition by calling `disconnect_drm_connector()`
(`drm.c:1867-1871`).

So why the output survived is **still open**. The deciding evidence is a single wlroots log line,
`'DP-3' disconnected` — and it was lost, because the session's log went only to tty1. That is
precisely the gap the report's own "make the next one debuggable" item names, and it is the one
action item worth doing *before* the next occurrence.

## Decision

**Three parts.**

1. **Report the Wayfire bug upstream, with the full call chain.** The write-up is in
   [`docs/upstream/commit-failure-busy-loop.md`](../upstream/commit-failure-busy-loop.md), ready to
   file. The bug stands on its own merits — unbounded retry plus an unthrottled error log on a
   permanent failure — independently of the DP-3 trigger, and is better fixed once upstream than
   carried by every fork that hits it.

2. **Do NOT patch `src/output/render-manager.cpp` in this fork, for now.** Every change so far
   lives under `plugins/<name>/` plus one appended line in `plugins/meson.build`, which is what
   makes `docs/SYNCING-UPSTREAM.md` a mechanical merge. A core patch turns every future sync into
   a conflict to re-resolve in a file we do not own and do not test. The incident is a
   single occurrence with a hardware trigger, and the cheap mitigation (capture the log) is
   not a patch at all.

3. **If it recurs, carry the patch — in the documented shape below, and amend this ADR.** The
   trigger for that is *a second occurrence*, or upstream declining the report.

### The patch shape, if it is ever needed

Not applied. Written down so it is not redesigned under pressure:

```
in swapchain_damage_manager_t: int consecutive_commit_failures = 0;

on success in swap_buffers():        consecutive_commit_failures = 0;
on failure in swap_buffers():        ++consecutive_commit_failures;
                                     log only when it is 1, or a power of two
in paint()'s !committed branch:      damage_whole() WITHOUT the repaint re-arm
                                     (damage_buffer(extents, /*repaint=*/false)),
                                     then schedule the retry on repaint_timer at
                                     one refresh interval * min(2^(n-1), 64),
                                     so a transient failure still recovers next
                                     frame and a permanent one decays to ~2 Hz.
```

The essential property is that the retry must go through a **timer**, never an idle source: an
idle retry cannot be throttled, because `frame_pending` — the only thing that would throttle it —
is by definition not set when the commit failed.

## Consequences

**Good.** Core stays untouched, so upstream syncs stay mechanical. The analysis is recorded while
the evidence is fresh, with the mechanism verified rather than inferred, so neither the next
occurrence nor the upstream discussion starts from zero. The patch shape is pre-argued.

**Bad.** A recurrence hangs the session again. Accepted deliberately: the cost is a reboot of a
single-user desktop, against the recurring cost of a core patch on every sync.

**Follow-up, not gated on upstream.** Wayfire's output went only to tty1, so the lines before the
loop — including the one that would have closed the open question above — were unrecoverable.
Logging to a file with rotation is worth doing on its own.

## Alternatives considered

- **Patch core now.** Rejected per decision 2: it buys protection against a single observed
  hardware fault at the price of permanent merge friction in an untested file.
- **Patch wlroots to throttle `wlr_output_schedule_frame`.** Rejected. The idle source is correct
  for its documented purpose (clients asking for frame callbacks without submitting a buffer); the
  defect is the caller retrying an operation that cannot succeed, not the scheduling primitive.
- **Work around it in a plugin.** Not possible. No plugin API observes commit failures, and none
  should.
- **Call it hardware and move on.** Rejected: the amplifier is reachable by any permanent commit
  failure, so the robustness bug is real whatever caused this instance.
