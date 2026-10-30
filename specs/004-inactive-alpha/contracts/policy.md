# Contract: the pure dim policy

**NORMATIVE.** The plugin's only decision logic, extracted so it is testable without a compositor
(constitution Principle I). Zero Wayfire dependencies.

## Types and function

```cpp
namespace wf::inactive_alpha
{
struct policy_input
{
    uint32_t focused = 0;     // the window that must stay opaque; 0 = none known
    bool     suppressed = false;
    double   dim = 0.85;      // configured opacity for non-focused windows
};

// The opacity this window should be rendered at.
double target_alpha(const policy_input& in, uint32_t view_id);
}
```

## Definition

```
target_alpha(in, id) =
    1.0                      if in.suppressed                       // P2
    1.0                      if id == in.focused                    // P1
    clamp(in.dim, 0.0, 1.0)  otherwise                              // P3
```

`focused == 0` means "no window currently holds keyboard focus"; the caller is responsible for
keeping the **last** focused window's id there rather than clearing it, which is what makes FR-008
("focus moving to a panel must not dim everything") a caller rule and keeps this function total.

## Invariants (asserted by doctest)

- **#P1 — The focused window is never dimmed.** `target_alpha(in, in.focused) == 1.0` for every
  input, suppressed or not.
- **#P2 — Suppression wins.** `in.suppressed` ⇒ `target_alpha(in, id) == 1.0` for every `id`,
  whatever `dim` is. (This is the property the retired script got wrong by keying on plugin names.)
- **#P3 — Others get exactly the configured value**, clamped to `[0,1]`; `dim == 1.0` is a no-op
  dimming and must not produce anything other than `1.0`.
- **#P4 — Totality and purity**: defined for every `view_id` including ids not currently mapped and
  `0`; no global state, no time, no RNG; identical inputs give identical output.
- **#P5 — Monotonic in `dim`**: for a non-focused, non-suppressed window the result equals the
  clamped `dim`, so lowering the option can only darken.

## Tests

1. #P1 with the focused id, suppressed and not.
2. #P2 across `dim ∈ {0, 0.5, 0.85, 1}` and several ids.
3. #P3 including `dim = 1.0`, `dim = 0.0`, and out-of-range `-1` / `2` (clamped).
4. #P4 with `focused = 0` (nothing focused) — the result must be the dim value for every id, which
   is why the caller must retain the last focused id instead of zeroing it.
5. A table-driven pass over the cross product of {suppressed, not} × {focused, other} × four `dim`
   values, asserting the three-line definition exactly.

## Caller rules (not part of the pure function, verified at the J2 gate)

- Keep the **last** focused window in `focused`; do not clear it when keyboard focus moves to a
  non-window (FR-008).
- Recompute and apply for every window on: focus change, window mapped, suppression begin/end, and
  option change (FR-002/FR-004).
- Apply by writing the `"alpha"`-named 2D transformer, the same one the stock per-window opacity
  feature uses (FR-006); drop the transformer when the target is `1.0` so no idle node is left.
- On unload, apply `1.0` to every window this plugin dimmed (FR-005).
