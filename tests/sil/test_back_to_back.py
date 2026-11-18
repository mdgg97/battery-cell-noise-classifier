"""Back-to-back test: integer C implementation vs. floating-point reference model."""
import numpy as np
import pytest

from cnc import Cls, classify, default_config
from reference_model import reference_classify
from signals import SignalSpec, generate

TOL_UV = 2


def _random_spec(rng: np.random.Generator, seed: int) -> SignalSpec:
    n = int(rng.integers(8, 257))
    spikes = {}
    if rng.random() < 0.3:
        for _ in range(int(rng.integers(1, 4))):
            spikes[int(rng.integers(0, n))] = float(rng.choice([-1, 1]) * rng.uniform(4_000, 20_000))
    return SignalSpec(
        n=n,
        base_uv=float(rng.uniform(2_600_000, 4_200_000)),
        noise_rms_uv=float(rng.choice([0, 50, 150, 400, 800, 2000])),
        drift_uv=float(rng.choice([0, 0, rng.uniform(-10_000, 10_000)])),
        spikes=spikes,
        seed=seed,
    )


def _near_threshold(ref, cfg) -> bool:
    """True if a feature lies within tolerance of a decision threshold."""
    return (abs(ref.rms_clean_uv - cfg.rms_limit_uv) <= TOL_UV
            or abs(abs(ref.drift_uv) - cfg.drift_limit_uv) <= TOL_UV)


@pytest.mark.req("REQ-020")
def test_features_match_reference():
    rng = np.random.default_rng(2026)
    cfg = default_config()
    mismatches = []
    for seed in range(1000):
        samples = generate(_random_spec(rng, seed))
        _, c = classify(samples, cfg)
        ref = reference_classify(samples, cfg)

        deltas = {
            "mean": abs(c.mean_uv - ref.mean_uv),
            "p2p": abs(c.p2p_uv - ref.p2p_uv),
            "rms": abs(c.rms_uv - ref.rms_uv),
            "drift": abs(c.drift_uv - ref.drift_uv),
        }
        bad = {k: v for k, v in deltas.items() if v > TOL_UV}
        if bad:
            mismatches.append((seed, bad))
        if Cls(c.cls) != ref.cls and not _near_threshold(ref, cfg):
            mismatches.append((seed, f"class C={Cls(c.cls).name} ref={ref.cls.name}"))
        if c.outlier_count != ref.outlier_count:
            mismatches.append((seed, f"outliers C={c.outlier_count} ref={ref.outlier_count}"))
    assert mismatches == []
