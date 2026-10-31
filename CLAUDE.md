<!-- SPECKIT START -->
**No active feature.** All four are shipped and merged into `master`: **001-spread-overview**,
**002-spread-refine**, **003-spread-zoom**, **004-inactive-alpha**. Their artifacts are
**append-only**; 002 superseded 001 layout invariant #10, and 003 superseded only the two
*animations* (001's directional entry, 002's fixed-size exit fade) — no layout contract changed.

003 in one line: the overview's open and close are **one camera in two directions**, pivoting on
the workspace you are going to (close) or came from (open). Read `specs/003-spread-zoom/plan.md`
with `contracts/camera.md` (NORMATIVE, #C1–#C8) and `research.md` R-201…**R-211**. The three that
cost the most to learn: **R-209** the viewport switch must be `set_workspace`, never
`request_workspace`, or vswitch slides the viewport while the cell zooms; **R-210** a close that
changes workspace must hand the viewport over *before* it zooms, because `set_workspace` moves
every non-sticky view and "settle to identity" only means "arrive on screen" once the viewport is
already there; **R-211** the camera is composed *into* each view's transformer, so anything that
reads a rendered transform back as an animation base must divide it out — screen space for input,
base space for clocks.

Known issues: `docs/known-issues/` — currently one open, an intermittent leftover line after a
`grid` move. Leading hypothesis is a buffer-age damage bug (it *blinks*, and a capture taken while
it was visible came back clean), unconfirmed; read that file before re-investigating, since it
records four hypotheses already ruled out with source evidence.

Governance: `.specify/memory/constitution.md` — now **v1.4.0** (the scope line governs every in-tree
plugin of this fork, not just spread-overview; Principle V's coexistence clause points at the native
`inactive-alpha` plugin). 003's plan was written against v1.3.0: its Constitution Check still holds,
but read Principle V's referent as the plugin, not the daemon.
Architecture decisions: `docs/adr/` (001–006; **006** is the first that concerns core rather
than a plugin — it decides NOT to patch `src/` for the commit-failure busy loop, and records
the pre-argued patch shape in case it recurs. Upstream reports live in `docs/upstream/`).
Verified API surface: `docs/API-MAP-verified.md`.
Upstream sync: `docs/SYNCING-UPSTREAM.md`.

Key constraints: build **in-tree on Wayfire master (0.12-dev, ABI `2026'08'01`)** under
`plugins/<name>/` — a new directory plus one appended line in `plugins/meson.build`, never a new
file inside an upstream directory; installed to `/usr/local`; keep the Arch 0.10.1 package as an
untouched TTY fallback; C++17; the pure core carries doctests (`-Dtests=enabled`) — `layout()` must
still match `specs/002-spread-refine/reference/layout_ref.py` within 0.5 px, and the camera must
satisfy `contracts/camera.md` #C1–#C8 (#C8, the inverse, was added in I3 — the camera is composed
into each view's transformer, so anything reading a rendered transform back as an animation base
must divide it out: screen space for input, base space for clocks).

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
