# Feature Specification: Native Inactive-Window Dimming

**Feature Branch**: `004-inactive-alpha`
**Created**: 2026-10-29
**Status**: Draft
**Input**: User description: "implement the inactive alpha stuffs natively instead of using ipc"

## Clarifications

### Session 2026-10-29

- Q: How should the plugin decide when **not** to dim? → A: **Any plugin holding the output
  suppresses dimming** — not a list of plugin names. The existing script hardcodes
  `{scale, spread-overview}`, which is exactly what caused a window to appear dimmed inside the
  overview; "is anything active?" is correct for plugins that do not exist yet.
- Q: What happens to the existing script? → A: **Retire it once the plugin is verified** — keep it
  in the repository as reference, stop autostarting it. One mechanism owning the opacity, never
  two.
- Q: Scope of v1? → A: **Match the script exactly.** No fade, no per-window rules.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Inactive windows dim, with no helper process (Priority: P1)

The focused window is fully opaque; every other window on the workspace is slightly transparent, so
the user can see at a glance which window has the keyboard. This happens with no Python process, no
IPC socket and no autostart entry — the compositor does it directly.

**Why this priority**: It is the whole feature. Replacing the script is only worthwhile if the
behaviour it provides is preserved exactly.

**Independent Test**: Enable the plugin, stop the script, and switch focus between several windows
on a workspace.

**Acceptance Scenarios**:

1. **Given** several windows are open and the script is not running, **When** the user focuses a
   window, **Then** that window becomes fully opaque and every other window becomes dimmed.
2. **Given** a window is focused, **When** a new window opens and takes focus, **Then** the new
   window is opaque and the previously focused one dims.
3. **Given** dimmed windows, **When** the dimming amount is changed in the configuration, **Then**
   the new amount applies to the already-open windows without restarting the compositor.
4. **Given** dimmed windows, **When** the plugin is disabled (or the compositor shuts down),
   **Then** every window is left fully opaque — no window is stranded dim.
5. **Given** keyboard focus moves to something that is not a window (a panel, or nothing at all),
   **Then** the last focused window keeps its opacity rather than everything dimming at once.

---

### User Story 2 — Dimming never fights a full-screen plugin (Priority: P1)

While a plugin that takes over the screen is active — the window overview, scale, expo — every
window is shown at full opacity, and the dimming resumes when it closes. The user never sees the
window they are looking at mysteriously dimmed inside an overview.

**Why this priority**: Equal to US1, because this is the defect that motivated the rewrite. A
faithful replacement that reproduces the bug is not worth shipping.

**Independent Test**: Open the overview and confirm every thumbnail is fully opaque, including the
one that had focus; close it and confirm dimming returns.

**Acceptance Scenarios**:

1. **Given** windows are dimmed, **When** any screen-taking plugin activates, **Then** every
   window is restored to full opacity for the duration.
2. **Given** such a plugin is active, **When** keyboard focus changes while it runs, **Then** no
   window is dimmed in response.
3. **Given** such a plugin closes, **Then** dimming is re-applied according to the focus that is
   current at that moment.
4. **Given** a *future* plugin that takes over the screen, **Then** it is handled with no change
   to this plugin's code or configuration.

---

### User Story 3 — The helper script is retired (Priority: P2)

Once the native behaviour is confirmed, the Python script stops being autostarted, so exactly one
mechanism controls window opacity.

**Why this priority**: The cleanup that makes the feature worth doing, but only safe after US1 and
US2 are verified.

**Independent Test**: Confirm no helper process is running, dimming still works, and nothing in
the configuration starts it.

**Acceptance Scenarios**:

1. **Given** the plugin is verified, **When** the configuration is updated, **Then** no helper
   process is started at login and dimming still behaves as in US1.
2. **Given** the script is retired, **Then** it remains available in the repository as reference.

---

### Edge Cases

- **Both mechanisms running at once** (during migration) — they would write the same opacity; the
  result must still be sane, and the final state after retiring the script must be correct.
- **A window is dimmed, then the user sets its opacity by hand** (the stock per-window opacity
  binding) — the next focus change overrides the manual value. Accepted for v1; noted so it is not
  mistaken for a defect.
- **A window closes while dimmed** — no crash, nothing left behind.
- **A window appears while a screen-taking plugin is active** — it must not be dimmed mid-overview,
  and must take part in dimming once the plugin closes.
