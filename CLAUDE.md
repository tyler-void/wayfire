<!-- SPECKIT START -->
Active feature: **spread-overview** (branch `001-spread-overview`).
Read the implementation plan first: `specs/001-spread-overview/plan.md`
(with `research.md`, `data-model.md`, `contracts/`, `quickstart.md`).

Governance: `.specify/memory/constitution.md` (v1.1.1). Architecture decisions: `docs/adr/`.
Verified Wayfire API surface: `docs/API-MAP-verified.md`.

Key constraints: build **in-tree on Wayfire master (0.11-dev)** under `plugins/spread-overview/`,
installed to `/usr/local`; keep the Arch 0.10.1 package as an untouched TTY fallback; C++17;
pure `layout()` is unit-tested (doctest, `-Dtests=true`); the drag is self-managed
(`view_2d_transformer_t` translate, NOT `core_drag_t` — ADR-001).
<!-- SPECKIT END -->
