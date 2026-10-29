# Phase 0 Research: 004-inactive-alpha

Numbering continues from **R-301** (001: R1–R16 · 002: R-101–R-115 · 003: R-201–R-208).

---

## R-301 — Suppress on *any* plugin activation, not a name list (VERIFIED + decided)

**Decision**: track `wf::output_plugin_activated_changed_signal`
(`signal-definitions.hpp:221`, emitted `output.cpp:271,297` carrying `plugin_name` and
`activated`) and suppress dimming while the active count is non-zero.

**Rationale**: `output->is_plugin_active()` (`output.hpp:172`) answers only for a **named** plugin,
so it cannot express "is anything active?". The retired script keyed on names
(`{scale, spread-overview}`), which is precisely the defect: adding a second overview plugin
silently reintroduced the bug. A count has no list to maintain and covers plugins that do not exist
yet (FR-003, spec US2 scenario 4).

**Known cost** (spec Assumptions): the signal's own documentation says it fires for plugins that
*"(temporarily) change the way Wayfire works like Expo, Scale, Vswitch"*. `vswitch` activates for a
workspace-switch animation, so opacity may rise and fall on every switch. The J2 gate looks for
this explicitly. **Fallback is a debounce** (ignore suppressions shorter than a threshold), never a
name list — reintroducing the list would recreate the bug this feature exists to remove.

**Outcome (MEASURED at the J2 gate, 2026-10-30)**: **no flicker** on workspace switches. `vswitch`
does activate and does suppress, but the transition is not visible in practice — plausibly because
the switch animation covers it and the re-dim lands inside the same motion. **No debounce
implemented.** The risk stays documented because it remains a real property of "any plugin
suppresses": if a future plugin activates briefly enough to matter, the debounce is the answer,
never a name list.

**Alternative rejected**: filtering by plugin *capability* (only suppress for
`CAPABILITY_GRAB_INPUT`) would be ideal, but capabilities are not carried by the signal and the
active-plugin set is private to `output_impl_t` — it would need a core change, which is out of
scope for a fork feature.

---

## R-302 — Write the same opacity the compositor already uses (VERIFIED)

**Decision**: get-or-create a `wf::scene::view_2d_transformer_t` named **`"alpha"`** at
`wf::TRANSFORMER_2D` and set its `alpha`, mirroring `plugins/single_plugins/alpha.cpp:92-93`;
remove it again when the target returns to `1.0`, as that plugin does (`:90`, `:154`).

**Rationale**: that is the transformer the stock `alpha` plugin and the IPC method
`wf/alpha/set-view-alpha` write — the path the retired script went through. Reusing it means
(a) the two can never disagree about a window's opacity (FR-006), and (b) **spread-overview's
existing save/restore already understands it** (`render.cpp`'s `own_alpha_node`, same `"alpha"`
name), so the overview keeps forcing windows opaque and restoring exactly as before.

**Consequence** (accepted for v1): a manual per-window opacity set with the stock plugin's binding
is overridden at the next focus change — last writer wins. Noted in the plugin README so it is not
reported as a bug.

---

## R-303 — Focus tracking must retain the last window (VERIFIED)

`wf::keyboard_focus_changed_signal` (`seat.hpp:22`) is a **core** signal and its documentation
states focus *"may change to nullptr"* — which happens when a panel or layer-shell surface takes
keyboard focus.

**Decision**: on a `nullptr` focus, **keep** the previously focused window as the opaque one rather
than treating it as "nothing focused" (FR-008). The pure policy stays total either way
(`contracts/policy.md` #P4), so this is a caller rule, verified at the J2 gate by clicking a panel.

---

## R-304 — Its own plugin directory (VERIFIED process constraint)

**Decision**: `plugins/inactive-alpha/` with its own `meson.build`, plus **one appended line** in
`plugins/meson.build`.

**Rationale**: `docs/SYNCING-UPSTREAM.md` records why — custom work in new directories merges
automatically, and appending to a shared file is the one edit git resolves on its own. A new source
file under `plugins/single_plugins/` would mean editing an upstream `meson.build` on every sync.
001 chose this shape for the same reason; 002's only merge conflict in two syncs was a shared file
where upstream appended at the same spot.

---

## R-305 — Increments and gates (Principle III)

| # | Increment | Gate |
|---|---|---|
| **J1** | Constitution **v1.4.0** (scope line + Principle V's daemon referent); API-map entries; the pure policy + doctests | tests only; **no plugin, no install** — cannot affect a running desktop |
| **J2** | The plugin: the three signals, the `"alpha"` transformer, `dim_alpha` + metadata, restore-on-unload, meson wiring | build + install + ABI stamp; **tty2 §J2** with the script stopped |
| **J3** | Retire the script from `[autostart]`; enable the plugin; mark the script superseded; docs | **tty2 §J3** on a fresh session; user sign-off |

J1 is docs + a pure function, so the edge cases (suppression transitions, focus → nothing) are
settled before any signal is wired. The constitution amendment goes first for the same reason 002's
did: the tree must never contain code that contradicts its own governance — here J3 would otherwise
violate Principle V's explicit coexistence clause.
