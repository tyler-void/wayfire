# Feature Specification: Overview Opens and Closes as One Camera Zoom

**Feature Branch**: `003-spread-zoom`
**Created**: 2026-10-29
**Status**: Draft
**Input**: User description: "the closing animation of spread overview can be better, make the
separating lines, zoom out and scale like on expo — the current closing animation makes the lines
fade instead of scale and zoom like expo"

## Clarifications

### Session 2026-10-29

- Q: What should actually zoom — only the chrome, everything, or a hybrid? → A: **Hybrid.** The
  workspace you are landing on keeps today's seamless hand-off (its windows settle to/from their
  real desktop positions); every *other* workspace's thumbnails scale away and fade; the chrome
  (workspace separating lines, dim veil, wallpaper tiles) scales with the camera and fades.
- Q: Exit only, or mirror it on opening too? → A: **Both.** Opening becomes the inverse camera
  move, which replaces the current "windows converge from their real positions" entry.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Closing zooms into the workspace I am going to (Priority: P1)

The user dismisses the overview — by pressing the toggle again, by Esc, by clicking a window, or
by clicking an empty workspace cell. Instead of the grid and veil fading away where they are, the
whole overview **zooms toward the workspace the user is about to land on**: that cell grows until
it fills the screen, its windows arriving exactly at their real desktop positions, while the
separating lines, the dim veil and the wallpaper tiles scale along with it and fade out, and the
other workspaces' thumbnails scale away off-screen. The effect reads like expo closing.

**Why this priority**: This is the reported complaint and the half of the motion the user sees most
often. It is complete on its own even if opening is never touched.

**Independent Test**: Open the overview on a 3×3 grid with windows on several workspaces, then
dismiss it four ways (toggle, Esc, click a window on another workspace, click an empty cell) and
watch where the motion converges.

**Acceptance Scenarios**:

1. **Given** the overview is open, **When** the user presses Esc, **Then** the overview zooms
   toward the **current** workspace's cell, that cell's windows end exactly at their real desktop
   positions, and the separating lines scale with the zoom rather than fading in place.
2. **Given** the overview is open and the user clicks a window belonging to another workspace,
   **When** the overview closes, **Then** the zoom converges on **that window's** workspace cell
   — the one being switched to — not on the workspace the user came from.
3. **Given** the overview is open, **When** the user clicks an empty workspace cell, **Then** the
   zoom converges on **that** cell.
4. **Given** any close, **When** the animation ends, **Then** every window is at exactly the
   position, size and stacking it had before the overview opened (or, for a relocated window, its
   new correct position), with no visible settling step after the zoom.
5. **Given** a single-workspace grid (1×1), **When** the overview closes, **Then** the animation
   completes cleanly with no zoom (there is nothing to zoom from) and no visual glitch.

---

### User Story 2 — Opening zooms out from the workspace I was on (Priority: P2)

Opening the overview is the same motion in reverse: the current workspace fills the screen, then
the camera pulls back until the whole workspace grid is visible, the separating lines and veil
scaling in with it, and the other workspaces' thumbnails scaling in from beyond the screen edges.

**Why this priority**: It makes opening and closing read as one gesture rather than two unrelated
animations. Valuable but strictly secondary — the overview is already usable with today's entry.

**Independent Test**: Open the overview repeatedly from different workspaces and confirm the
motion starts from the workspace you were on and is the mirror of the close.

**Acceptance Scenarios**:

1. **Given** the user is on a workspace, **When** the overview opens, **Then** the motion begins
   with that workspace's cell filling the screen and pulls back to the full grid.
2. **Given** the overview is opening, **When** the animation ends, **Then** every thumbnail is at
   its final layout position with no extra jump or settle.
3. **Given** the previous entry animation (thumbnails converging from their real positions) is
   replaced, **When** the overview opens, **Then** there is no trace of the old motion — no
   thumbnail travels independently of the camera except those of the starting workspace.

---

### User Story 3 — The zoom never costs me control (Priority: P3)

The user can act during the motion: pressing a thumbnail while the overview is still zooming open
grabs the thumbnail that is **visibly** under the pointer, and dragging from there behaves exactly
as it does once the motion has settled.

**Why this priority**: A correctness guard rather than a new capability — it protects behaviour the
previous feature already guarantees, which a camera move could silently break.

**Independent Test**: Open the overview and immediately press and drag a thumbnail mid-zoom;
confirm the grabbed window is the one under the cursor and that the drop lands where the highlight
says.

**Acceptance Scenarios**:

1. **Given** the overview is zooming open, **When** the user presses on a thumbnail, **Then** the
   thumbnail visibly under the pointer is the one pressed, it stops where it is drawn, and the
   rest of the motion continues.
2. **Given** a drag begins mid-zoom, **When** the user drops it on a workspace cell, **Then** the
   window is relocated to the cell the highlight marked.
3. **Given** a window is closed by its application during either animation, **Then** the overview
   neither crashes nor leaves a ghost thumbnail.

---

### Edge Cases

- **Single workspace (1×1 grid)** — there is no surrounding grid to zoom from; the animation must
  degrade to the present behaviour (settle/fade) without a glitch or a division-by-zero.
- **Non-square grids** (e.g. 4×2 on a 16:9 screen) — a cell's shape does not match the screen's,
  so mapping it exactly to the screen would stretch windows. The zoom keeps window aspect ratios
  and lets the overflow run past the screen edges instead (see Assumptions).
