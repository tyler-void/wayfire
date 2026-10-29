# inactive-alpha

Dims every window except the focused one, so the window holding the keyboard is obvious at a
glance — and gets out of the way automatically whenever a plugin takes over the screen.

Replaces an IPC helper script (`docs/ipc-scripts/inactive-alpha.py`, kept as reference) with a
native in-tree plugin: no Python process, no IPC socket, no autostart entry.

## Usage

Enable it and set the opacity:

```ini
[core]
plugins = … inactive-alpha

[inactive-alpha]
dim_alpha = 0.85
```

| Option | Default | Meaning |
|---|---|---|
| `dim_alpha` | `0.85` | Opacity of windows without keyboard focus. `1.0` disables dimming. Applies to already-open windows the moment it changes — no restart. |

## How it decides

- The **focused** window is always fully opaque.
- Every other mapped, non-minimized window is drawn at `dim_alpha`.
- **While any plugin is active on the output, dimming is suspended** and every window is fully
  opaque; it resumes from the focus current at the moment that plugin releases the output.
- If keyboard focus moves to something that is not a window — a panel, a layer-shell surface — the
  **last focused window keeps its opacity** rather than everything dimming at once.

That third rule is the reason this plugin exists. The script it replaces decided by **plugin
name**, so adding a second overview plugin silently reintroduced the bug it was written to avoid:
the focused window appeared dimmed inside `spread-overview` until its name was added to a
hardcoded list. Asking "is anything active?" instead needs no maintenance for plugins that do not
exist yet — including future ones.

## Interactions worth knowing

- **Shared opacity mechanism.** This writes the same per-window opacity the stock `alpha` plugin
  sets by hand (and that its IPC method sets), so **the last writer wins**: a value you set
  manually is replaced at the next focus change. Not a defect, a consequence of there being one
  opacity per window.
- **Overviews are unaffected.** `spread-overview` forces every window opaque for its session and
  restores the previous value on close; this plugin stands aside for the whole session, so the two
  never fight.
- **On unload** (disabling the plugin, or compositor shutdown) every window this plugin dimmed is
  returned to full opacity — and only those, so a manual value elsewhere is left alone.

## Design

The decision is a pure function (`src/policy.cpp`, ~10 lines) with its own unit tests
(`test/plugins/inactive-alpha-policy.cpp`, `meson test -C build "Inactive alpha policy test"`) —
constitution Principle I. `src/plugin.cpp` is the only Wayfire contact: three signals in, one
transformer out.

- Spec, plan and contracts: [`specs/004-inactive-alpha/`](../../specs/004-inactive-alpha/)
- Verified Wayfire API surface: [`docs/API-MAP-verified.md`](../../docs/API-MAP-verified.md) §7
