# Tasks: Native Inactive-Window Dimming

**Feature**: `004-inactive-alpha` | **Plan**: [plan.md](./plan.md) | **Contract**:
[contracts/policy.md](./contracts/policy.md) (NORMATIVE) | **Gates**:
[quickstart.md](./quickstart.md)

Phases map onto the increments J1–J3 of `plan.md`. **Every increment ends with a build + test task,
and J2/J3 end with a tty2 gate that stops work until the user reports back** (constitution
Principle III).

**Legend**: `[P]` = parallelizable · `[US1/2/3]` = user story from `spec.md`.

---

## Phase 1 — Governance + the pure policy (J1) 🔴 NO PLUGIN, NO INSTALL

**Why governance first**: J3 retires the script that Principle V explicitly requires coexistence
with, and the constitution's scope line governs only `spread-overview`. Landing the code first
would put the tree in violation of its own governance.

- [ ] K001 Amend `.specify/memory/constitution.md` to **v1.4.0 (MINOR)**: widen the scope line (`:44`, "Governs the `spread-overview` Wayfire plugin") to the fork's **in-tree plugins**, keeping spread-overview as the worked example; add a Sync Impact entry recording 1.3.0 → 1.4.0 and why
- [ ] K002 In `.specify/memory/constitution.md` Principle V (`:105`), replace the obligation to "coexist with the user's `inactive-alpha` pywayfire daemon" with coexistence with the **native dimming plugin** (the obligation stands; only its referent changes), and note the daemon as superseded
- [ ] K003 [P] Add to `docs/API-MAP-verified.md`: `wf::keyboard_focus_changed_signal` (`src/api/wayfire/seat.hpp:22`, core, may be nullptr), `wf::output_plugin_activated_changed_signal` (`signal-definitions.hpp:221`, emitted `output.cpp:271,297`), `output_t::is_plugin_active` (`output.hpp:172`, per-name only), and the `"alpha"` transformer convention (`plugins/single_plugins/alpha.cpp:92-93`, removal at `:90`,`:154`) — all cited from this tree
- [ ] K004 Create `plugins/inactive-alpha/src/policy.hpp` + `policy.cpp`: `policy_input` and `target_alpha()` exactly as `contracts/policy.md` defines them — three lines of logic, zero Wayfire includes
- [ ] K005 Create `test/plugins/inactive-alpha-policy.cpp` and register it in `test/plugins/meson.build` as `Inactive alpha policy test`, following the spread-overview layout test's shape (include the pure TU directly; doctest only)
- [ ] K006 Implement the policy tests: #P1 focused never dimmed, #P2 suppression beats everything across four `dim` values, #P3 clamping incl. `dim` of `1.0`/`0.0`/`-1`/`2`, #P4 totality with `focused = 0`, #P5 monotonicity, plus the table-driven cross product
- [ ] K007 Run `ninja -C build`, `meson test -C build "Inactive alpha policy test"` and the **full** `meson test -C build`; report files changed and test output. **No install, no tty2** — this increment cannot affect the desktop

---

## Phase 2 — The plugin (J2) · US1 + US2

**Goal**: the native behaviour works with the script stopped.
**Independent test**: quickstart §J2 (9 steps).