- **Workspace gap and margin configured** — with a gap between cells the camera still converges on
  the cell itself, not the gap.
- **A relocate during the session** — the stay-open re-flow after a drop is **not** a camera move;
  it keeps gliding thumbnails to their new slots as it does today.
- **Closing while a drag is still held** — input is already ignored once closing starts; the
  dragged thumbnail must still end up correctly placed and not stranded mid-flight.
- **Window closed mid-animation** — skipped safely, no crash, no ghost.
- **An empty destination workspace** — the zoom still converges on that cell and ends on a clean
  empty desktop.
- **Zero/very short configured duration** — the end state must be correct even if the motion is
  effectively instant.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Opening and closing the overview MUST be a single camera motion in two directions:
  the view scales between "one workspace cell fills the screen" and "the whole workspace grid is
  visible".
- **FR-002**: The closing camera MUST converge on the workspace the session is about to leave the
  user on: the clicked window's workspace, the clicked empty cell, or the current workspace for a
  plain dismissal.
- **FR-003**: The opening camera MUST start from the workspace that was current when the overview
  was triggered.
- **FR-004**: The workspace separating lines, the dim veil and the wallpaper tiles MUST scale with
  the camera (not stay at a fixed size) and fade over the same motion.
- **FR-005**: The destination workspace's windows MUST arrive at — and depart from — their real
  desktop positions, so the hand-off between the overview and the desktop has no visible jump.
- **FR-006**: Every other workspace's thumbnails MUST scale with the camera and fade out, rather
  than fading in place at a fixed size.
- **FR-007**: Window aspect ratios MUST be preserved throughout both animations; nothing may
  appear stretched.
- **FR-008**: The closing animation MUST use the configured close duration and the opening
  animation the configured open duration; no new configuration option is introduced.
- **FR-009**: The stay-open re-flow after a window is relocated MUST keep its current behaviour
  and MUST NOT become a camera move.
- **FR-010**: While either animation is running, hit-testing a press and resolving a drop target
  MUST agree with where thumbnails are **actually drawn**, so the previous feature's "the
  thumbnail you see is the one you grab" guarantee still holds mid-motion.
- **FR-011**: Both animations MUST leave no residue: on close every window is restored exactly as
  before the session (including a window relocated during it), and on open every thumbnail ends
  at its final layout position.
- **FR-012**: The layout itself MUST NOT change: workspace cells keep their fixed, equal positions
  and sizes, thumbnails keep their arrangement and sizes, and zero-overlap still holds.
- **FR-013**: Both animations MUST be safe against a window disappearing mid-motion.
- **FR-014**: The feature MUST NOT change behaviour outside the overview and MUST keep coexisting
  with the user's inactive-window dimming helper.
- **FR-015**: The replaced entry animation MUST be removed rather than left unreachable in the
  code.

### Key Entities

- **Camera** — the single zoom shared by both directions: which workspace cell it is anchored on,
  and how far between "cell fills the screen" and "whole grid visible" it currently is.
- **Destination workspace** — the cell the close converges on; the workspace the user will be on
  when the overview is gone.
- **Chrome** — the non-window overlay that scales with the camera: separating lines, dim veil,
  wallpaper tiles.

## Success Criteria *(mandatory)*

- **SC-001**: Watching the close, the separating lines visibly **grow** toward the destination
  workspace rather than staying the same size — judged on the user's 3×3 grid at 3440×1440.
- **SC-002**: At the end of a close, the destination workspace's windows are within 1 px of the
  positions and sizes they had before the overview opened (no visible settle after the zoom).
- **SC-003**: Both animations complete within their configured durations ± one frame.
- **SC-004**: Dismissing by clicking a window on another workspace lands the user on that
  workspace, with the zoom having converged on that same cell, in 5 out of 5 attempts.
- **SC-005**: A press during the opening motion grabs the thumbnail under the pointer, and its
  subsequent displacement matches the pointer's within 1 px — the previous feature's guarantee,
  re-verified under the camera.
- **SC-006**: All seven of the original overview's acceptance scenarios still pass unchanged.
- **SC-007**: Neither animation drops more presented frames than today's animations do on the
  user's setup (3440×1440 at 165 Hz, Intel iGPU), measured over ten open/close cycles.
- **SC-008**: No window is left mis-positioned, dimmed, or duplicated after 20 consecutive
  open/close cycles mixing all four dismissal routes.

## Assumptions

- **Aspect handling on non-square grids**: the camera uses one uniform scale for both axes, chosen
  so the destination cell at least covers the screen, with the overflow running past the screen
  edges. This keeps windows unstretched (FR-007) at the cost of the cell not mapping exactly to
  the screen on grids like 4×2. The user's grid is 3×3, where a cell has the screen's aspect
  ratio and the question does not arise.
- **Durations**: the existing open and close duration settings are reused as-is; the close is
  shorter than the open by default, which the camera inherits.
- **The dim veil's bright-cell transition** stays as it is — its deferral is recorded in the
  previous feature and is out of scope here.
- **The re-flow after a relocate** stays a per-thumbnail glide; only open and close become camera
  moves.
- **Expo's own implementation is not reusable**: it renders all workspaces onto a composed surface
  and animates a viewport over it. This overview deliberately transforms the live windows instead,
  a decision made to avoid scene-capture buffer traffic on this machine's hybrid GPU. The camera
  must therefore be built on the existing per-window and overlay rendering, and that constraint is
  a design input, not a thing to revisit here.
