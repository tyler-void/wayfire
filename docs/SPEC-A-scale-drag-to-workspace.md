# SPEC A — `scale-relocate`: Fork of the `scale` Plugin Adding Drag-to-Workspace

**Status:** Draft for agent+codebase evaluation
**Target Wayfire:** match the running build (`0.10.1-746bc7e9`, wlroots `0.19.3`). Pin the clone to the matching tag.
**Author intent:** Extend the existing `scale` plugin — specifically its `toggle_all` (all-workspaces) mode — so that a window dragged with the mouse and dropped over a different workspace's on-screen region is actually relocated to that workspace, instead of scale's current behavior (select/focus only). Reuse everything scale already does (per-view spread, input grab, thumbnail render); add only the drag-relocate layer.

> **Reading convention** (same as SPEC B): **[REQ]** = hard requirement. **[ASSUME→VERIFY]** = inferred from docs/snippets, agent must confirm against the pinned checkout. **[OPEN]** = decision deferred to you after agent reports.

---

## 1. Motivation / why fork scale instead of building fresh [REQ]

Scale in `toggle_all` mode already delivers ~80% of the target:
- spreads every view across all workspaces, no overlap, including maximized ones;
- grabs input, renders per-view thumbnails, supports pointer interaction (click-to-select).

The missing 20% is: **a dragged view, dropped over another workspace's region, does not move to that workspace** — scale treats the interaction as focus selection. This spec adds that.

