<!-- SPECKIT START -->
Active feature: **003-spread-zoom** (branch `003-spread-zoom`).
Read the implementation plan first: `specs/003-spread-zoom/plan.md`
(with `spec.md`, `design-notes.md`, `research.md` R-201…R-208, `data-model.md`,
`contracts/camera.md` — NORMATIVE —, and `quickstart.md` for the per-increment tty2 gates).

Shipped and merged into `master`: **001-spread-overview**, **002-spread-refine**,
**004-inactive-alpha** (native inactive-window dimming, which retired the IPC daemon). Their
artifacts are **append-only**; 002 superseded 001 layout invariant #10, and 003 supersedes only the
two *animations* (001's directional entry, 002's fixed-size exit fade) — no layout contract changes.

Governance: `.specify/memory/constitution.md` — now **v1.4.0** (the scope line governs every in-tree
plugin of this fork, not just spread-overview; Principle V's coexistence clause points at the native
`inactive-alpha` plugin). 003's plan was written against v1.3.0: its Constitution Check still holds,
but read Principle V's referent as the plugin, not the daemon.
Architecture decisions: `docs/adr/` (001–005). Verified API surface: `docs/API-MAP-verified.md`.
Upstream sync: `docs/SYNCING-UPSTREAM.md`.

Key constraints: build **in-tree on Wayfire master (0.12-dev, ABI `2026'08'01`)** under
`plugins/<name>/` — a new directory plus one appended line in `plugins/meson.build`, never a new
file inside an upstream directory; installed to `/usr/local`; keep the Arch 0.10.1 package as an
untouched TTY fallback; C++17; the pure core carries doctests (`-Dtests=enabled`) — `layout()` must
still match `specs/002-spread-refine/reference/layout_ref.py` within 0.5 px, and the camera must
satisfy `contracts/camera.md` #C1–#C7.

Measuring render performance: `tools/render-bench/` — read its README before drawing any conclusion.
`paint-budget` is a LEARNED estimate (ramps and decays; never compare two sequential windows — use
`ab` mode), `successful-presentations` is ROLLING and can decrease, and GPU busy % can read low
while every frame misses its deadline.

Gotchas worth not rediscovering: window opacity lives in the `"alpha"`-named
`view_2d_transformer_t` at `wf::TRANSFORMER_2D` — shared by the stock `alpha` plugin, the
`inactive-alpha` plugin and spread-overview's save/restore, so last writer wins.
`focus_raise_view(view, /*allow_switch_ws=*/true)` does NOT switch workspace from an overview
(`ensure_visible` bails once ≥10% of the view is on screen) — resolve the workspace explicitly like
`scale` does. A missing `glm` package breaks every build with `glm/vec2.hpp: No such file or
directory`; `meson setup build --reconfigure` can silently reset `buildtype` to release.

Each increment stops at a clean build + a tty2 test plan and waits for the user's result. Never
push without asking; never commit to `master` while a feature branch is open.
<!-- SPECKIT END -->
