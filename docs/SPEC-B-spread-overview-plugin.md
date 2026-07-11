# SPEC B — `spread-overview`: Standalone Wayfire Overview Plugin with Drag-to-Workspace

**Status:** Draft for agent+codebase evaluation
**Target Wayfire:** match the running build (user is on `0.10.1-746bc7e9`, wlroots `0.19.3`). Pin the clone to the matching tag before evaluating.
**Author intent:** A GNOME-Activities-style overview: press a key → ALL windows across ALL workspaces spread out flat (no overlap, even maximized ones), each labeled with its source workspace → drag any window with the mouse → drop it onto another workspace's region → the window actually moves to that workspace. One-hand-on-keyboard-to-trigger, mouse-to-manipulate.

> **How to read this spec.** Sections marked **[REQ]** are hard requirements. Sections marked **[ASSUME→VERIFY]** are architectural claims I inferred from Wayfire docs/source snippets but did **not** confirm against the exact target tag; the coding agent must confirm or correct each against the real codebase before implementing. Sections marked **[OPEN]** are decisions deferred to you after the agent reports back.

---

## 1. Motivation / Problem statement [REQ]

Wayfire ships two overview-like plugins that are architecturally disjoint:

- **expo** renders each workspace as a *workspace stream* (a single composited buffer of the whole workspace, preserving real z-order) and tiles those buffers. It operates on **workspace images**, not individual views, so it structurally cannot spread windows apart or un-overlap a maximized window covering others.
- **scale** operates per-view (computes geometry per window, spreads them) but is designed as a *focus switcher*: it does not move views between workspaces on drop.

Neither delivers "spread every window in the system AND let me drag one to another workspace." This plugin fills that gap as a **standalone, out-of-tree plugin** — no fork of Wayfire core, no fork of scale. It links against Wayfire via `wayfire.pc` and is built/installed independently.

**Non-goal:** This is not a reimplementation of scale for single-workspace focus switching. It is specifically the multi-workspace, drag-to-relocate overview.

---

## 2. Success criteria / acceptance tests [REQ]

The plugin is "done" when all of these hold on the target machine:

1. **Trigger.** A configurable activator (default `<super> KEY_G`) toggles the overview on/off. Works with the user launching Wayfire via `exec wayfire` from TTY.
2. **Complete spread.** When active, every mapped toplevel view on every workspace of the current output is visible simultaneously, scaled down, with **zero overlap** — including views that are maximized or that cover others in their home workspace.
3. **Workspace grouping is legible.** Views are visually grouped by their source workspace (e.g. arranged into 3×3 clusters matching the workspace grid, or labeled). The user must be able to tell which workspace each window currently lives in.
4. **Mouse drag.** Click-and-hold on any spread view begins a drag; the view thumbnail follows the cursor.
5. **Drop-to-relocate.** Releasing the drag over a different workspace's region moves that view to that workspace *for real* (persists after the overview closes). Releasing over the same region is a no-op (or re-snaps to origin).
6. **Exit paths.** Overview closes on: pressing the trigger again, `Esc`, or (optionally) clicking empty background. On close, all views return to their true positions/z-order with no residual transform, alpha, or geometry corruption.
7. **No leak on view close.** If a view is closed by its client while the overview is open, the plugin does not crash or leave a dangling thumbnail (mirror the `Maybe it was closed?` class of bug seen in the IPC alpha script).
8. **Coexists with the inactive-alpha IPC script.** The user runs a pywayfire daemon that sets per-view alpha on focus change. The overview must restore correct alpha on exit and must not fight the daemon (see §7).

---

## 3. Architecture constraints (Wayfire-specific) [ASSUME→VERIFY]

The agent must confirm each of these against the target tag's headers before building on them.

