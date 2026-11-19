# Battery Cell Noise Classifier

![CI](https://github.com/mdgg97/battery-cell-noise-classifier/actions/workflows/ci.yml/badge.svg)

Embedded C logic that classifies the **measurement noise of a battery cell voltage
signal**, plus a layered test setup: C unit tests, pytest software-in-the-loop (SIL) tests,
back-to-back testing against a reference model, and GenAI-assisted edge-case design.

Personal embedded QA portfolio project by Mustafa Deniz Gökcan.

## What it does

A battery monitoring AFE delivers cell voltages as 16-bit codes (here 1 LSB = 100 µV).
For each window of 8–256 samples the classifier reports:

| Class | Typical cause |
|-------|---------------|
| `OUT_OF_RANGE` | open wire, ADC saturation, wrong channel mapping |
| `STUCK` | frozen ADC, stale register |
| `SPIKE` | isolated EMI burst on an otherwise clean signal |
| `ELEVATED_NOISE` | loose contact, bad grounding, inverter ripple |
| `DRIFT` | thermal or reference drift |
| `OK` | none of the above |

Plus the features behind the decision: mean, peak-to-peak, detrended RMS, clean RMS,
drift and outlier count.

```c
cnc_config_t cfg;
cnc_result_t res;
cnc_default_config(&cfg);
if (cnc_classify(samples, 64, &cfg, &res) == CNC_STATUS_OK) {
    printf("%s, rms = %u uV\n", cnc_class_name(res.cls), res.rms_uv);
}
```

### Embedded design choices
- integer only, no heap, no recursion, O(n) — checked by `make lint`
- freestanding: no libc headers needed
- least-squares detrend in 64-bit fixed point (Q18), with a documented overflow budget
- per-sample work uses only multiply and shift: a 256-sample window needs 8 64-bit
  divisions instead of 515 (Cortex-M has no 64-bit divide instruction)
- robust refit without outliers, so one spike does not look like broadband noise
- fail-safe: any error returns class `INVALID`, never `OK`

## Test strategy

```
requirements.md ──► unit tests (C)        hand-built signals, boundaries, overflow
                ├─► SIL tests (pytest)     realistic synthetic signals → real C code via ctypes
                ├─► back-to-back           C (integer) vs. NumPy (float) model, 1000 random windows
                └─► GenAI edge cases       proposed by an LLM, executed and triaged
                         ▲
        trace_matrix.py checks every requirement has a test
```

| Check | Command |
|-------|---------|
| C unit tests | `make unit` |
| SIL tests | `make sil` |
| Same results at -O0, -O1, -O2, -O3, -Os | `make optlevels` |
| AddressSanitizer + UBSan | `make sanitize` |
| Valgrind | `make memcheck` |
| Line/branch coverage (gcov) | `make coverage` |
| Requirements traceability | `make trace` |
| 64-bit divisions per window are constant | `make divcount` |
| Unit tests on Cortex-M4 (QEMU mps2-an386) | `make arm qemu` |
| Flash / stack on the target | `make arm-report` |
| Everything | `make test` |

Current status: 23 C unit tests (on the host and on Cortex-M4), 95 SIL tests + 4 strict
`xfail` (known limitations), 100 % line coverage, all requirements traced.

### On the target (Cortex-M4, arm-none-eabi-gcc -Os, from CI)

| Resource | Value |
|----------|-------|
| Flash (library) | 1.8 KB |
| Static RAM | 0 bytes |
| Worst-case stack | about 300 bytes (`cnc_classify` 224 + `fit_line` 72) |
| 64-bit divisions per 256-sample window | 8 (was 515) |

QEMU is not cycle-accurate, so execution time still needs a real board.

### GenAI-assisted testing
An LLM was given only the requirements and the API and asked for risky edge cases
(quantisation, aliasing, regression leverage, balancing steps, ...). Every proposal was
run against the real code and triaged:

- **1 defect found and fixed** — a single spike in an 8-sample window was misclassified
  as noise (DEF-001)
- **5 limitations documented** — see [`docs/limitations.md`](docs/limitations.md)
- the rest kept as regression tests

Details: [`docs/genai-test-design.md`](docs/genai-test-design.md)

## Build

Requirements: gcc, make, Python 3.10+, `pip install pytest numpy`, optionally valgrind.
For the target: `gcc-arm-none-eabi`, `libnewlib-arm-none-eabi`, `qemu-system-arm`.

```bash
make test
```

## Repository layout

```
include/cell_noise.h        public API
src/cell_noise.c            implementation
tests/unit/                 C unit tests (no external framework)
tests/sil/                  pytest SIL tests, signal generator, reference model, GenAI cases
target/                     Cortex-M4 startup code and linker script (QEMU mps2-an386)
tools/trace_matrix.py       requirements → tests matrix
tools/count_divisions.py    REQ-034 check: 64-bit divisions per window
docs/                       requirements, limitations, GenAI test design
```

## Next steps
- measure execution time on a real board (STM32 Nucleo) and add a hardware-in-the-loop test
- internal resistance from step changes as a State-of-Health indicator
- `STEP` class for balancing events (L4)
- robust slope estimator against end-point masking (L5)
