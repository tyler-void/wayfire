# Phase 1 Data Model: 004-inactive-alpha

All state is transient and in-memory, owned by one per-output plugin instance. Small by design.

## Entities

### `policy_input` (pure, `contracts/policy.md`)
`{ focused (view id), suppressed (bool), dim (double) }` — plain values, no Wayfire types. The only
thing the decision logic sees.

### DimSession (the plugin instance)
| Field | Meaning |
|---|---|
| `dimmed` | the set of views **this plugin** currently has below 1.0, so FR-005 can restore exactly those and nothing else |
| `last_focused` | the most recent toplevel to hold keyboard focus; **retained** when focus moves to a non-window (R-303 / FR-008) |
| `active_plugins` | names currently activated on this output; non-empty ⇒ suppressed (R-301). A set rather than a counter so a double-activation or a missed deactivation cannot wedge it |
| `opt_dim_alpha` | the configured opacity, with a change callback that re-applies to open windows (FR-002) |

### Per-view opacity (not owned here)
The `"alpha"`-named `view_2d_transformer_t` at `wf::TRANSFORMER_2D` — shared with the stock
per-window opacity feature and understood by spread-overview's save/restore (R-302). This plugin
creates it on demand and **removes it when the target returns to 1.0**, so an undimmed window
carries no idle node.

## Transitions

| Event | Action |
|---|---|
| keyboard focus → a toplevel | `last_focused = view`; re-apply to all |
| keyboard focus → nullptr | **keep** `last_focused`; no re-apply |
| view mapped | re-apply to all (the new window may be focused, or may need dimming) |
| view unmapped | erase from `dimmed`; no opacity write (the view is going away) |
| plugin activated | insert name; if the set was empty, re-apply (everything to 1.0) |
| plugin deactivated | erase name; if the set is now empty, re-apply from current focus |
| `dim_alpha` changed | re-apply to all |
| plugin unloaded / shutdown | set every view in `dimmed` to 1.0, drop its transformer, clear state |

"Re-apply to all" = for each mapped toplevel on the output, compute `target_alpha` and write it
only if it differs from what is there — so an unchanged state causes no damage and no repaint
(SC-007).

## Validation rules

- `dim` is clamped to `[0,1]` by the policy; metadata declares `min 0.0`, `max 1.0`.
- A view handle is checked valid/mapped immediately before each use (Principle VI).
- `dimmed` only ever contains views this plugin dimmed — never one the user dimmed by hand, so
  restoring cannot clobber a manual value set *after* the plugin last wrote.
- Suppression is derived from `active_plugins.empty()`, never cached separately.
