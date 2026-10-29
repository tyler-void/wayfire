<!-- SPECKIT START -->
Active feature: **004-inactive-alpha** (branch `004-inactive-alpha`, based on `master`).
Read the implementation plan first: `specs/004-inactive-alpha/plan.md`
(with `spec.md`, `research.md` R-301…R-305, `data-model.md`, `contracts/policy.md` — NORMATIVE —,
`quickstart.md` for the per-increment tty2 gates, and `tasks.md` K001–K024).

Also in flight, on its own branch: **003-spread-zoom** (`specs/003-spread-zoom/`, plan + ADR-005)
— the overview's camera zoom. It touches no file 004 touches, so the two are independent.

Shipped and merged into `master`: **001-spread-overview**, **002-spread-refine**. Their artifacts
are **append-only**.

Governance: `.specify/memory/constitution.md` (v1.3.0 → **v1.4.0 MINOR in J1**: the scope line
widens from `spread-overview` to the fork's in-tree plugins, and Principle V's "coexist with the
`inactive-alpha` pywayfire daemon" clause repoints at the native plugin that replaces it).
Architecture decisions: `docs/adr/` (001–005). Verified API surface: `docs/API-MAP-verified.md`.
Upstream sync: `docs/SYNCING-UPSTREAM.md`.

Key constraints: build **in-tree on Wayfire master (0.12-dev, ABI `2026'08'01`)** under
`plugins/<name>/` — a new directory plus one appended line in `plugins/meson.build`, never a new
file inside an upstream directory; installed to `/usr/local`; keep the Arch 0.10.1 package as an
untouched TTY fallback; C++17; the pure core carries doctests (`-Dtests=enabled`).

Gotchas worth not rediscovering: window opacity lives in the `"alpha"`-named
`view_2d_transformer_t` at `wf::TRANSFORMER_2D` — shared by the stock `alpha` plugin, the IPC
method, and spread-overview's save/restore, so last writer wins.
`focus_raise_view(view, /*allow_switch_ws=*/true)` does NOT switch workspace from an overview
(`ensure_visible` bails once ≥10% of the view is on screen) — resolve the workspace explicitly like
`scale` does. A missing `glm` package breaks every build with `glm/vec2.hpp: No such file or
directory`; `meson setup build --reconfigure` can silently reset `buildtype` to release.

Each increment stops at a clean build + a tty2 test plan and waits for the user's result. Never
push without asking; never commit to `master` while a feature branch is open.
<!-- SPECKIT END -->