- [ ] K008 Create `plugins/inactive-alpha/meson.build` (mirroring `plugins/spread-overview/meson.build`: inherit the parent build's resolved deps, never re-declare wlroots) and append `'inactive-alpha',` to the plugin list in `plugins/meson.build` — **one appended line**, so upstream syncs stay conflict-free
- [ ] K009 [US1] Create `plugins/inactive-alpha/src/plugin.cpp`: a `wf::per_output_plugin_instance_t` registered via `DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<...>)`, holding `dimmed`, `last_focused`, `active_plugins` and `wf::option_wrapper_t<double> opt_dim_alpha{"inactive-alpha/dim_alpha"}` per `data-model.md`
- [ ] K010 [US1] Add the apply helper: get-or-create the `"alpha"`-named `view_2d_transformer_t` at `wf::TRANSFORMER_2D` and set its alpha, mirroring `alpha.cpp:87-99`; **remove the transformer when the target is 1.0** (as `alpha.cpp:90`,`:154` does) so an undimmed window carries no idle node; write only when the value actually differs (SC-007) and damage the view the way `alpha.cpp` does
- [ ] K011 [US1] Add `apply_all()`: for each mapped, non-minimized toplevel on the output, compute `target_alpha(policy_input{last_focused, suppressed, opt_dim_alpha}, id)` and apply via K010; maintain `dimmed` to contain exactly the views this plugin put below 1.0
- [ ] K012 [US1] Connect `wf::keyboard_focus_changed_signal` (core): on a toplevel, set `last_focused` and `apply_all()`; on **nullptr, keep `last_focused` and do nothing** (FR-008, research R-303)
- [ ] K013 [US1] Connect `wf::view_mapped_signal` → `apply_all()`, and `wf::view_unmapped_signal` → erase from `dimmed` with no opacity write (the view is going away); check handle validity before each use (Principle VI)
- [ ] K014 [US2] Connect `wf::output_plugin_activated_changed_signal` on this output: insert/erase `plugin_name` in `active_plugins`; `apply_all()` when the set becomes non-empty (everything to 1.0) and again when it becomes empty (re-dim from current focus). **No plugin names anywhere in the code** (FR-003)
- [ ] K015 [US1] Set a change callback on `opt_dim_alpha` → `apply_all()` (FR-002), and implement `fini()`: restore every view in `dimmed` to 1.0, drop its transformer, clear all state (FR-005)
- [ ] K016 [P] [US1] Create `metadata/inactive-alpha.xml` declaring `dim_alpha` (`type="double"`, default `0.85`, `min 0.0`, `max 1.0`, `precision 0.01` — matching the style of the existing `inactive_brightness` option) and install it from `metadata/meson.build`
- [ ] K017 [US1] Build, install (`sudo ninja -C build install`) and run the ABI-stamp check from `docs/SYNCING-UPSTREAM.md` — one `plugins:` value equal to `pluginabi` (expected `20260801`); confirm the new `.so` and XML are installed
- [ ] K018 🛑 **tty2 gate** — present quickstart §J2 as a numbered plan (including step 8, the `vswitch` flicker risk, and the reminder to `pkill -f inactive-alpha.py` first) and **STOP**; wait for the user's result before Phase 3

---

## Phase 3 — Retire the script (J3) · US3

- [ ] K019 [US3] Back up `~/.config/wayfire.ini`, then add `inactive-alpha` to `[core] plugins`, add an `[inactive-alpha]` section with `dim_alpha = 0.85`, and **remove** the `[autostart]` line that launches `inactive-alpha.py` (it applies live — tell the user before saving)
- [ ] K020 [US3] Mark `docs/ipc-scripts/inactive-alpha.py` superseded in a header comment (what replaced it, when, and that it is kept as reference per FR-012); leave the file otherwise untouched
- [ ] K021 [P] [US3] Add the plugin to `FEATURES.md` under a new entry: what it does, that it replaced an IPC daemon, and the "any plugin suppresses dimming" design with the bug it removes
- [ ] K022 [P] [US3] Write `plugins/inactive-alpha/README.md`: the option, the suppression rule, and the two accepted interactions — a manual per-window opacity is overridden at the next focus change, and the overview's own opaque-forcing is unaffected
- [ ] K023 🛑 **tty2 gate** — present quickstart §J3 (fresh session, no helper process, SC-005/006/007) and **STOP** for the user's sign-off
- [ ] K024 Final report: files changed per increment, test output, the `vswitch` observation, and what remains. Merging and pushing are the user's decision — **ask before every push**

---

## Dependencies

```text
Phase 1 (K001-K007)  ──►  Phase 2 (K008-K018)  ──►  Phase 3 (K019-K024)
```

- **K004 → K005 → K006** (type before test file before tests).
- **K008 → K009** (build wiring before the source it builds), **K009 → K010 → K011**, and
  K011 before K012/K013/K014 (they all call `apply_all`).
- Phases are strictly sequential: no increment starts before the previous gate is reported green.
- K001/K002 before any code lands, per the plan's Constitution Check.

## Parallel opportunities

- Phase 1: K003 alongside K001/K002 (different files).
- Phase 2: K016 (metadata) alongside K009–K015 (different file).
- Phase 3: K021 and K022 together.

## Implementation strategy

**MVP = Phase 1 + Phase 2.** At the end of J2 the native dimming works and the motivating bug is
gone; the script merely still runs at login. J3 is the cleanup that makes it the single mechanism —
valuable, but revertible with one config edit.
