<!-- SPECKIT START -->
Active feature: **003-spread-zoom** (branch `003-spread-zoom`).
Read the implementation plan first: `specs/003-spread-zoom/plan.md`
(with `spec.md`, `design-notes.md`, `research.md` R-201…R-208, `data-model.md`,
`contracts/camera.md` — NORMATIVE —, and `quickstart.md` for the per-increment tty2 gates).

Shipped and merged into `master`: **001-spread-overview**, **002-spread-refine**. Their artifacts
(`specs/001-*`, `specs/002-*`) are **append-only**; 002 superseded 001 layout invariant #10, and
003 supersedes only the two *animations* (001's directional entry, 002's fixed-size exit fade) —
no layout contract changes.

Governance: `.specify/memory/constitution.md` (**v1.3.0**). Architecture decisions: `docs/adr/`
(001 self-managed drag · 002 wallpaper file-load · 003 snap-into-workspace · 004 per-window cluster
scale · **005 overview camera zoom**). Verified Wayfire API surface: `docs/API-MAP-verified.md`.
Upstream sync: `docs/SYNCING-UPSTREAM.md`.

Key constraints: build **in-tree on Wayfire master (0.12-dev, ABI `2026'08'01`)** under
`plugins/<name>/`, installed to `/usr/local`; keep the Arch 0.10.1 package as an untouched TTY
fallback; C++17; the pure core is unit-tested (doctest, `-Dtests=enabled`) — `layout()` must still
match `specs/002-spread-refine/reference/layout_ref.py` within 0.5 px, and the camera must satisfy
`contracts/camera.md` #C1–#C7. The drag is self-managed (`view_2d_transformer_t` translate, NOT
`core_drag_t` — ADR-001).

Gotchas worth not rediscovering: `focus_raise_view(view, /*allow_switch_ws=*/true)` does NOT switch
workspace from an overview (`ensure_visible` bails once ≥10% of the view is on screen) — resolve the
workspace explicitly like `scale` does. A missing `glm` package breaks every build with
`glm/vec2.hpp: No such file or directory`; `meson setup build --reconfigure` can silently reset
`buildtype` to release.

Each increment stops at a clean build + a tty2 test plan and waits for the user's result. Never
push without asking; never commit to `master` while a feature branch is open.
<!-- SPECKIT END -->
