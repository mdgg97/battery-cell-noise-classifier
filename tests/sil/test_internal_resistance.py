"""SIL tests: internal resistance and SoH from a passive balancing step (src/cell_ir.c)."""
import numpy as np
import pytest

from cnc import IrQuality, IrStatus, ir_default_config, ir_estimate
from signals import quantise

N, STEP = 129, 64          # 64 samples before, 1 guard, 64 after
R_BOL, R_EOL = 0.040, 0.080  # ohm, same as the C defaults


def balancing_window(r_ohm=0.040, i_a=0.050, noise_uv=150.0, wire_ohm=0.0, seed=0,
                     relax_uv=0.0, relax_tau=20.0, pre_drift_uv=0.0, n=N, step=STEP):
    """Cell at rest; passive balancing switched on at `step` pulls the voltage down by I*R."""
    rng = np.random.default_rng(seed)
    t = np.arange(n, dtype=float)
    v = np.full(n, 3_700_000.0) + rng.normal(0.0, noise_uv, n)
    v += pre_drift_uv * t / (n - 1)
    after = t >= step
    v[after] -= i_a * (r_ohm + wire_ohm) * 1e6
    v[after] -= relax_uv * (1 - np.exp(-(t[after] - step) / relax_tau))  # polarisation
    return quantise(v)


def run(i_ua=50_000, cfg=None, **kw):
    st, res = ir_estimate(balancing_window(i_a=i_ua / 1e6, **kw), STEP, i_ua, cfg)
    assert st == IrStatus.OK
    return res


# ---- accuracy and honest uncertainty --------------------------------------

@pytest.mark.req("REQ-046")
def test_uncertainty_is_honest():
    """|error| <= 2 sigma must hold in about 95 % of runs, and the estimate must be unbiased."""
    errors, sigmas = [], []
    for seed in range(1000):
        res = run(seed=seed)
        assert res.q == IrQuality.OK, res
        errors.append(res.r_uohm - 40_000)
        sigmas.append(res.r_sigma_uohm)
    errors, sigmas = np.array(errors), np.array(sigmas)
    coverage = np.mean(np.abs(errors) <= 2 * sigmas)
    assert 0.92 <= coverage <= 0.98, f"2-sigma coverage {coverage:.3f}"
    assert abs(errors.mean()) < 3 * errors.std() / np.sqrt(errors.size), "biased estimate"


@pytest.mark.req("REQ-046")
@pytest.mark.parametrize("r_mohm", [40, 50, 60, 70, 80])
def test_soh_tracks_aging(r_mohm):
    expected = (R_EOL - r_mohm / 1000) / (R_EOL - R_BOL) * 1000
    socs = [run(r_ohm=r_mohm / 1000, seed=s).soh_permille for s in range(50)]
    assert abs(np.median(socs) - expected) <= 20  # 2 % SoH


@pytest.mark.req("REQ-044", "REQ-046")
def test_precision_versus_balancing_current():
    """Smaller current -> smaller voltage step -> LESS precise. 10 mA mostly fails the 5 % gate."""
    rel_sigma, ok_rate = {}, {}
    for i_ma in (10, 20, 50, 100, 200):
        results = [run(i_ua=i_ma * 1000, seed=s) for s in range(200)]
        rel_sigma[i_ma] = np.median([r.r_sigma_uohm / 40_000 for r in results])
        ok_rate[i_ma] = np.mean([r.q == IrQuality.OK for r in results])
    values = list(rel_sigma.values())
    assert values == sorted(values, reverse=True), rel_sigma
    assert rel_sigma[50] < 0.02 and ok_rate[50] > 0.99
    assert ok_rate[10] < 0.2, ok_rate


# ---- error sources ---------------------------------------------------------

@pytest.mark.req("REQ-042")
def test_sense_wire_must_be_compensated():
    """Internal balancing current flows through the sense wire: 15 mOhm wire looks like aging."""
    raw = run(wire_ohm=0.015, seed=1)
    fixed = run(wire_ohm=0.015, seed=1, cfg=ir_default_config(r_wire_uohm=15_000))
    assert raw.soh_permille < 700, raw           # a new cell reported at ~62 % SoH
    assert fixed.soh_permille > 950, fixed


@pytest.mark.req("REQ-041")
def test_polarisation_inflates_resistance():
    """Characterisation: the result depends on the time scale of the post window."""
    ohmic = run(seed=2)
    with_relax = run(seed=2, relax_uv=1000, relax_tau=20)
    assert with_relax.r_uohm > ohmic.r_uohm + 10_000, (ohmic, with_relax)


# ---- quality gates -----------------------------------------------------------

@pytest.mark.req("REQ-045")
def test_switch_fault_never_reported_ok():
    """Balancing commanded but no current flows: never a valid (and wrong) SoH."""
    for seed in range(200):
        res = run(r_ohm=0.0, seed=seed)
        assert res.q == IrQuality.NO_RESPONSE, (seed, res)


@pytest.mark.req("REQ-044")
def test_spike_in_window_rejected():
    samples = balancing_window(seed=3)
    samples[90] += 120  # 12 mV EMI spike after the step
    _, res = ir_estimate(samples, STEP, 50_000)
    assert res.q == IrQuality.NOISY_WINDOW


@pytest.mark.req("REQ-044")
def test_cell_still_relaxing_from_load_rejected():
    """Pre-step drift (cell not at rest) would bias dV: must be rejected, not measured."""
    res = run(seed=4, pre_drift_uv=-12_000)
    assert res.q == IrQuality.NOISY_WINDOW, res


@pytest.mark.req("REQ-040")
def test_window_errors():
    samples = balancing_window()
    assert ir_estimate(samples, 7, 50_000)[0] == IrStatus.ERR_WINDOW
    assert ir_estimate(samples, N - 8, 50_000)[0] == IrStatus.ERR_WINDOW
