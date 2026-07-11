# API Map — verified against this checkout (SPEC §3 gate deliverable)

**Verified against:** commit `8603d187`, meson `0.11.0` (Wayfire master / 0.11-dev).
**Target build (locked):** Wayfire **master built from this checkout** → installs to `/usr/local`.
**wlroots 0.20.1** and **wf-config 0.11.0** are provided by meson **subprojects** (git submodules in
`subprojects/`), chosen via `use_system_wlroots=auto` (`meson.build:60-74`) because no system
`wlroots-0.20` exists on this machine (verified: no `/usr/lib/libwlroots-0.20*`, no `wlroots0.20`
package, `pkg-config --exists wlroots-0.20` absent). The subprojects build as **shared** libs
installed to `/usr/local/lib` and are **linked dynamically** — `ldd /usr/local/bin/wayfire` →
`libwlroots-0.20.so => /usr/local/lib/…` — so they *do* appear in `ldd`/`ldconfig`; that is expected
and is **not** system-linking. To force the subproject deterministically (e.g. if an AUR `wlroots0.20`
later appears), pin `-Duse_system_wlroots=disabled`. The plugin is **in-tree** under
`plugins/spread-overview/` and inherits the `wlroots`/`wfconfig` meson dep objects from the parent
build (like `plugins/scale/meson.build` → `all_deps`), so it never re-declares wlroots — no
ABI-matching step. Master requires wlroots **0.20** (`wlroots_base_version = '0.20'`); 0.19.x is not
a build option.
**Decision locked:** build **SPEC B** — standalone `spread-overview` plugin. No fork of scale/core.
**Status of every `[ASSUME→VERIFY]` in SPEC A/B:** resolved below with `file:line` citations.

> ✅ **Signature authority = this master checkout (Principle IV).** Every signature below is confirmed
> against **`src/api/wayfire/**`** and **`plugins/common/wayfire/plugins/common/**`** of this checkout
> (commit `8603d187`), and against in-tree plugin sources (`plugins/scale/scale.cpp`,
> `plugins/single_plugins/move.cpp`, `plugins/vswitch/vswitch.cpp`). **No citation points at
> `/usr/include`.** The earlier pass that used the installed 0.10.1 headers has been fully re-verified.
>
> **0.10.1 → master signature deltas found during re-verification:**
>
> | Symbol | 0.10.1 (installed) | master (this tree) | Impact |
> |---|---|---|---|
> | `WAYFIRE_API_ABI_VERSION_MACRO` (`plugin.hpp`) | `2025'08'22` | **`2026'07'09`** (`:110`) | **Load-gating.** A plugin built in-tree reports `2026'07'09` → loads only into the master compositor at `/usr/local/bin/wayfire`; the 0.10.1 Arch build rejects it. |
> | `core_drag_t::handle_motion` (`move-drag-interface.hpp`) | `wf::point_t` | `wf::pointf_t` (`:198`) | Moot for v1 — `core_drag_t` is **not used** (see `docs/adr/001-self-managed-drag.md`). |
> | `view_2d_transformer_t` header | `view-transform.hpp` | `view-transform.hpp` (`:340`) | **No delta.** My earlier note claiming it moved to the scene headers on master was **wrong** and is retracted; it lives in `src/api/wayfire/view-transform.hpp` on *both* versions (line 331 → 340). |
>
> All other cited signatures (workspace-set methods, per-output-plugin, activation, `add_activator` /
> `activate_plugin` / `rem_binding`, input-grab ctor + `grab_input`, scene-input handlers, layer enum,
> bindings) are **signature-identical** on master; only minor line-number shifts.

---

## 1. The two findings that decided SPEC B over SPEC A

1. **scale's `toggle_all` is flat-packed — no per-workspace regions.**
   `get_all_workspace_views()` (`plugins/scale/scale.cpp:726`) flattens every workspace into one
   list; `layout_slots()` (`scale.cpp:920+`) packs them into a *single size-sorted grid* over the
   output workarea. There is **zero** per-workspace spatial grouping, so "drop over workspace X" is
   undefined in scale today. Adding it = layout surgery inside a 1562-line core plugin → this is
   exactly SPEC A §3's "stop and reconsider B" gate condition. → **B.**