Trade-off vs SPEC B: smaller code surface (you inherit scale's render+input+layout), faster to a working result. Cost: you now maintain a **fork of an actively-developed core plugin** and must rebase your diff onto upstream scale each time Wayfire bumps. SPEC B avoids the fork but rebuilds the 80%.

**Decision driver for you+agent:** how large and how entangled is scale's existing pointer-handling code? If the click-handling is cleanly separable, the fork diff stays small and A wins on effort. If scale's input/render is tightly coupled, the fork diff will be invasive and fragile, tilting toward B. **The agent's API map (§3) is what settles this.**

---

## 2. Success criteria / acceptance tests [REQ]

On the target machine, with the forked scale installed:

1. **Backward compatible.** All existing scale behavior (`toggle`, `toggle_all`, click-to-focus, existing options like `inactive_alpha`, `title_overlay`) is unchanged when the new drag feature is not used. Existing `wayfire.ini [scale]` config keeps working.
2. **Drag in toggle_all.** In `toggle_all` mode, click-hold-drag on a thumbnail enters a drag; the thumbnail follows the cursor.
3. **Click still selects.** A click (below the drag threshold) still does scale's normal select-and-close. Drag and click are disambiguated, not conflated.
4. **Drop-to-relocate.** Releasing a drag over a workspace region different from the view's source workspace moves the view there for real (persists after scale closes). Same-region release snaps back.
5. **Region model correct.** In `toggle_all`, scale lays views out across the whole output; the plugin must map a drop cursor position to the correct **target workspace**. This requires knowing (or reconstructing) which screen region corresponds to which workspace. **[ASSUME→VERIFY]** whether scale-all already carries a view→workspace mapping and any spatial notion of per-workspace regions, or whether that mapping must be added.
6. **Single-workspace mode unaffected.** In plain `toggle` (current workspace only) mode, drag-relocate is a no-op or disabled — relocation only makes sense across workspaces. **[OPEN]:** decide whether to also allow drag-relocate in single `toggle` mode by treating drop position against the (hidden) workspace grid; default v1 = only in `toggle_all`.
7. **Clean teardown & no crash on view close** — same as SPEC B §2.6/2.7.
8. **Alpha-daemon coexistence** — same as SPEC B §7.

---

## 3. Existing-code map the agent must produce first [REQ / ASSUME→VERIFY]

Before any edit, the agent reads `plugins/scale/` on the pinned tag and answers:

- **[ASSUME→VERIFY] File set.** Confirm the scale source layout (historically `plugins/scale/scale.cpp`, possibly split across headers / a `scale-title-filter` etc.). List every file and its role.
- **[ASSUME→VERIFY] View→workspace knowledge.** In `toggle_all`, does scale store, per scaled view, which workspace it came from? Where? (This is the linchpin: relocation needs source-ws + a way to compute target-ws from drop coords.)
- **[ASSUME→VERIFY] Layout geometry.** How does scale compute each thumbnail's rect in `toggle_all`? Is there any structure that groups thumbnails by workspace spatially, or are all views packed into one flat grid regardless of source workspace? *This determines whether "drop over workspace X's region" is even well-defined, or whether you must impose a per-workspace regioning first.*
  - **[REQ] If flat-packed:** the fork must add a per-workspace clustering to `toggle_all` layout so that a drop location maps to a workspace. This is a real layout change, not just an input hook — note it raises the fork's invasiveness (data point for A-vs-B decision).
- **[ASSUME→VERIFY] Pointer handling.** Where does scale receive pointer button/motion events while active? What function decides "this click selects view V and closes scale"? Identify the exact hook to branch click-vs-drag.
- **[ASSUME→VERIFY] Per-view render/transform.** How does scale draw a thumbnail (scene transform node vs view transformer)? For drag, we must move one thumbnail to follow the cursor each motion event — find the mutable geometry/transform to update per frame.
- **[ASSUME→VERIFY] Relocation API.** Same as SPEC B §3: confirm the correct core call to move a toplevel view to a target workspace on the same output (the vswitch `with_win_*` path, or `start_move_view_to_wset`, or a workspace-manager method). Reuse whatever vswitch uses.
- **[ASSUME→VERIFY] Grab exclusivity.** Confirm scale's input grab won't conflict with the extra drag handling; that motion events during a button-hold are delivered to scale.

> **Gate:** produce this map before editing. The map's answer to "is scale-all flat-packed or workspace-clustered?" and "how coupled is the pointer handler?" directly decides whether SPEC A stays cheap. If the map reveals scale-all is flat-packed AND pointer handling is deeply coupled, **stop and reconsider SPEC B** — report back rather than pushing a large invasive fork.

---

## 4. Change surface (minimal-diff target) [REQ]

Aim to keep the fork diff as small and localized as possible for rebaseability:

1. **New session sub-state in scale:** a `drag_state` ( `none | maybe_drag | dragging` ) plus the dragged view handle and press origin. Prefer adding this as a self-contained member/struct rather than threading booleans through existing methods.
2. **Branch the existing pointer handler:**
   - button-down on a view → record press origin, `drag_state = maybe_drag` (do **not** immediately select).
   - motion while `maybe_drag` and movement ≥ `drag_threshold` → `drag_state = dragging`; from here scale's normal select-on-release is suppressed.
   - motion while `dragging` → update dragged thumbnail geometry to follow cursor; compute+highlight target workspace region.
   - button-up while `maybe_drag` (never crossed threshold) → fall through to scale's existing select-and-close (unchanged path).
   - button-up while `dragging` → hit-test → relocate if target ≠ source (via §3 verified API) → reflow layout → `drag_state = none`. Do **not** close scale on a relocate (let the user drag several); **[OPEN]** or close after each — your call.
3. **[CONDITIONAL] Layout clustering:** only if §3 map shows scale-all is flat-packed — add per-workspace clustering + region metadata to the `toggle_all` layout so drops resolve to a workspace. Keep behind the same `toggle_all` path so single-workspace `toggle` is untouched.
4. **New options** (additive, in `metadata/scale.xml` fork): `drag_threshold` (px, default 8), `relocate_enabled` (bool, default true), optional `close_after_relocate` (bool, default false).

**[REQ]** No behavior change to any existing scale code path when `relocate_enabled=false`. This lets you A/B the fork against stock scale by flipping one option.

---

## 5. Interaction with inactive-alpha daemon [REQ]

Identical to SPEC B §7. Scale already manages view alpha during its session (`inactive_alpha`, `minimized_alpha` options the user has set to `1.0`), so scale's own alpha handling plus restore-on-exit must be preserved; ensure the drag additions don't bypass scale's existing alpha restore. Capture and restore the daemon's pre-session per-view alpha as scale already does — verify scale's teardown restores original alpha and that a *relocated* view isn't left at a scale-internal alpha.

---

## 6. Risks specific to the fork route [REQ]

- **Rebase burden:** scale is core and moves. Every Wayfire bump = rebase your diff. Mitigation = keep the diff localized per §4; avoid reformatting untouched code; keep new logic in as few added functions as possible so conflicts are rare.
- **Hidden coupling:** if `toggle_all` layout is flat-packed, adding workspace regioning is a non-trivial layout change inside someone else's plugin — higher conflict risk than a pure input hook. This is the main scenario where B becomes the better bet; §3 gate catches it.
- **Upstreamability:** a clean, option-gated `relocate` feature might be acceptable upstream (scale is maintained by Kondor/Moreau). If so, contributing it removes the fork-maintenance problem entirely. Consider opening a design issue referencing the long-standing community requests (Wayfire "Present windows" #449; Hyprland #1902 asking for *Wayfire-like* drag-across-workspaces) before large work, to gauge upstream appetite. **[OPEN]** whether to pursue upstream-first.

---

## 7. Suggested build-up order (for the agent) [REQ]

1. **Existing-code map (§3). Gate everything on it.** Its answers decide if A stays cheap or you should pivot to B.
2. Fork scale in-tree in your Wayfire checkout; build unmodified; confirm parity with stock scale.
3. Add `drag_state` + branch pointer handler so that drag is *detected* (log only), click path untouched. Prove disambiguation without moving anything.
4. Make the dragged thumbnail follow the cursor; snap back on release. Prove per-frame geometry mutation + threshold.
5. **[CONDITIONAL]** If flat-packed: add per-workspace clustering + region map to `toggle_all`. Prove drop→workspace resolution.
6. Wire release → target-ws hit-test → verified relocate API → reflow. Payoff.
7. Options, highlight, alpha-restore verification, edge cases.

---

## 8. A-vs-B decision checklist (fill in after §3 map) [REQ]

Record concrete findings so the choice is evidence-based, not vibes:

- [ ] scale-all layout: flat-packed OR workspace-clustered? → clustered favors A; flat-packed adds layout work to A.
- [ ] pointer handler coupling: isolated function OR threaded through many methods? → isolated favors A.
- [ ] relocate API: single stable call confirmed? (same for both specs)
- [ ] render/transform API: does mutating one thumbnail's geometry per frame look straightforward in scale's existing render path?
- [ ] estimated fork diff size (LOC touched in existing files) vs estimated fresh-plugin size (SPEC B).
- [ ] rebase pain estimate: how often does `plugins/scale/` change upstream (check git log frequency on that dir)?

**Rule of thumb:** if the fork diff is small and mostly *additive* (new functions, one branch in the pointer handler, no layout surgery), take **A**. If it requires surgery inside scale's layout to introduce workspace regions AND the pointer path is entangled, take **B** — the standalone plugin will be cleaner to own long-term even though it re-does the spread.
