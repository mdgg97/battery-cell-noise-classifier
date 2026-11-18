"""Synthetic cell voltage signals, quantised the way an AFE delivers them."""
from dataclasses import dataclass, field

import numpy as np

from cnc import LSB_UV


@dataclass
class SignalSpec:
    n: int = 64
    base_uv: float = 3_700_000.0       # cell voltage
    noise_rms_uv: float = 150.0        # white Gaussian noise
    drift_uv: float = 0.0              # linear change over the window
    spikes: dict = field(default_factory=dict)  # index -> amplitude in uV
    hum_amp_uv: float = 0.0            # sinusoidal interference (e.g. inverter ripple)
    hum_cycles: float = 0.0            # periods per window
    exp_step_uv: float = 0.0           # relaxation after a load step: A * exp(-t/tau)
    exp_tau: float = 10.0              # in samples
    step_at: int = -1                  # sample index of a step change (-1 = none)
    step_uv: float = 0.0               # step height
    seed: int = 0


def generate(spec: SignalSpec) -> np.ndarray:
    rng = np.random.default_rng(spec.seed)
    t = np.arange(spec.n, dtype=float)
    v = np.full(spec.n, spec.base_uv, dtype=float)
    v += rng.normal(0.0, spec.noise_rms_uv, spec.n) if spec.noise_rms_uv > 0 else 0.0
    if spec.n > 1:
        v += spec.drift_uv * t / (spec.n - 1)
    if spec.hum_amp_uv:
        v += spec.hum_amp_uv * np.sin(2 * np.pi * spec.hum_cycles * t / spec.n)
    if spec.exp_step_uv:
        v += spec.exp_step_uv * np.exp(-t / spec.exp_tau)
    if spec.step_at >= 0:
        v[spec.step_at:] += spec.step_uv
    for idx, amp in spec.spikes.items():
        v[int(idx)] += amp
    return quantise(v)


def quantise(v_uv: np.ndarray) -> np.ndarray:
    return np.clip(np.round(v_uv / LSB_UV), 0, 65535).astype(np.uint16)