- **[ASSUME→VERIFY] Per-output plugin instances.** Wayfire creates one plugin instance per `wf::output_t`. Workspaces belong to an output. Since the user runs a single active output (`DP-3`), the plugin only needs to handle its own output's workspace grid, but must not assume a global view list. *Verify: is the plugin base still `wf::plugin_interface_t`, and is per-output instantiation still automatic?*
- **[ASSUME→VERIFY] Workspace enumeration.** The workspace grid size and per-workspace view lists are reachable via the output's workspace manager. Historical API: `output->workspace->get_workspace_grid_size()` and `output->workspace->get_views_on_workspace({x,y}, layers, ...)`. *Verify: exact current class name (older docs say `workspace_manager_t`; newer code moved to a `workspace_set_t` model) and method signatures on the target tag.*
- **[ASSUME→VERIFY] Layers to enumerate.** To catch everything including fullscreen/minimized, enumerate `LAYER_WORKSPACE | LAYER_MINIMIZED | LAYER_FULLSCREEN` (or the current equivalents). *Verify: current layer enum names and whether fullscreen is still a separate layer.*
- **[ASSUME→VERIFY] Moving a view to another workspace.** Core exposes movement primitives; `wf::start_move_view_to_wset(view, wset)` and `VIEW_TO_OUTPUT_FLAG_*` appear in `src/core/core.cpp`. There is also the historical per-output "move view to workspace" path used by vswitch's `with_win_*`. *Verify: which is the correct, stable API to relocate a toplevel view to a target workspace coordinate on the same output — the wset move, or a workspace-manager call. Prefer the one vswitch uses for same-output workspace moves.*
- **[ASSUME→VERIFY] Rendering override.** Overview plugins override the output's renderer / add a custom render hook to draw scaled thumbnails. Expo uses workspace streams; scale uses per-view transformers/render. *Verify: current render-hook API (`wf::scene` graph node? `render_manager` hook? per-view transformer?). This is the single biggest unknown and likely the largest implementation surface.*
- **[ASSUME→VERIFY] Input grab.** While active, the plugin must grab pointer + keyboard so clicks/drags go to the plugin, not the underlying clients. Scale does this. *Verify: current input-grab API (`wf::input_grab_t`? seat grab? scene-graph interactive node?).*
- **[ASSUME→VERIFY] Per-view thumbnail transform.** Spreading requires drawing each view scaled+translated. *Verify: whether to use the scene-graph transform node (post-0.8 scene API) or the older view transformer API on the target tag.*

> The agent's **first task** is to produce a "Wayfire API map" answering every [ASSUME→VERIFY] above with the real symbol names, file paths, and signatures from the pinned checkout. Do not write plugin logic until that map exists. If any assumption is wrong, update this spec section before proceeding.

---

## 4. Module / build layout [REQ]

Out-of-tree, meson-built, installs to the Wayfire plugin dir discovered via `pkg-config wayfire`.

```
spread-overview/
├── meson.build                  # pkg-config: wayfire, wlroots, wf-config; installs .so to plugindir
├── metadata/
│   └── spread-overview.xml       # option schema (see §6) so wf-config validates + WCM shows it
├── src/
│   ├── plugin.cpp                # plugin_interface entry: init/fini, activator binding, lifecycle
│   ├── overview.hpp / .cpp        # core state machine (§5), owns the active session
│   ├── layout.hpp / .cpp          # pure function: (views, ws_grid, output_geom) -> per-view target rects
│   ├── render.cpp                 # thumbnail rendering / scene nodes
│   ├── input.cpp                  # pointer+keyboard grab, click-vs-drag discrimination, drop hit-test
│   └── move.cpp                   # wrapper around the verified core "move view to workspace" API
└── README.md
```

**[REQ]** `layout.*` must be a **pure, unit-testable function** with no Wayfire rendering deps: input = list of `{view_id, source_ws (x,y), natural_size}` + grid dims + output logical size; output = map `view_id -> target_rect (+ which ws-cluster it belongs to)`. This isolates the one piece with real design decisions (how to pack windows without overlap) from the hard-to-test rendering/input glue, and lets you iterate the packing algorithm fast.

---

## 5. Core state machine [REQ]

States: `IDLE → ACTIVATING → ACTIVE → (DRAGGING ⇄ ACTIVE) → DEACTIVATING → IDLE`.

