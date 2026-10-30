# Specification Quality Checklist: Native Inactive-Window Dimming

**Purpose**: Validate specification completeness and quality before planning
**Created**: 2026-10-29
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

- All three clarifications were answered by the user before the spec was written and are recorded
  in § Clarifications, so no markers remain.
- The spec deliberately says "the compositor's existing per-window opacity feature" (FR-006) rather
  than naming the transformer, and "any plugin active on the output" (FR-003) rather than naming a
  signal — the mechanisms are pinned in `plan.md`'s Principle IV table instead.
- One known risk is carried in § Assumptions rather than hidden: plugins that activate briefly for
  animations (a workspace switch) count as "screen-taking" under FR-003, which may flicker opacity.
  The agreed fallback is a debounce, explicitly **not** a plugin-name list — that list is the design
  this feature exists to remove. The J2 gate looks for it directly (quickstart §J2 step 8).
- Two accepted interactions are documented rather than specified away: a manual per-window opacity
  is overridden at the next focus change, and multi-output behaviour must merely be harmless on a
  single-output machine.
