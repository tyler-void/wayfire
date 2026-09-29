# ADR-004: within a workspace cluster, size each thumbnail individually (GNOME row layout)

**Status:** Accepted
**Date:** 2026-09-29
**Feature:** 002-spread-refine
**Amends:** 001 decision R14 (`specs/001-spread-overview/research.md`) — its second half only — and
supersedes 001 contract invariant #10 (`specs/001-spread-overview/contracts/layout.md`). R14's
first half (fixed-equal cluster regions, invariant #9) is **kept unchanged**.

## Context

R14 chose two things: (1) every workspace gets a fixed, equal cell of the grid, independent of how
many windows it holds; (2) inside a cell, all windows share **one uniform scale** — the largest at
which every member fits an equal slot.

(1) is what makes the overview legible as a map of the workspace grid, and daily use has confirmed
it. (2) turned out to be the wrong trade: the largest window in a cell dictates the scale for every
window in it. On the user's 3440×1440 / 3×3 setup, a maximized browser in a 355.6-px slot forces
`s = 0.103`, so a volume mixer sharing its workspace renders at 64×48 px and a notification at
80×42 — unrecognizable. The user reported exactly this: "the big one is barely usable, the small
ones are tiny".

## Decision

Replace the within-cluster packing with a port of **GNOME Shell's `UnalignedLayoutStrategy`**
(`js/ui/workspace.js`, main @ `0062bde`, 2026-09-27):

- Windows are packed into **rows of variable width** (each window keeps its own width), rows chosen
  by the windows' vertical position, order within a row by horizontal position (ties by id).
- The row count is chosen by GNOME's search (`_createBestLayout`: layout scale weighted 1, filled
  space weighted 0.1).
- Each window gets a **per-window scale**: one layout scale × an emphasis for short windows
  (`lerp(β, 1, h / output_h)`) × a per-row fit factor, capped at `max_scale`.
- The emphasis `β` is a new option, `small_window_boost` (default 1.5 = GNOME's constant; 1.0 = off).

Deliberate deviations from GNOME (details in `specs/002-spread-refine/design-brief.md §A.3`): D1 empty
rows dropped; D2 a row's height counts only its own windows; D3 the emphasis ratio is clamped to
[0, 1]; D4 row counts whose spacing alone cannot fit are skipped, with a zero-spacing fallback
(GNOME's rule alone can produce zero or negative sizes, and a division by zero in very dense
clusters).

An executable reference (`specs/002-spread-refine/reference/layout_ref.py`) is the oracle; the C++
must match its golden fixture within 0.5 px.

## Consequences

**Upsides (measured on the golden fixture):**
- Every thumbnail in a crowded workspace roughly doubles or better: the browser 356×149 → 649×272,
  the volume mixer 64×48 → 157×116, the smallest thumbnail area 3,047 → 9,427 px² (3.1×).
- No fixture window is smaller than under 001 at β ∈ {1.0, 1.5, 2.5}.
- Thumbnails keep their windows' rough spatial arrangement, which also shortens entry/exit travel.
- Regions (invariant #9), zero-overlap (#3), drop resolution and determinism are unchanged.

**Costs:**
- **Not "never worse than 001" in general.** Emphasis is keyed to height; in width-bound regions it
  can shrink siblings, and GNOME's row-count objective is not "maximize the smallest thumbnail".
  In random samples (one generator and seed each, not bounds): on the target geometry the smallest
  thumbnail got smaller than 001 in 0 / 1 / 24 of 5,000 clusters at β = 1.0 / 1.5 / 2.5 (worst
  0.96× at the default); in a broad sample, restricted to clusters where 001 was legible (smallest
  side ≥ 16 px), in 1 / 8 / 25 of 1,523 (worst 0.71× at the default).
- A larger algorithm than equal slots (row search, three nested fits) — mitigated by keeping it pure,
  golden-tested and fuzzed (0 violations in 18,009 random and degenerate layouts).
- The layout input gains the window's position (`natural_pos`) — a change to constitution
  Principle I's signature contract (MINOR amendment).
- A new option (`small_window_boost`) in the plugin's configuration surface.

**Revisit if:** a workspace in daily use shows a thumbnail noticeably smaller than 001 would have
produced — add a per-cluster fallback to the 001 packing when its smallest thumbnail is larger, or
switch the emphasis to the binding dimension (width or height) of the region.

## Affected artifacts
- `plugins/spread-overview/src/layout.hpp`, `layout.cpp` — types (`natural_pos`,
  `small_window_boost`) and the packing.
- `plugins/spread-overview/src/render.cpp` — `natural_pos` input, option wiring.
- `metadata/spread-overview.xml` — `small_window_boost`.
- `test/plugins/spread-overview-layout.cpp` — golden parity, degenerate cases, randomized properties.
- `specs/002-spread-refine/contracts/layout.md` — supersedes 001 invariant #10.
- `.specify/memory/constitution.md` — Principle I signature contract (MINOR).
- *(Update this list to what was actually built before closing 002.)*
