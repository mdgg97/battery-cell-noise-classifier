"""Software-in-the-loop scenario tests: realistic synthetic cell signals -> C classifier."""
import ctypes

import numpy as np
import pytest

from cnc import MAX_SAMPLES, MIN_SAMPLES, Cls, Flag, Result, Status, classify, classify_raw, default_config
from signals import SignalSpec, generate


# ---- API contract ------------------------------------------------------------

@pytest.mark.req("REQ-001")
@pytest.mark.parametrize("n,expected", [
    (MIN_SAMPLES - 1, Status.ERR_LENGTH),
    (MIN_SAMPLES, Status.OK),
    (MAX_SAMPLES, Status.OK),
    (MAX_SAMPLES + 1, Status.ERR_LENGTH),
])
def test_window_length(n, expected):
    st, _ = classify(generate(SignalSpec(n=n)))
    assert st == expected


@pytest.mark.req("REQ-002", "REQ-003")
def test_null_samples_is_failsafe():
    res = Result(cls=int(Cls.OK))
    cfg = default_config()
    st = classify_raw(None, 64, ctypes.byref(cfg), ctypes.byref(res))
    assert st == Status.ERR_NULL
    assert res.klass == Cls.INVALID


@pytest.mark.req("REQ-004", "REQ-003")
@pytest.mark.parametrize("override", [
    {"v_min_uv": 4_300_000},
    {"rms_limit_uv": 0},
    {"spike_limit_uv": 400},
    {"spike_max_count": 0},
])
def test_bad_config_rejected(override):
    st, res = classify(generate(SignalSpec()), default_config(**override))
    assert st == Status.ERR_CONFIG
    assert res.klass == Cls.INVALID


# ---- classes on realistic signals -------------------------------------------

@pytest.mark.req("REQ-011")
@pytest.mark.parametrize("seed", range(20))
def test_clean_cell_is_ok(seed):
    st, res = classify(generate(SignalSpec(noise_rms_uv=150, seed=seed)))
    assert st == Status.OK
    assert res.klass == Cls.OK, res


@pytest.mark.req("REQ-021")
def test_false_alarm_rate_on_clean_cells():
    alarms = []
    for seed in range(500):
        _, res = classify(generate(SignalSpec(noise_rms_uv=150, seed=seed)))
        if res.klass != Cls.OK:
            alarms.append((seed, res))
    assert alarms == []


@pytest.mark.req("REQ-008")
@pytest.mark.parametrize("seed", range(20))
def test_loose_contact_noise(seed):
    _, res = classify(generate(SignalSpec(noise_rms_uv=1500, seed=seed)))
    assert res.klass == Cls.ELEVATED_NOISE, res


@pytest.mark.req("REQ-008")
@pytest.mark.parametrize("seed", range(10))
def test_strong_noise_is_not_mistaken_for_spikes(seed):
    # sigma = 2.5 mV: some samples exceed the 5 mV spike limit by chance,
    # but the remaining signal is noisy, so this must be noise, not SPIKE.
    _, res = classify(generate(SignalSpec(noise_rms_uv=2500, seed=seed)))
    assert res.klass == Cls.ELEVATED_NOISE, res


@pytest.mark.req("REQ-007")
@pytest.mark.parametrize("idx", [0, 1, 31, 62, 63])
@pytest.mark.parametrize("amp", [+12_000, -12_000])
def test_emi_spike_any_position(idx, amp):
    _, res = classify(generate(SignalSpec(spikes={idx: amp}, seed=idx)))
    assert res.klass == Cls.SPIKE, res
    assert res.outlier_count == 1


@pytest.mark.req("REQ-007", "REQ-008")
@pytest.mark.parametrize("count,expected", [(1, Cls.SPIKE), (2, Cls.SPIKE),
                                            (3, Cls.ELEVATED_NOISE), (6, Cls.ELEVATED_NOISE)])
def test_spike_burst_length(count, expected):
    spikes = {10 + 7 * k: 10_000 for k in range(count)}
    _, res = classify(generate(SignalSpec(spikes=spikes, seed=3)))
    assert res.klass == expected, res


@pytest.mark.req("REQ-009")
@pytest.mark.parametrize("drift", [+5_000, -5_000, +20_000])
def test_thermal_drift(drift):
    _, res = classify(generate(SignalSpec(drift_uv=drift, seed=7)))
    assert res.klass == Cls.DRIFT, res
    assert res.drift_uv == pytest.approx(drift, abs=300)
    assert res.rms_uv < 300  # the trend must not appear as noise


@pytest.mark.req("REQ-009")
def test_drift_does_not_inflate_rms():
    base = classify(generate(SignalSpec(seed=11)))[1]
    ramp = classify(generate(SignalSpec(seed=11, drift_uv=30_000)))[1]
    assert abs(int(ramp.rms_uv) - int(base.rms_uv)) <= 10


@pytest.mark.req("REQ-006")
def test_frozen_adc_register():
    samples = np.full(64, 37_123, dtype=np.uint16)
    _, res = classify(samples)
    assert res.klass == Cls.STUCK


@pytest.mark.req("REQ-005")
@pytest.mark.parametrize("value", [0, 24_999, 43_001, 65_535])
def test_out_of_range_single_sample(value):
    samples = generate(SignalSpec(seed=1))
    samples[20] = value
    _, res = classify(samples)
    assert res.klass == Cls.OUT_OF_RANGE


@pytest.mark.req("REQ-010")
def test_precedence_all_flags_reported():
    samples = generate(SignalSpec(drift_uv=10_000, spikes={30: 15_000}, seed=4))
    _, res = classify(samples)
    assert res.klass == Cls.SPIKE
    assert Flag(res.flags) == Flag.SPIKE | Flag.DRIFT


@pytest.mark.req("REQ-031")
def test_extreme_input_full_scale_noise():
    rng = np.random.default_rng(0)
    samples = rng.integers(0, 65536, MAX_SAMPLES, dtype=np.uint16)
    st, res = classify(samples, default_config(v_min_uv=0, v_max_uv=6_553_500))
    assert st == Status.OK
    # uniform distribution over 0..6.5535 V: sigma ~ 1.89 V
    assert 1_700_000 < res.rms_uv < 2_100_000
