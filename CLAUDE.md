<!-- SPECKIT START -->
Active feature: **002-spread-refine** (branch `002-spread-refine`).
Read the implementation plan first: `specs/002-spread-refine/plan.md`
(with `spec.md`, `design-brief.md` — NORMATIVE where marked —, `research.md`, `data-model.md`,
`contracts/layout.md`, `quickstart.md`, and the oracle `reference/layout_ref.py`).

Shipped: **001-spread-overview** — complete, merged into `master`; its artifacts
(`specs/001-spread-overview/`) are **append-only**. 002 supersedes only 001 layout invariant #10.

Governance: `.specify/memory/constitution.md` (v1.2.0 → **v1.3.0 MINOR in I1**: Principle I gains
`natural_pos`; Principle VIII facts refreshed). Architecture decisions: `docs/adr/` (004 = per-window
cluster scale). Verified Wayfire API surface: `docs/API-MAP-verified.md`.
Upstream sync: `docs/SYNCING-UPSTREAM.md`.

Key constraints: build **in-tree on Wayfire master (0.12-dev, ABI `2026'08'01`)** under
`plugins/<name>/`, installed to `/usr/local`; keep the Arch 0.10.1 package as an untouched TTY
fallback; C++17; the pure `layout()` is unit-tested (doctest, `-Dtests=enabled`) and must match
`reference/layout_ref.py` within 0.5 px; the drag is self-managed (`view_2d_transformer_t`
translate, NOT `core_drag_t` — ADR-001). Each increment stops at a clean build + a tty2 test plan
and waits for the user's result; never push or commit to master.
<!-- SPECKIT END -->
