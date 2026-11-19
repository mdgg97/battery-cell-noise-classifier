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

### Internal resistance and State of Health (`cell_ir`)

When the firmware switches passive balancing on, the bleed current I pulls the
cell voltage down by dV = I x R. Averaging the samples before and after the step
gives R, its uncertainty, and a resistance-based SoH:

```
SoH_R = (R_eol - R) / (R_eol - R_bol)      defaults: R_bol = 40 mOhm, R_eol = 80 mOhm
```

A value is only reported as valid when it can be trusted: enough current, both
windows clean (the noise classifier is the quality gate), uncertainty below 5 %,
and a physically plausible drop. Otherwise the quality code says why
(`LOW_CURRENT`, `NOISY_WINDOW`, `NO_RESPONSE`, `UNCERTAIN`).

Precision versus balancing current (simulated, 40 mOhm cell, 150 µV noise,
64 + 64 samples, 500 runs each):

| Balancing current | Voltage step | Median uncertainty of R | Windows accepted |
|---|---|---|---|
| 10 mA | 0.4 mV | 6.5 % | 0 % |
| 20 mA | 0.8 mV | 3.2 % | 100 % |
| 50 mA | 2.0 mV | 1.3 % | 100 % |
| 100 mA | 4.0 mV | 0.7 % | 100 % |
| 200 mA | 8.0 mV | 0.3 % | 100 % |

A smaller current makes the measurement *less* precise: the voltage step shrinks
while the noise stays. The reported uncertainty is checked to be honest: 94.8 % of
the errors lie within ±2σ (target 95 %).

Pitfalls covered by tests:
- with AFE-internal balancing the current flows through the sense wire, so the wire
  resistance looks like ageing; it must be calibrated and set in `r_wire_uohm`
- the value depends on the time scale of the post window (ohmic + polarisation)
- a broken balancing switch is reported as `NO_RESPONSE`, never as a SoH value

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

Current status: 37 C unit tests (on the host and on Cortex-M4), 108 SIL tests + 4 strict
`xfail` (known limitations), 100 % line coverage, all requirements traced.

### On the target (Cortex-M4, arm-none-eabi-gcc -Os, from CI)

| Resource | Value |
|----------|-------|
| Flash (noise classifier) | 1.8 KB |
| Flash (internal resistance) | see CI step summary |
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
include/cell_noise.h        noise classifier API
include/cell_ir.h           internal resistance / SoH API
src/                        implementation
tests/unit/                 C unit tests (no external framework)
tests/sil/                  pytest SIL tests, signal generator, reference model, GenAI cases
target/                     Cortex-M4 startup code and linker script (QEMU mps2-an386)
tools/trace_matrix.py       requirements → tests matrix
tools/count_divisions.py    REQ-034 check: 64-bit divisions per window
docs/                       requirements, limitations, GenAI test design
```

## Next steps
- measure execution time on a real board (STM32 Nucleo) and add a hardware-in-the-loop test
- trend of R over many balancing events (filtering, temperature compensation)
- validate R against an impedance or DC-pulse measurement on a real 18650 cell
- `STEP` class for balancing events (L4)
- robust slope estimator against end-point masking (L5)