2. **A drag→relocate engine exists (`wf::move_drag::core_drag_t`) but is NOT used in v1.**
   ~~This is the "hard 20%" both specs feared — reusable by B directly.~~ **Downgraded.** On deep
   read, `core_drag_t`'s drop→workspace math is screen-edge based (wrong for an all-on-one-screen
   overview) and its subject is the *real* view, not a thumbnail. **v1 self-manages the drag** via
   the same `view_2d_transformer_t` used for the spread → see `docs/adr/001-self-managed-drag.md`.
   The "SPEC B is cheaper" claim now rests on finding (1) (owning a clustered layout) + relocation
   still being a single call (`move_to_workspace`), **not** on reusing `core_drag_t`.

---

## 2. Verified API surface (what the plugin will call)

### Plugin skeleton / lifecycle
- **Base class:** `wf::per_output_plugin_instance_t` (per-output instance — SPEC B §3 assumption
  confirmed) wrapped by a global `wf::plugin_interface_t`, registered with
  `DECLARE_WAYFIRE_PLUGIN(...)`. Model: `wayfire_scale` + `wayfire_scale_global` at
  `scale.cpp:95, 1489, 1562`.
- **Activator binding:** `wf::plugin_activation_data_t grab_interface{ .name=..., .capabilities=...,
  .cancel=[]{...} }` — `scale.cpp:136`.

