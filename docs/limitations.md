# Known limitations

Found while triaging the GenAI-assisted edge cases (`tests/sil/genai_edge_cases.json`).
Each one is covered by a strict `xfail` test (or a pinned characterisation test), so a
change in behaviour is noticed immediately.

## L1 — STUCK false positive on very quiet cells (GA-01)
With 100 µV per LSB, a healthy cell with less than about 30 µV noise can produce the same
code for a whole window and is reported as `STUCK`.
*Mitigation options:* longer windows, ADC dithering, or confirm STUCK only after several
consecutive windows (debounce in the caller).

## L2 — Interference synchronous to the sample rate is invisible (GA-03)
If a disturbance runs at an integer multiple of the sampling frequency, every sample sees
the same phase and the disturbance looks like a constant offset.
*Mitigation options:* sampling with jitter, or an anti-aliasing filter in the AFE. This is
a system-level property; no window statistic can detect it.

## L3 — Strongly curved trends are reported as noise (GA-05)
The detrend is linear. A fast exponential relaxation after a load step leaves curvature in
the residuals and is classified `ELEVATED_NOISE` (with the `DRIFT` flag set).
*Mitigation options:* quadratic detrend, or suppress noise classification for a defined
time after a known load step.

## L4 — No dedicated class for step changes (GA-10)
A step (for example when cell balancing switches on) is reported as `ELEVATED_NOISE` +
`DRIFT`. The behaviour is pinned by GA-10. A `STEP` class based on the largest
first difference is a candidate feature.

## L5 — Outlier masking at the window ends (GA-11)
End points have the highest leverage on the regression line. Two opposite outliers at
both ends of a short window can pull the line so far that neither exceeds the spike
limit; the window is classified `ELEVATED_NOISE` instead of `SPIKE`.
*Mitigation options:* a robust estimator (for example median of first differences, or
Theil–Sen slope), at the cost of more CPU time.

## L6 — Resistance depends on the time scale (cell_ir)
The post window includes fast polarisation, so R is larger than the pure ohmic value
and depends on window length and sample rate. Compare R values only for the same
window settings. Pinned by `test_polarisation_inflates_resistance`.

## L7 — 10 mA balancing current is not enough with 64-sample windows (cell_ir)
The 0.4 mV step gives about 6.5 % uncertainty, above the 5 % gate, so no window is
accepted. Options: longer windows, averaging over several balancing events, or more
current. Pinned by `test_precision_versus_balancing_current`.

## Fixed defects

| ID | Found by | Description | Fix |
|----|----------|-------------|-----|
| DEF-002 | `test_switch_fault_never_reported_ok` | A broken balancing switch was reported `UNCERTAIN` instead of `NO_RESPONSE` in about 1 of 40 windows: noise alone exceeded the 2σ threshold. | Physical plausibility check `r_min_uohm` (default R_bol / 2) in addition to the statistical test. |
| DEF-001 | GA-06 | A single spike in an 8-sample window was classified `ELEVATED_NOISE`: the spike bent the fitted line and inflated every other residual. | Robust refit: outliers are removed and the clean RMS is computed against a second fit (`src/cell_noise.c`, pass 3). Regression test: GA-06. |
