# Contract: configuration options (`metadata/spread-overview.xml`)

User-facing contract validated by wf-config and shown in WCM. Read via
`wf::option_wrapper_t<T>("spread-overview/<name>")`. Section name MUST be `spread-overview` (the
`.so` basename). Maps FR-001, FR-005, FR-015.

| Option | Type | Default | Purpose | Spec ref |
|---|---|---|---|---|
| `toggle` | activator | `<super> KEY_G` | Toggle the overview on/off. | FR-001 |
| `duration` | animation | `300ms` | Entry/exit + reflow animation length. | SC-001, FR-010 |
| `spacing` | int (px) | `20` | Gap between thumbnails within a workspace cluster. | FR-003 |
| `cluster_gap` | int (px) | `40` | Gap between workspace clusters. | FR-004 |
| `drag_threshold` | int (px) | `8` | Movement past which a press becomes a drag, not a click. | FR-005/FR-006 |
| `background` | color | `0.1 0.1 0.1 1.0` | Dim color drawn behind the overview. | — |
| `show_ws_labels` | bool | `true` | Draw the workspace label on each cluster. | FR-004 |
| `close_on_bg_click` | bool | `true` | Clicking empty background closes the overview. | FR-011 |
| `include_minimized` | bool | `false` | Include minimized windows. **STUBBED in v1** — accepted but no effect (see note). | R13 |

> **`include_minimized` is stubbed in v1** (R13, known-open): `layout()` needs a `natural_size` per
> view, but a minimized view has no displayed geometry. The data source is unresolved, so the option
> is parsed but has no effect until the natural-size source is specified. Do not document it as
> functional. `min_scale` (over-dense soft target) is an internal layout tuning value, not exposed as
> a config option in v1.

## Behavioral contract (not options, but user-visible guarantees)

- **Click vs drag** (FR-005/006): release < `drag_threshold` px from press → focus + close; ≥ →
  drag. Deterministic, never both.
- **Drop resolution** (FR-008/009): release over a *different* cluster → relocate + persist; over the
  same cluster or a gap → no change.
- **Stay open** (FR-010, clarified): a successful relocate keeps the overview open and reflows.
- **Fullscreen** (clarified): shown scaled in place, kept fullscreen; re-fullscreened on the target
  workspace if relocated.
- **Restore** (FR-012): on any close, every touched view's position/size/stacking/opacity is exact.

## XML schema note

Format mirrors `metadata/scale.xml`: `<wayfire><plugin name="spread-overview"><group>…<option
name="…" type="activator|animation|int|bool|color">`. `duration` uses `type="animation"` (accepts
`300ms`). `background` uses `type="color"` (`r g b a`). Registered in `metadata/meson.build` via
`install_data('spread-overview.xml', install_dir: conf_data.get('PLUGIN_XML_DIR'))`.