- **Dimming amount set to 1.0** — effectively disables dimming; must not leave windows in a
  half-applied state.
- **Multiple outputs** — each output's focus is independent; dimming must not dim windows on an
  output the user is not looking at merely because they lack keyboard focus. (Single-output
  machine; behaviour must at least be harmless.)
- **Very short-lived plugin activations** (e.g. a workspace switch animation) — if these count as
  "screen-taking", opacity would flicker up and back on every switch. See Assumptions.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The focused window MUST be fully opaque; every other mapped, non-minimized window
  MUST be rendered at a configurable opacity.
- **FR-002**: The dimming amount MUST be configurable, default **0.85**, and MUST apply to already
  open windows when changed, without restarting the compositor.
- **FR-003**: Dimming MUST be suppressed while **any** plugin is active on the output, and MUST NOT
  require naming those plugins in code or configuration.
- **FR-004**: While suppressed, every window MUST be at full opacity; when suppression ends,
  dimming MUST be re-applied from the focus state current at that moment.
- **FR-005**: The plugin MUST restore every window to full opacity when it is unloaded or the
  compositor shuts down.
- **FR-006**: The plugin MUST use the same opacity mechanism as the compositor's existing
  per-window opacity feature, so the two cannot disagree about a window's opacity.
- **FR-007**: The plugin MUST handle windows appearing, disappearing and changing focus at any
  time, including while suppressed, without crashing or leaving a window stranded.
- **FR-008**: Keyboard focus moving to something that is not a window MUST NOT dim the last
  focused window.
- **FR-009**: The plugin MUST be declared in the plugin metadata so its option appears in the
  configuration UI, and MUST be inert when not enabled.
- **FR-010**: Behaviour MUST match the retired script for the cases the script handled: focus
  changes, new windows, and suppression during overviews.
- **FR-011**: No IPC, no external process, and no polling: the plugin MUST react to compositor
  events.
- **FR-012**: The retired script MUST remain in the repository as reference while no longer being
  autostarted.

### Key Entities

- **Dim state** — per window: whether this plugin currently has it dimmed, so it can be restored
  exactly.
- **Focused window** — the window that must stay opaque; the last window to hold keyboard focus if
  focus has since moved to a non-window.
- **Suppression** — whether a plugin is currently active on the output, and therefore whether
  dimming applies at all.

## Success Criteria *(mandatory)*

- **SC-001**: With the script stopped, focusing each of three windows in turn leaves exactly one
  opaque and the others dimmed, every time.
- **SC-002**: Opening the overview shows **all** windows fully opaque, including the one that had
  focus, in 5 out of 5 attempts — the defect that motivated the feature.
- **SC-003**: Changing the dimming amount in the configuration visibly changes already-open windows
  with no restart.
- **SC-004**: Disabling the plugin at runtime leaves zero windows dimmed.
- **SC-005**: After the script is retired, no helper process is running and SC-001 and SC-002 still
  hold.
- **SC-006**: 50 focus changes mixed with opening and closing the overview leave no window stranded
  at the wrong opacity.
- **SC-007**: No measurable change to idle behaviour: with focus unchanged, the compositor does no
  additional repainting because of this plugin.

## Assumptions

- **"Any plugin suppresses dimming" includes short activations.** Some plugins activate briefly for
  animations (a workspace switch, for example). If that produces a visible brighten-then-dim flash
  at the verification gate, the agreed fallback is a short debounce — ignore suppressions shorter
  than a threshold — rather than reintroducing a list of plugin names, which is the design this
  feature exists to remove.
  **RESOLVED at the J2 gate (2026-10-30): no flicker observed on workspace switches**, so no
  debounce is needed and none was added. The risk is kept on record because it is a real property
  of the design: should a future plugin activate briefly enough to matter, the debounce — never a
  name list — is the answer.
- **The existing per-window opacity feature owns the mechanism**: this plugin sets the same opacity
  a user can set by hand, so the last writer wins. A manual value is therefore overridden by the
  next focus change; v1 accepts this.
- **The overview's own behaviour is unchanged**: it already forces every window opaque for its
  session and restores afterwards. This plugin must not fight that, which FR-003 guarantees.
- **Default 0.85** matches the retired script, so the change is invisible to the user except for
  the bug it fixes.
- **Single output** is the target; multi-output must merely be harmless, not optimal.
