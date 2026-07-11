# Feature Specification: Spread Overview with Drag-to-Workspace

**Feature Branch**: `001-spread-overview`
**Created**: 2026-07-11
**Status**: Draft
**Input**: All-workspace window overview that spreads every window (unoverlapped) and lets the user
drag a window onto another workspace's region to relocate it there. Source design:
`docs/SPEC-B-spread-overview-plugin.md`.

## Clarifications

### Session 2026-07-11

- Q: After a successful drag-relocate, does the overview stay open or close? → A: **Stay open and
  re-flow** — the moved window animates into its new workspace cluster and the user can keep dragging
  others; the overview closes only on the explicit exit paths (trigger again / Esc / background
  click). Confirms User Story 3 and FR-010.
- Q: How are fullscreen windows presented in the spread? → A: **Scaled in place, kept fullscreen** —
  no temporary un-fullscreen; the scene transform renders a fullscreen view directly (as the `scale`
  plugin already does). If a fullscreen window is relocated, fullscreen is re-issued against the
  target workspace. Removes the earlier "temporarily un-fullscreen" assumption.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - See every window at once and jump to one (Priority: P1)

The user presses a shortcut and every open window across all of their workspaces appears at once,
shrunk to fit on a single screen, none overlapping, each visibly grouped under the workspace it
belongs to. The user can immediately see everything they have open and where it lives. Clicking a
window brings that window to the foreground and dismisses the overview; pressing the shortcut again,
pressing Escape, or clicking empty space also dismisses it. When the overview closes, the desktop
looks exactly as it did before.

**Why this priority**: This is the foundational, standalone value — a legible bird's-eye view of the
whole session plus a fast way to switch to any window. It is a complete, usable feature on its own
even if no window is ever moved, and everything else builds on the spread it produces.

**Independent Test**: Open several windows spread across 3+ workspaces (including one maximized
window). Trigger the overview. Confirm every window is visible, none overlap, and each is grouped
and labeled by its home workspace. Click one window and confirm it is focused and the overview
closes with the desktop restored unchanged.

**Acceptance Scenarios**:

1. **Given** windows open on multiple workspaces, **When** the user presses the toggle shortcut,
   **Then** all of them appear simultaneously, scaled down, with no two overlapping, grouped by
   their source workspace.
2. **Given** a maximized window that covers others on its workspace, **When** the overview opens,
   **Then** that window and the ones it was covering are all visible and separated.
3. **Given** the overview is open, **When** the user clicks a window's thumbnail (without dragging),
   **Then** that window is focused and raised, and the overview closes.
4. **Given** the overview is open, **When** the user presses the toggle shortcut again, presses
   Escape, or clicks empty background, **Then** the overview closes and every window returns to its
   exact previous position, size, stacking order, and opacity.

---

### User Story 2 - Drag a window to another workspace (Priority: P2)

While the overview is open, the user presses and holds on a window, drags it across the screen onto
the area representing a different workspace, and releases. That window is really moved to the target
workspace — it stays there after the overview closes and when the user later visits that workspace.
Releasing back over the window's own workspace changes nothing.

**Why this priority**: This is the headline capability that no existing Wayfire overview provides —
relocating a window between workspaces by direct manipulation. It depends on the spread from
User Story 1 and turns the overview from a viewer into an organizer.

**Independent Test**: With the overview open and User Story 1 working, drag a window from
workspace 1's group onto workspace 4's group and release. Close the overview, switch to workspace 4,
and confirm the window is there; switch to workspace 1 and confirm it is gone.

**Acceptance Scenarios**:

