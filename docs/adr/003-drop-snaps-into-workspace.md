# ADR-003: an overview drop snaps a window fully into the target workspace (discrete model)

**Status:** Accepted
**Date:** 2026-07-13
**Feature:** 001-spread-overview
**Amends:** ADR-001 §Decision — the drop is no longer a bare `move_to_workspace(view, target_ws)`.

## Context

Wayfire (Compiz lineage) uses a **viewport** virtual-desktop model: all workspaces form one
continuous coordinate plane, each window has an absolute position, and a window can be positioned to
**straddle** two (or four, at a grid corner) adjacent workspaces at once. This differs from the
discrete model (GNOME/KDE/i3/sway) where a window belongs to exactly one workspace.

Core's `wf::workspace_set_t::move_to_workspace()` only **guarantees partial visibility**: if the
window already intersects the target workspace's region it does **nothing** (`if (!(box & visible))`
in `workspace-impl.cpp`), preserving the straddle. Compiz Expo behaves identically — a dropped
window stays wherever it lands, straddle and all. This is the deliberate viewport convention.

Our spread, however, presents a **discrete UI**: one thumbnail per window (placed in its center's
workspace via `get_view_main_workspace`), one cluster per workspace, and **no** continuous straddle
rendering (ruled out as not viable — a thumbnail lives in one cluster). The UI promises "drop = put
it here." Two bugs surfaced from that mismatch:

1. A **maximized** (tiled-on-all-edges) window's plain `move_to_workspace` was silently reverted by
   the tile/maximize constraint — it never left its source workspace (the log showed repeated
   identical relocations because `get_view_main_workspace` kept reporting the old workspace).
2. A **straddling floating** window could not be dropped onto a workspace it already partially
   overlapped (the overlap guard no-op'd) — "you can only move it to the workspaces it isn't
   already touching."

Notably, even Compiz separates the two verbs: **Expo drag preserves position; a separate "Put on
Viewport N" command moves a window fully into a viewport.** Our drag-to-relocate is semantically
"Put," not "Expo drag."

## Decision

Adopt the discrete **"snap fully in"** guarantee on drop, resolved per window class in
`relocate()` / `snap_into_workspace()` (`move.cpp`):

- **Floating** → compute the destination position directly and **guard-free**: preserve the window's
  in-cell offset (modular arithmetic, like core's `cx % width`), then **clamp** so a window that
  fits lands entirely inside the target cell; **center** it if it is larger than a workspace.
- **Maximized / tiled / fullscreen** → re-pin onto the target with
  `tile_request` / `fullscreen_request(target_ws)` (a plain move is reverted by the constraint),
  then `move_to_workspace` as a visibility guarantee.
- **Sticky / on-all-workspaces** → **no-op**; the drop snaps the thumbnail back via reflow.

We **deliberately diverge** from the viewport straddle-preserving convention: a discrete UI must
make a discrete promise.

## Consequences

**Upsides:**
- "Which workspace is this window on?" is unambiguous after a drop; every reachable cell accepts a
  drop; behavior matches the thumbnail UI's implied semantics (the GNOME/KDE guarantee).
- Maximized windows — the common case here (a window-rule maximizes them) — relocate reliably.

**Costs:**
- Diverges from Expo / vswitch; a Wayfire native might expect the straddle preserved.
- A window larger than a workspace cannot be fully contained (centered, best effort).
- Moves the real geometry slightly more than the viewport convention would (the clamp), a larger
  position change for a straddling window.
- Relies on `move()` / `tile_request` committing synchronously enough for the immediate reflow —
  verified: `consider_commit()` runs in-stack for position-only changes
  (`transaction-manager-impl.hpp`).

**Revisit if:** users want the viewport convention back — add an opt-in `snap_into_workspace = false`
config option to restore straddle-preserving `move_to_workspace` semantics (the option the straddle
research recommended).

## Affected artifacts
- `plugins/spread-overview/src/move.cpp` — `relocate()` + `snap_into_workspace()`; `overview.hpp` decl.
- Amends **ADR-001** §Decision (drop is no longer a bare `move_to_workspace`).
- `specs/001-spread-overview/tasks.md` — closes most of the relocate edge-case task (T029).
- Straddle-model research (viewport vs discrete; Compiz Expo vs "Put"; GNOME/KDE) informing this.
