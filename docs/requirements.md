# Requirements

Each requirement has an ID that test cases reference. `tools/trace_matrix.py` scans the
test sources for these IDs and fails CI if a requirement has no test.

## Functional

| ID | Requirement |
|----|-------------|
| REQ-001 | The classifier shall accept windows of 8 to 256 samples and reject any other length with `CNC_STATUS_ERR_LENGTH`. |
| REQ-002 | NULL pointers for samples, config or result shall be rejected with `CNC_STATUS_ERR_NULL`. |
| REQ-003 | Fail-safe: on any error the result shall be written with class `INVALID` and all features zero; an error shall never produce `OK`. |
| REQ-004 | Inconsistent configurations (see `cnc_validate_config`) shall be rejected with `CNC_STATUS_ERR_CONFIG`. |
| REQ-005 | A window with at least one sample outside `[v_min, v_max]` shall be classified `OUT_OF_RANGE`. |
| REQ-006 | A window whose peak-to-peak is at or below `stuck_max_p2p_uv` shall be classified `STUCK`. |
| REQ-007 | 1 to `spike_max_count` outliers above `spike_limit_uv` on a signal whose remaining RMS is within `rms_limit_uv` shall be classified `SPIKE`. |
| REQ-008 | A window whose detrended RMS (outliers excluded) exceeds `rms_limit_uv`, or with more outliers than `spike_max_count`, shall be classified `ELEVATED_NOISE`. |
| REQ-009 | A window whose fitted change over the window exceeds `drift_limit_uv` in magnitude shall be classified `DRIFT`; a pure linear trend shall not raise the RMS. |
| REQ-010 | When several findings apply, the class shall follow the precedence OUT_OF_RANGE > STUCK > SPIKE > ELEVATED_NOISE > DRIFT > OK, and all findings shall be reported in `flags`. |
| REQ-011 | A clean signal (noise RMS well below `rms_limit_uv`, no trend) shall be classified `OK`. |

## Accuracy

| ID | Requirement |
|----|-------------|
| REQ-020 | Mean, peak-to-peak, RMS and drift shall match a floating-point reference model within ±2 µV (back-to-back test). |
| REQ-021 | Over 500 random clean windows (σ = 150 µV) the false-alarm rate shall be 0 %. |

## Internal resistance and SoH (`cell_ir`)

| ID | Requirement |
|----|-------------|
| REQ-040 | `cir_estimate` shall reject NULL pointers, inconsistent configurations and windows with fewer than 8 samples before or after the step; on error the result is INVALID with all values zero. |
| REQ-041 | R shall be computed as -dV / I from the means of the windows before and after the balancing step; the guard samples after the step shall be excluded. |
| REQ-042 | The configured sense-wire resistance shall be subtracted from the measured resistance. |
| REQ-043 | SoH shall be (R_eol - R) / (R_eol - R_bol), in permille, clamped to 0..1000. |
| REQ-044 | A result shall be accepted (quality OK) only if the current is at least `min_current_ua`, both windows classify OK with the noise classifier, and sigma(R)/R is at most `max_rel_sigma_permille`. |
| REQ-045 | A missing or implausible voltage drop (below 2 sigma, wrong direction, or R below `r_min_uohm`) shall be reported as NO_RESPONSE, never as a valid SoH. |
| REQ-046 | The reported uncertainty shall be honest: over 1000 noisy windows, 92–98 % of the errors lie within ±2 sigma, and the estimate is unbiased. |

## Embedded constraints

| ID | Requirement |
|----|-------------|
| REQ-030 | No dynamic memory allocation and no floating point in `src/`. |
| REQ-031 | No 64-bit overflow for any input within the allowed length and full 16-bit sample range. |
| REQ-032 | The code shall compile without warnings with `-Wall -Wextra -Wpedantic -Wconversion -Werror` (C99). |
| REQ-033 | The test suite shall run without memory errors under Valgrind. |
| REQ-034 | The number of 64-bit divisions per window shall be constant, independent of the window length (Cortex-M has no 64-bit divide instruction). |
| REQ-035 | The library shall build freestanding (no libc headers) and the unit tests shall pass on a Cortex-M4 target (QEMU mps2-an386). |
