# SF-004 — Note Resolution

Implement absolute notes, Relative Values, Pass, Relative Pass-through, Force Scale, Document Scale/Root, and deterministic random value behavior.

Acceptance:
- Unit tests cover positive/negative intervals and scale boundaries.
- Pass overrides Relative Values.
- Force Scale only quantizes absolute out-of-scale notes.
- Unresolved edge cases are explicit policies, not implicit assumptions.