- **IDLE → ACTIVATING** (trigger pressed): snapshot every mapped toplevel across all workspaces; grab input; compute layout (§4 pure fn); start entry animation (views tween from true position → spread target).
- **ACTIVE**: input grabbed. Pointer hover highlights a view. Keyboard `Esc`/trigger → DEACTIVATING.
- **ACTIVE → DRAGGING** (pointer button down on a view, held past a small movement/time threshold to distinguish from a click): bind that view to cursor.
  - **[REQ] Click vs drag discrimination:** button-down + release with < N px movement and < T ms = "click" (default action: focus that view + close overview, i.e. behave like scale's select). Button-down + movement ≥ N px = "drag". Defaults `N=8px`, `T=250ms`; both configurable.
- **DRAGGING**: thumbnail follows cursor; the workspace-cluster under the cursor is highlighted as drop target.
- **DRAGGING → ACTIVE** (button release): hit-test cursor → target workspace cluster. If different from view's source ws → call verified move API (§move.cpp) → re-run layout so the moved view animates into its new cluster. If same → snap back.
- **ACTIVE → DEACTIVATING**: reverse animation, views return to true positions; release grab; restore alpha (§7); tear down render hooks.

**[REQ]** Every state transition must be safe against a view disappearing mid-session (client closes it). Hold views by a stable handle/weak-ref pattern the agent confirms is correct for the target tag; before any per-view op, check the view is still mapped/valid.

---

## 6. Configuration schema (`metadata/spread-overview.xml`) [REQ]

Expose at minimum:

| option | type | default | purpose |
|---|---|---|---|
| `toggle` | activator | `<super> KEY_G` | open/close overview |
| `duration` | int (ms) | `300` | entry/exit + reflow animation length |
| `spacing` | int (px) | `20` | gap between thumbnails within a cluster |
| `cluster_gap` | int (px) | `40` | gap between workspace clusters |
| `drag_threshold` | int (px) | `8` | click-vs-drag movement threshold |
| `background` | color | `0.1 0.1 0.1 1.0` | dim behind the overview |
| `show_ws_labels` | bool | `true` | draw workspace number on each cluster |
| `close_on_bg_click` | bool | `true` | click empty area to exit |

**[ASSUME→VERIFY]** Confirm the current metadata XML schema format (option types, `<compound>`/list support) against an existing plugin's metadata XML on the target tag (e.g. `metadata/scale.xml`).

---

## 7. Interaction with the inactive-alpha IPC daemon [REQ]

The user runs a pywayfire daemon that sets `set_view_alpha` on focus change (values ~0.85 inactive / 1.0 focused). Constraints:

- **[REQ]** On overview activate, the plugin should render thumbnails at full opacity regardless of the daemon's per-view alpha (the overview is its own visual context). Decide whether to (a) temporarily force alpha=1.0 on all views for the session and restore on exit, or (b) render thumbnails independent of the view's alpha property. **[OPEN]** — agent to advise which is cleaner given the render API; (b) is preferable if the render path lets you ignore the source view's alpha.
- **[REQ]** On overview exit, if the plugin touched view alpha, it must restore each view's pre-session alpha exactly, so the daemon's state isn't clobbered. Capture pre-session alpha per view at ACTIVATING.
- **[REQ]** A relocated view (moved to a new workspace) will trigger a `view-focused`/geometry event the daemon listens to; that's fine and expected — just ensure the plugin doesn't itself set a wrong alpha that the daemon then can't correct.

---

## 8. Rendering approach decision [OPEN → agent-led]

Two candidate rendering strategies; agent picks based on the verified render API:

- **B-render-1 (per-view scene transform):** add a transform node per view in the scene graph that scales+translates it to its spread rect; draw an overlay layer for background/labels/drop-highlight. Preferred if the post-0.8 scene-graph API cleanly supports interactive transform nodes (this is closest to how modern scale works).
- **B-render-2 (custom output renderer + thumbnails):** override the output renderer, draw each view's texture as a scaled quad at its target rect (expo-like control, but per-view instead of per-workspace-stream). More control, more code, more breakage risk across versions.

**[REQ]** Agent must state which it chose and why, citing the actual render API found. Prefer B-render-1 unless the scene API can't express drag/interactivity.

---

## 9. Risks & explicit non-solutions [REQ]

- **Render API churn** is the top maintenance risk: Wayfire's scene/render layer has changed across 0.7→0.8→0.10. The plugin's render+input code is where rebases will hurt. Keeping `layout.*` pure and isolating all API contact in thin wrappers (`render.cpp`, `input.cpp`, `move.cpp`) is the mitigation — when the API moves, only the wrappers change.
- **Not the IPC route.** Wayfire IPC does not expose in-overview drag position events, so a pure pywayfire implementation cannot do mouse drag-to-workspace. This plugin must be native C++. (A pywayfire helper could still offer a *menu-based* "send window to workspace," but that is explicitly not the goal.)
- **Multi-output** is out of scope for v1 (user drives one output). Design the layout fn to take an output param so multi-output can be added later without rework, but do not implement cross-output drag in v1.
- **Direct scanout / fullscreen clients:** a truly fullscreen client may be on a separate layer/scanout path. Confirm enumeration catches it; if a fullscreen client can't be thumbnailed cleanly, define the fallback (e.g. temporarily unfullscreen on session start, restore on exit) — **[OPEN]**.

---

## 10. Suggested build-up order (for the agent) [REQ]

1. Produce the **API map** (§3). Gate everything on this.
2. Skeleton plugin: activator toggle that just logs + dims background + grabs input + exits cleanly. Prove lifecycle + grab + teardown with zero rendering.
3. Static spread: on activate, enumerate views, run `layout.*`, render thumbnails at computed rects (no drag yet). Prove enumeration + render + exit-restore.
4. Click-to-focus (scale parity): click a thumbnail → focus that view + close. Prove hit-testing.
5. Drag: click-hold-move a thumbnail following cursor, snap back on release (no ws move yet). Prove drag state machine + threshold.
6. Drop-to-relocate: wire release → target-cluster hit-test → verified move API → reflow. This is the payoff.
7. Polish: labels, drop highlight, animations, alpha coexistence (§7), edge cases (§2.7).

Each step is independently testable and independently revertable. Do not skip the API map.