1. **Given** the overview is open, **When** the user presses-holds a window and moves the pointer
   beyond a small threshold, **Then** the thumbnail detaches and follows the cursor (a click that
   never crosses the threshold instead behaves as User Story 1's select-and-close, never a move).
2. **Given** a window is being dragged, **When** the pointer is over a workspace group different
   from the window's own, **Then** that group is highlighted as the drop target.
3. **Given** a window is being dragged over a different workspace's group, **When** the user
   releases, **Then** the window is moved to that workspace and the move persists after the overview
   closes.
4. **Given** a window is being dragged, **When** the user releases over its own workspace's group or
   outside any group, **Then** the window returns to its place and nothing is moved.

---

### User Story 3 - Reorganize several windows in one sitting (Priority: P3)

After moving a window, the overview stays open and the layout re-flows so the moved window now
appears under its new workspace. The user can keep dragging additional windows to tidy up their
whole session in a single pass, then dismiss the overview once.

**Why this priority**: A convenience multiplier on User Story 2. Re-flowing in place and staying
open lets the user reorganize many windows without re-triggering the overview each time, but the
feature is valuable even if only one move per session were possible.

**Independent Test**: Open the overview, move window A to another workspace, confirm the overview
remains open and A now sits under the new workspace group, then move window B to a third workspace
in the same session. Dismiss once and confirm both moves persisted.

**Acceptance Scenarios**:

1. **Given** a window has just been relocated, **When** the move completes, **Then** the overview
   remains open and the layout re-flows so the moved window is shown under its new workspace group.
2. **Given** the overview is open after a relocation, **When** the user drags a second window to a
   different workspace, **Then** it is relocated the same way.

---

### Edge Cases

- **A window is closed by its application while the overview is open** — the overview must not crash
  and must not leave a ghost thumbnail; the layout re-flows to fill the gap.
- **A workspace has no windows** — its group is still shown (empty) so it remains a valid drop
  target and the user can see it exists.
- **Only one window exists** — the overview still opens, shows it under its workspace, and can be
  dismissed cleanly (dragging it is allowed but there may be nowhere meaningful to drop it).
- **A window is fullscreen** — it is scaled in place like any other window and stays fullscreen (no
  state change); if it is relocated, it is re-fullscreened on the target workspace.
- **The pointer is released in a gap between groups or on the background** — treated as "no move"
  (snap back), never an accidental relocation.
- **A window will not fit at a reasonable size** (many windows on one workspace) — thumbnails shrink
  to keep zero overlap rather than overflowing or overlapping.
- **The overview is force-dismissed by the system** (e.g. screen lock) — it tears down and restores
  all window state just as a normal close would.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST provide a user-configurable shortcut that toggles the overview on and
  off, working when the desktop is started directly from a text console.
- **FR-002**: When active, the system MUST simultaneously display every currently-open,
  non-minimized top-level window from every workspace of the active display, each scaled to fit.
- **FR-003**: Displayed windows MUST NOT overlap one another, including when a source window was
  maximized or covering others on its workspace.
- **FR-004**: The system MUST visually group displayed windows by their source workspace and label
  each group so the user can tell which workspace each window currently lives on.
- **FR-005**: A press-and-release on a window that does not move beyond a configurable distance
  threshold MUST focus and raise that window and close the overview, and MUST NOT relocate it.
- **FR-006**: A press followed by movement beyond the threshold MUST begin a drag in which the
  window's thumbnail follows the pointer.
- **FR-007**: While dragging, the system MUST indicate which workspace group (if any) the pointer is
  currently over as the prospective drop target.
- **FR-008**: Releasing a drag over a workspace group different from the window's source workspace
  MUST move that window to the target workspace, and the move MUST persist after the overview closes.
- **FR-009**: Releasing a drag over the window's own workspace group, or outside any group, MUST
  make no change (the window returns to its place).
- **FR-010**: After a successful relocation the overview MUST remain open and re-flow the layout so
  the moved window appears under its new workspace group.
- **FR-011**: The system MUST close the overview when the user presses the toggle shortcut again,
  presses Escape, or clicks empty background.
- **FR-012**: On close (including force-dismissal), the system MUST restore every affected window's
  prior position, size, stacking order, and opacity with no residual visual change.
- **FR-013**: If a window is destroyed while the overview is open, the system MUST continue without
  crashing and MUST remove its thumbnail from the layout.
- **FR-014**: The system MUST render thumbnails at full opacity while active regardless of any
  externally-applied per-window transparency, and MUST restore each window's prior opacity on close.
- **FR-015**: The system MUST expose configuration for at least: the toggle shortcut, animation
  duration, spacing between thumbnails, spacing between workspace groups, the click-vs-drag distance
  threshold, the background dim color, whether workspace labels are shown, and whether clicking the
  background closes the overview.

### Key Entities

- **Overview session**: the transient active state from trigger to dismissal; owns the captured
  pre-session state of all affected windows and restores it on exit.
- **Window thumbnail**: the scaled on-screen stand-in for one real window; has a current on-screen
  rectangle and a reference to the real window it represents.
- **Workspace group**: the on-screen region that represents one workspace; acts as both a visual
  grouping/label and a drop target for relocations.
- **Drag operation**: a single press→move→release interaction binding one thumbnail to the pointer
  and resolving, on release, to either a relocation or a snap-back.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: From pressing the shortcut, every window across all workspaces is visible and its
  owning workspace is identifiable within 500 ms.
- **SC-002**: A user can move a window from one workspace to another using only a single
  press-drag-release gesture, with no keyboard input beyond the initial trigger.
- **SC-003**: 100% of relocations persist: after the overview closes, the moved window is present on
  the target workspace and absent from the source workspace.
- **SC-004**: On close, 100% of windows return to their exact prior position, size, stacking order,
  and opacity, with no visible residue (verifiable by before/after comparison).
- **SC-005**: In the spread, zero pairs of window thumbnails overlap, including sessions that
  contained a maximized window.
- **SC-006**: A click below the movement threshold focuses the clicked window and closes the
  overview in 100% of cases and never relocates a window.
- **SC-007**: Closing any window from its application while the overview is open results in zero
  crashes and zero leftover thumbnails across repeated trials.
- **SC-008**: Reorganizing 5 windows across different workspaces can be completed in a single
  overview session in under 30 seconds.

## Assumptions

- **Single display for v1**: the feature targets one active display. The layout is defined in terms
  of a given display so additional displays can be supported later, but dragging a window between
  physically separate displays is out of scope for v1.
- **Overview stays open after a relocation** (re-flowing so the moved window appears under its new
  workspace), enabling User Story 3 — confirmed in Clarifications (2026-07-11).
- **Fullscreen windows are shown scaled in place and kept fullscreen** (no temporary un-fullscreen);
  the scene transform renders a fullscreen view directly, as `scale` does. If relocated, fullscreen
  is re-issued against the target workspace — confirmed in Clarifications (2026-07-11).
- **Direct manipulation requires native compositor integration**: the drag interaction cannot be
  delivered by an external scripting/automation channel, so this is a compositor-native feature.
- **Not a single-workspace window switcher**: this feature is specifically the multi-workspace,
  drag-to-relocate overview and does not aim to replace an existing single-workspace "show all
  windows on this workspace" switcher.
- **Coexistence with external opacity control**: the user runs an external helper that dims
  unfocused windows; the overview overrides opacity only for its own duration and restores the
  helper's state exactly on exit.
- **Minimized windows are excluded by default** from the spread (they are not currently "open on a
  workspace" in the visual sense); this may be made configurable later.
