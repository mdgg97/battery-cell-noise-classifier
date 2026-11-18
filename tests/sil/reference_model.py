"""Floating-point reference model of the classifier (independent implementation).

Used for back-to-back testing: same input -> C (integer) and Python (float)
must agree on features within tolerance and on the class.
"""
from dataclasses import dataclass

import numpy as np

from cnc import LSB_UV, Cls, Config, Flag, class_from_flags


@dataclass
class RefResult:
    cls: Cls
    flags: Flag
    mean_uv: float
    p2p_uv: float
    rms_uv: float
    rms_clean_uv: float
    drift_uv: float
    outlier_count: int


def _round_half_away(x: np.ndarray) -> np.ndarray:
    """Same rounding as div_round() in C."""
    return np.sign(x) * np.floor(np.abs(x) + 0.5)


def _detrend(t: np.ndarray, y: np.ndarray) -> tuple[np.ndarray, float]:
    """Residuals (rounded to 1 uV) against a least-squares line, and its slope."""
    slope, intercept = np.polyfit(t, y, 1)
    return _round_half_away(y - (intercept + slope * t)), float(slope)


def _rms(r: np.ndarray) -> float:
    return float(np.sqrt(np.mean(r ** 2)))


def _evaluate_flags(cfg: Config, *, out_of_range: bool, p2p: float, outliers: int,
                    rms_clean: float, drift: float) -> Flag:
    isolated = 0 < outliers <= cfg.spike_max_count
    flags = Flag(0)
    if out_of_range:
        flags |= Flag.OUT_OF_RANGE
    if p2p <= cfg.stuck_max_p2p_uv:
        flags |= Flag.STUCK
    if isolated and np.floor(rms_clean) <= cfg.rms_limit_uv:
        flags |= Flag.SPIKE
    elif np.floor(rms_clean) > cfg.rms_limit_uv or outliers > cfg.spike_max_count:
        flags |= Flag.ELEVATED_NOISE
    if abs(round(drift)) > cfg.drift_limit_uv:
        flags |= Flag.DRIFT
    return flags


def reference_classify(samples: np.ndarray, cfg: Config) -> RefResult:
    y = samples.astype(float) * LSB_UV
    t = np.arange(y.size, dtype=float)

    resid, slope = _detrend(t, y)
    is_outlier = np.abs(resid) > cfg.spike_limit_uv
    outliers = int(is_outlier.sum())
    rms = _rms(resid)

    if 0 < outliers <= cfg.spike_max_count:
        clean_resid, _ = _detrend(t[~is_outlier], y[~is_outlier])  # robust refit
        rms_clean = _rms(clean_resid)
    else:
        rms_clean = rms

    p2p = float(y.max() - y.min())
    drift = slope * (y.size - 1)
    flags = _evaluate_flags(
        cfg,
        out_of_range=bool(np.any((y < cfg.v_min_uv) | (y > cfg.v_max_uv))),
        p2p=p2p, outliers=outliers, rms_clean=rms_clean, drift=drift,
    )
    return RefResult(class_from_flags(flags), flags, float(y.mean()), p2p, rms,
                     rms_clean, drift, outliers)
