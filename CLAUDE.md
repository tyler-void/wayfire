<!-- SPECKIT START -->
Last feature: **spread-overview** — complete, merged into `master` (branch `001-spread-overview`).
Its plan: `specs/001-spread-overview/plan.md`
(with `research.md`, `data-model.md`, `contracts/`, `quickstart.md`).

Governance: `.specify/memory/constitution.md` (v1.2.0). Architecture decisions: `docs/adr/`.
Verified Wayfire API surface: `docs/API-MAP-verified.md`. Upstream sync: `docs/SYNCING-UPSTREAM.md`.

Key constraints: build **in-tree on Wayfire master (currently 0.12-dev)** under `plugins/<name>/`,
installed to `/usr/local`; keep the Arch 0.10.1 package as an untouched TTY fallback; C++17;
pure `layout()` is unit-tested (doctest, `-Dtests=enabled`); the drag is self-managed
(`view_2d_transformer_t` translate, NOT `core_drag_t` — ADR-001).
<!-- SPECKIT END -->
