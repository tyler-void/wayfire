# Implementation Plan: Native Inactive-Window Dimming

**Branch**: `004-inactive-alpha` | **Date**: 2026-10-29 | **Spec**: [spec.md](./spec.md)
**Base**: `master` @ `46f31453` (independent of the in-flight `003-spread-zoom`)

## Summary

Replace `~/.config/ipc-scripts/inactive-alpha.py` with a small in-tree plugin. The focused window
stays opaque, other mapped toplevels render at a configurable opacity (default 0.85), and dimming
is suppressed while **any** plugin is active on the output — which is the whole point of the
rewrite: the script's hardcoded `{scale, spread-overview}` list is what let a focused window appear
dimmed inside the overview, and "is anything active?" needs no maintenance for plugins that do not
exist yet.

Deliberately small: no fade, no per-window rules, no IPC, no polling. It reacts to three compositor
signals and writes the same opacity the stock per-window opacity feature writes.

## Technical Context

**Language/Version**: C++17 (`meson.build:9`)
**Primary Dependencies**: Wayfire 0.12.0-dev, plugin ABI `2026'08'01`. Feature-specific surface,
all verified below: `wf::keyboard_focus_changed_signal`, `wf::output_plugin_activated_changed_signal`,
`wf::view_mapped_signal` / `view_unmapped_signal`, and the `"alpha"`-named
`wf::scene::view_2d_transformer_t` at `wf::TRANSFORMER_2D`. **No new dependency, no core header
touched.**
**Storage**: N/A — per-view dim state in memory, restored on unload.
**Testing**: doctest, a new pure-policy test in the existing test tree; tty2 for the interactive
gates. No compositor needed for the policy tests.
**Target Platform**: self-built Wayfire at `/usr/local`; single output (3440×1440 @ 165 Hz panel),
Arch 0.10.1 kept as the TTY fallback.
**Project Type**: in-tree Wayfire plugin — **its own directory** `plugins/inactive-alpha/`, as
`spread-overview` is. Adding a file under `plugins/single_plugins/` would mean editing an upstream
`meson.build`, i.e. new merge-conflict surface on every sync; a new directory plus one appended
line to `plugins/meson.build` merges automatically (001's lesson, `docs/SYNCING-UPSTREAM.md`).
**Performance Goals**: zero cost at idle (SC-007) — event-driven, no hook, no timer, no repaint
unless an opacity actually changes.
**Constraints**: every window restored to full opacity on unload (FR-005); must never fight the
overview (FR-003); must not dim on focus moving to a non-window (FR-008).
**Scale/Scope**: a few dozen windows; one output.

## Constitution Check

*GATE: must pass before Phase 0; re-checked after Phase 1 (below).* Constitution **v1.3.0** →
**v1.4.0 (MINOR) required by this feature**, for two reasons found while running this gate:

| # | Principle | Status | How the design satisfies it |
|---|---|---|---|
| — | **Scope line** (`constitution.md:44`) | **AMENDMENT REQUIRED** | It reads *"Governs the `spread-overview` Wayfire plugin"*. 004 is a **different** plugin, so as written the constitution does not govern it at all. Widen it to the fork's in-tree plugins, keeping spread-overview as the worked example. |
| I | Pure, testable core | PASS | The decision logic is extracted as a pure policy on plain value types — given (focused id, suppressed, dim amount, view list) → target opacity per view — with doctests. Small, but it is where the edge cases live (focus → nothing, suppression transitions, amount changed). |
| II | Thin API-contact wrappers | PASS | Signals and the opacity transformer are touched in one thin file; the policy knows nothing about Wayfire types. |
| III | Incremental, verifiable build-up | PASS | J1 docs+policy (no plugin), J2 the plugin, J3 retire the script. Each builds and is revertable; J1 cannot affect a running desktop. |
| IV | Verify-before-build | PASS | All four APIs read in this checkout (table below) rather than assumed; recorded in `docs/API-MAP-verified.md` at J1. |
| V | Non-regression & coexistence | **AMENDMENT REQUIRED** | Principle V currently says the plugin *"MUST coexist with the user's `inactive-alpha` pywayfire daemon"* (`constitution.md:105`) — the daemon this feature retires. The obligation itself still holds and matters; its **referent** becomes the native plugin. Without the amendment, J3 would put the tree in violation of its own governance. |
| VI | View-lifetime safety | PASS | Per-view dim state keyed by a handle whose validity is checked before each use; `view_unmapped` scrubs it. A window closing while dimmed leaves nothing behind (spec Edge Cases). |
| VII | ADR discipline | PASS | No recorded architecture decision is reversed: the daemon was a *setup* fact, not an ADR. The notable choice — suppress on **any** activation rather than a plugin list — is recorded in the spec's Clarifications and Assumptions, including the agreed fallback (a debounce, never a name list). An ADR is not warranted; if the debounce fallback is ever taken, that *is* ADR-worthy. |
| VIII | Locked build target & fallback | PASS | In-tree, `/usr/local`, Arch fallback untouched. No core header changes, so the ABI stamp stays `20260801`; the new plugin is in-tree, so it rebuilds with the compositor and can never go stale (the ongoing payoff of the in-tree decision). |

**Gate result: PASS, conditional on the v1.4.0 amendment landing in J1** — docs-only, no code risk,
and scheduled before the code for the same reason 002's amendment was: the tree must not carry code
that contradicts its own governance.

**Complexity Tracking**: empty.

### Principle IV verification (read in this checkout, 2026-10-29)

| Need | API | Location | Note |
|---|---|---|---|
| React to focus changes | `wf::keyboard_focus_changed_signal` | `src/api/wayfire/seat.hpp:22` | emitted on **core**; *"may change to nullptr"* — exactly FR-008's case. This is what IPC's `view-focused` is built on (`ipc-events.hpp:355`) |
| Know when a plugin takes the output | `wf::output_plugin_activated_changed_signal` | `src/api/wayfire/signal-definitions.hpp:221`, emitted `src/output/output.cpp:271,297` with `plugin_name` + `activated` | per-output; the doc notes it fires for plugins that *"(temporarily) change the way Wayfire works like Expo, Scale, Vswitch"* and **not** for one-shot actions — hence the spec's debounce assumption for short activations |
| Track windows | `wf::view_mapped_signal`, `wf::view_unmapped_signal` | core, as used by `ipc-events.hpp:134-135` and by spread-overview | |
| Set a window's opacity | `view_2d_transformer_t` named **`"alpha"`** added at `wf::TRANSFORMER_2D` | `plugins/single_plugins/alpha.cpp:92-93`; read back at `:73`, removed again at `:90`,`:154` | **the same transformer the stock `alpha` plugin and the IPC method `wf/alpha/set-view-alpha` use** — which is why FR-006 holds, and why spread-overview's save/restore (`render.cpp`'s `own_alpha_node`, name `"alpha"`) already understands it |
| Query one named plugin | `output->is_plugin_active(std::string)` | `src/api/wayfire/output.hpp:172` | per-**name** only, so it cannot answer "is anything active?" — the activation signal is tracked instead |

## Project Structure

### Documentation (this feature)

```text
specs/004-inactive-alpha/
├── spec.md, plan.md, research.md, data-model.md, quickstart.md
├── contracts/policy.md          # the pure dim policy + its invariants
├── checklists/requirements.md
└── tasks.md                     # Phase 2 (/speckit-tasks)
```

### Source code

```text
plugins/inactive-alpha/
├── meson.build                  # new; one appended line in plugins/meson.build   [J2]
└── src/
    ├── policy.hpp / policy.cpp  # PURE: focus + suppression -> target opacity     [J1]
    └── plugin.cpp               # thin: 3 signals, the "alpha" transformer        [J2]

metadata/inactive-alpha.xml      # the dim_alpha option                            [J2]
test/plugins/inactive-alpha-policy.cpp   # doctest for the pure policy             [J1]
docs/API-MAP-verified.md         # the four APIs above                             [J1]
.specify/memory/constitution.md  # v1.4.0 (scope line + Principle V referent)      [J1]
~/.config/wayfire.ini            # [autostart] line removed; plugin enabled        [J3]
docs/ipc-scripts/inactive-alpha.py  # kept as reference, marked superseded         [J3]
FEATURES.md, plugins/inactive-alpha/README.md                                      [J3]
```

**Structure Decision**: its own plugin directory, mirroring `spread-overview`, so upstream syncs
stay conflict-free.

## Increments (Principle III)

| # | Scope | Gate |
|---|---|---|
| **J1** | Constitution **v1.4.0**; API-map entries; the pure policy + doctests | layout/policy tests + **full** suite. **No plugin, no install** — cannot touch a running desktop |
| **J2** | The plugin: three signal handlers, the `"alpha"` transformer, the `dim_alpha` option + metadata, restore-on-unload; `meson.build` wiring | build + install + ABI stamp; **tty2 §J2** with the script stopped — dimming works, the overview shows every window opaque, disabling the plugin leaves nothing dimmed |
| **J3** | Retire the script from `[autostart]`, enable the plugin in `wayfire.ini`, mark the script superseded, docs (FEATURES.md + plugin README) | **tty2 §J3**: fresh session, no helper process, SC-001/SC-002 still hold; then user sign-off |

J1 before J2 for the same reason as 002/003: the part with edge cases (suppression transitions,
focus → nothing) is proven without a compositor, so J2's gate is only about wiring.

## Known opens (resolved at implementation, tracked in tasks)

1. **Short activations** (spec Assumptions): `vswitch` activates for workspace-switch animations,
   so suppression may flicker opacity up and back on every switch. The J2 gate must look for this
   explicitly. Agreed fallback is a debounce threshold, **never** a plugin-name list.
2. **Per-output vs global**: focus is a core signal, activation is per-output. A per-output plugin
   instance is the natural shape, but the interaction on a multi-output setup is unverified
   (single-output machine). Must at least be harmless (spec Edge Cases).
3. **Transformer removal on restore**: the stock `alpha` plugin drops the transformer when opacity
   returns to 1.0 (`alpha.cpp:90`,`:154`). This plugin should do the same so it leaves no idle node
   behind — confirm it does not fight the stock plugin's own add/remove.
4. **Interaction with the stock `alpha` plugin** (the user has it enabled): both write the same
   transformer, so a manual per-window opacity is overridden at the next focus change. Accepted for
   v1 (spec Edge Cases); worth a line in the plugin README so it is not reported as a bug.