### Input grab (pointer + keyboard while active)
- **Class:** `wf::input_grab_t`, header `<wayfire/plugins/common/input-grab.hpp>`
  (`plugins/common/wayfire/plugins/common/input-grab.hpp` — a *common lib*, link it, don't reinvent).
- **Ctor:** `input_grab_t(name, output, keyboard_interaction*, pointer_interaction*, touch_interaction*)`
  — `scale.cpp:148` passes `this` for all three.
- **Interfaces to implement** (from `src/api/wayfire/scene-input.hpp:200,229`):
  - `void handle_pointer_button(const wlr_pointer_button_event&)` — click vs drag start.
  - `void handle_pointer_motion(wf::pointf_t pos, uint32_t time_ms)` — drag follow / hover.
  - `handle_keyboard_key(...)` — Esc / trigger to exit.
- **Grab/ungrab:** `grab->grab_input(...)` / `grab->ungrab_input()` (scale ctor/dtor pattern).

### Rendering — B-render-1 (per-view scene transform), the SPEC B §8 preferred path
- **Per-view transform:** `wf::scene::view_2d_transformer_t` — class in
  `src/api/wayfire/view-transform.hpp:340` (fields `scale_x/y`, `translation_x/y`, `alpha` at
  `:343–353`); added via `view->get_transformed_node()->add_transformer(tr, wf::TRANSFORMER_2D + 1,
  NAME)` — `scale.cpp:207–210`. Remove via `rem_transformer(NAME)`.
- **Same transform is the drag subject** (ADR-001): during a drag we mutate this node's
  `translation_{x,y}` each motion event so the thumbnail follows the cursor.
- This is exactly how modern scale draws thumbnails → the scene API cleanly supports our spread +
  drag. **No custom output renderer needed** (B-render-2 rejected).
- Background dim / labels / drop-highlight: an overlay scene node in the output overlay layer
  (follow scale-title-overlay.cpp for the text/overlay-node pattern).

### View enumeration + workspace geometry (the layout inputs)
- **All toplevels on output, all workspaces:** `output->wset()->get_views(flags)` —
  `workspace-set.hpp:148`. Flags: `wf::WSET_MAPPED_ONLY | wf::WSET_EXCLUDE_MINIMIZED`
  (pass `0` for the minimized bit to include minimized) — `scale.cpp:728`.
- **★ Per-view source workspace (the linchpin):** `output->wset()->get_view_main_workspace(view)`
  → `wf::point_t` — `workspace-set.hpp:157`. *This is the mapping SPEC A feared scale lacked.*
  It's what enables workspace-clustered layout + per-cluster labels in B.
- **Grid size:** `output->wset()->get_workspace_grid_size()` → `wf::dimensions_t` — `:202`.
- **Current workspace:** `output->wset()->get_current_workspace()` → `wf::point_t` — `:197`.
- **Output logical size:** `output->get_relative_geometry()` / `get_layout_geometry()`.

### Drag engine — self-managed (ADR-001), `core_drag_t` NOT used in v1
- **The drag is ours.** On button-down over a thumbnail, record the press origin + grabbed view.
  On motion past the click-vs-drag threshold, enter dragging; each subsequent motion updates the
  thumbnail's `view_2d_transformer_t::translation_{x,y}` (see Rendering above) so it follows the
  cursor. On release, run the layout-cluster hit-test (§3) and relocate if target ≠ source.
- **Why not `core_drag_t`** (`plugins/common/move-drag-interface.hpp`): its `adjust_view_on_output()`
  resolves the target workspace via *physical output-edge math* (`grab.x / output_geometry.width`),
  valid only when dragging a real window toward a screen edge — meaningless in an overview where all
  workspaces are on one screen. It also drives the *real* view, not a thumbnail, and its
  `handle_motion` signature drifted (`point_t`→`pointf_t`). Full rationale + revisit conditions:
  `docs/adr/001-self-managed-drag.md`.

### Relocate on drop (the payoff call)
- `output->wset()->move_to_workspace(view, target_ws)` — `workspace-set.hpp:168`. Same call vswitch
  uses (`vswitch.cpp:45`). Stable, same-output. (`wf::start_move_view_to_wset` is for cross-*output*
  moves only — not needed for v1 single-output.)

### Metadata / config
- Schema format confirmed from `metadata/scale.xml`: `<wayfire><plugin name="spread-overview">
  <group><option name=".." type="activator|bool|animation|int|color|..">`. Installs to `metadata/`.

### View-lifetime safety (SPEC B §5 requirement)
- Hold `wayfire_toplevel_view` (shared/weak handle); before every per-view op check `view->is_mapped()`.
  Watch scale's lifetime fixes for the pattern (commits `fec3c2ee`, `380b432f`, `495bd9b0`).
- `wf::find_output_view_at(output, at)` / `scale_find_view_at()` (`scale.hpp`) exist, but in the
  overview we hit-test against **our own layout rects**, not real view geometry.

---

## 3. The one piece with real design work: the pure layout function (SPEC B §4)

Everything above is glue over confirmed APIs. The only genuine algorithm is packing:

```
layout(views_with_ws, grid_dims, output_size, opts) -> { view -> target_rect, cluster_id }
```
- Input per view: `{ view_id, source_ws = get_view_main_workspace(v), natural_size = get_geometry() }`.
- Group by `source_ws` into clusters arranged on the workspace grid (e.g. 3×3); pack each cluster's
  views unoverlapped (borrow scale's compiz-derived row/col packing, `scale.cpp:920+`, but *per
  cluster* instead of one flat grid).
- Pure, no Wayfire render deps → unit-testable in isolation, per SPEC B §4 [REQ].
- Drop hit-test is the inverse: cursor → which cluster's region → `target_ws`.

---

## 4. Build-up order (SPEC B §10, now concrete)

1. ✅ **API map** (this doc) — gate cleared.
2. Skeleton: `wf::per_output_plugin_instance_t` + activator toggle → log + dim background +
   `input_grab_t` grab + clean exit. Prove lifecycle/grab/teardown, zero rendering.
3. Static spread: enumerate `get_views` → `get_view_main_workspace` → pure `layout()` →
   `view_2d_transformer_t` per view at target rects. Prove enumerate+render+exit-restore.
4. Click-to-focus (scale parity): click thumbnail → focus + close.
5. Drag (self-managed, ADR-001): update the thumbnail transform's translation per motion event to
   follow the cursor; snap back on release (no ws move yet).
6. **Drop-to-relocate:** release → cluster hit-test → `move_to_workspace()` → reflow. The payoff.
7. Polish: labels, drop highlight, animations, inactive-alpha coexistence (SPEC §7), edge cases.

---

## 5. Module layout (SPEC B §4, unchanged — all deps confirmed available)

```
spread-overview/
├── meson.build          # in-tree under plugins/; links the input-grab common lib
├── metadata/spread-overview.xml
└── src/
    ├── plugin.cpp        # per_output_plugin_instance_t + global wrapper + DECLARE_WAYFIRE_PLUGIN
    ├── overview.{hpp,cpp}# state machine: IDLE→ACTIVATING→ACTIVE→(DRAGGING⇄ACTIVE)→DEACTIVATING
    ├── layout.{hpp,cpp}  # PURE fn (§3) — no Wayfire render deps, unit-tested
    ├── render.cpp        # view_2d_transformer_t thumbnails + overlay node
    ├── input.cpp         # input_grab_t interactions, click-vs-drag, self-managed drag, drop hit-test
    └── move.cpp          # thin wrapper: wset->move_to_workspace() (NO core_drag_t — ADR-001)
```

**Build target (locked):** **in-tree** under `plugins/` on this master checkout — the plugin builds
with the compositor and inherits the master ABI (`2026'07'09`), so it loads only into the self-built
`/usr/local/bin/wayfire`. Out-of-tree via `wayfire.pc` is explicitly rejected for v1.
