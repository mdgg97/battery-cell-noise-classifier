# GenAI-assisted test design

GenAI was used to *propose* edge cases. It was not trusted to decide whether the code is
right: every proposal was executed against the real C code and triaged by a human.

## Process

1. **Input to the model** — `docs/requirements.md` and `include/cell_noise.h` (requirements
   and API only, not the implementation, so the proposals are not biased by the code).
2. **Prompt** (shortened):
   > You are a test engineer for a battery management system. Given these requirements and
   > this API, list edge cases where the classifier is most likely to be wrong. Think about
   > the physics of a Li-ion cell, the AFE (quantisation, sampling, open wire), EMI and the
   > statistics (leverage, masking, small windows). For each case give: signal description,
   > expected class according to the requirements, and why it is risky.
3. **Translate** each accepted proposal into a data-driven case in
   `tests/sil/genai_edge_cases.json` (signal parameters + expected class).
4. **Execute** the cases against the C library (SIL, `test_genai_edge_cases.py`).
5. **Triage** every result into one of three outcomes:

| Outcome | Action | Cases |
|---------|--------|-------|
| Confirmed | Kept as regression test | GA-02, GA-04, GA-07, GA-08, GA-09, GA-12 |
| Defect found | Code fixed, case kept as regression test | GA-06 → DEF-001 |
| Limitation | Documented in `limitations.md`, kept as strict `xfail` | GA-01, GA-03, GA-05, GA-11 |
| Behaviour pinned | Undefined by requirements; current behaviour asserted and documented | GA-10 |

## Review rules

- The expected class comes from the **requirements**, not from what the code does.
  If the code disagrees, that is a finding (defect or limitation), never a silent change
  of the expectation.
- Proposals that were physically unrealistic or duplicated existing tests were dropped.
- Known limitations run as `xfail(strict=True)`: if a later change fixes one, the test
  "unexpectedly passes" and CI fails until the case is promoted to `pass`.
- A strict mutation probe (manually flipping comparisons in `src/cell_noise.c`) was used
  to check that the tests actually detect faults; one surviving mutant led to the
  boundary test `test_spike_limit_is_exclusive`.

## What GenAI was good and bad at

- Good: broad, domain-aware brainstorming (aliasing, leverage of end points, balancing steps).
- Bad: it cannot know the exact numeric behaviour; several expectations needed a real run to
  decide whether they described a bug or an unrealistic requirement.
