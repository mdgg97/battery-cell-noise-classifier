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

## Embedded constraints

| ID | Requirement |
|----|-------------|
| REQ-030 | No dynamic memory allocation and no floating point in `src/`. |
| REQ-031 | No 64-bit overflow for any input within the allowed length and full 16-bit sample range. |
| REQ-032 | The code shall compile without warnings with `-Wall -Wextra -Wpedantic -Wconversion -Werror` (C99). |
| REQ-033 | The test suite shall run without memory errors under Valgrind. |
