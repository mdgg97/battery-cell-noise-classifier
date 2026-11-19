"""ctypes binding to the C classifier — the exact production code runs in the SIL tests."""
import ctypes
import enum
import os
from pathlib import Path

import numpy as np

_LIB_PATH = Path(os.environ.get("CNC_LIB", Path(__file__).resolve().parents[2] / "build" / "libcellnoise.so"))

LSB_UV = 100
MIN_SAMPLES = 8
MAX_SAMPLES = 256


class Cls(enum.IntEnum):
    INVALID = 0
    OUT_OF_RANGE = 1
    STUCK = 2
    SPIKE = 3
    ELEVATED_NOISE = 4
    DRIFT = 5
    OK = 6


class Flag(enum.IntFlag):
    OUT_OF_RANGE = 1 << 0
    STUCK = 1 << 1
    SPIKE = 1 << 2
    ELEVATED_NOISE = 1 << 3
    DRIFT = 1 << 4


# Highest-precedence finding decides the class (REQ-010); mirrors class_from_flags() in C.
PRECEDENCE = (
    (Flag.OUT_OF_RANGE, Cls.OUT_OF_RANGE),
    (Flag.STUCK, Cls.STUCK),
    (Flag.SPIKE, Cls.SPIKE),
    (Flag.ELEVATED_NOISE, Cls.ELEVATED_NOISE),
    (Flag.DRIFT, Cls.DRIFT),
)


def class_from_flags(flags: Flag) -> Cls:
    return next((cls for flag, cls in PRECEDENCE if flags & flag), Cls.OK)


class Status(enum.IntEnum):
    OK = 0
    ERR_NULL = 1
    ERR_LENGTH = 2
    ERR_CONFIG = 3


class Config(ctypes.Structure):
    _fields_ = [
        ("v_min_uv", ctypes.c_uint32),
        ("v_max_uv", ctypes.c_uint32),
        ("stuck_max_p2p_uv", ctypes.c_uint32),
        ("rms_limit_uv", ctypes.c_uint32),
        ("spike_limit_uv", ctypes.c_uint32),
        ("spike_max_count", ctypes.c_uint16),
        ("drift_limit_uv", ctypes.c_uint32),
    ]


class Result(ctypes.Structure):
    _fields_ = [
        ("cls", ctypes.c_int),
        ("flags", ctypes.c_uint8),
        ("mean_uv", ctypes.c_uint32),
        ("p2p_uv", ctypes.c_uint32),
        ("rms_uv", ctypes.c_uint32),
        ("rms_clean_uv", ctypes.c_uint32),
        ("drift_uv", ctypes.c_int32),
        ("outlier_count", ctypes.c_uint16),
    ]

    @property
    def klass(self) -> Cls:
        return Cls(self.cls)

    def __repr__(self) -> str:
        return (f"Result({self.klass.name}, flags={Flag(self.flags)!r}, mean={self.mean_uv}uV, "
                f"p2p={self.p2p_uv}uV, rms={self.rms_uv}uV, rms_clean={self.rms_clean_uv}uV, "
                f"drift={self.drift_uv}uV, outliers={self.outlier_count})")


_lib = ctypes.CDLL(str(_LIB_PATH))
_lib.cnc_default_config.argtypes = [ctypes.POINTER(Config)]
_lib.cnc_default_config.restype = ctypes.c_int
_lib.cnc_validate_config.argtypes = [ctypes.POINTER(Config)]
_lib.cnc_validate_config.restype = ctypes.c_int
_lib.cnc_classify.argtypes = [ctypes.POINTER(ctypes.c_uint16), ctypes.c_size_t,
                              ctypes.POINTER(Config), ctypes.POINTER(Result)]
_lib.cnc_classify.restype = ctypes.c_int


def default_config(**overrides) -> Config:
    cfg = Config()
    _lib.cnc_default_config(ctypes.byref(cfg))
    for key, value in overrides.items():
        setattr(cfg, key, value)
    return cfg


def classify(samples, cfg: Config | None = None) -> tuple[Status, Result]:
    """Run the C classifier on a sequence of raw 16-bit samples."""
    arr = np.ascontiguousarray(samples, dtype=np.uint16)
    cfg = cfg if cfg is not None else default_config()
    res = Result()
    st = _lib.cnc_classify(arr.ctypes.data_as(ctypes.POINTER(ctypes.c_uint16)),
                           arr.size, ctypes.byref(cfg), ctypes.byref(res))
    return Status(st), res


# ---- internal resistance (cell_ir.h) ------------------------------------------

class IrQuality(enum.IntEnum):
    INVALID = 0
    OK = 1
    LOW_CURRENT = 2
    NOISY_WINDOW = 3
    NO_RESPONSE = 4
    UNCERTAIN = 5


class IrStatus(enum.IntEnum):
    OK = 0
    ERR_NULL = 1
    ERR_WINDOW = 2
    ERR_CONFIG = 3


class IrConfig(ctypes.Structure):
    _fields_ = [
        ("noise", Config),
        ("guard_samples", ctypes.c_uint16),
        ("min_current_ua", ctypes.c_uint32),
        ("max_rel_sigma_permille", ctypes.c_uint32),
        ("r_wire_uohm", ctypes.c_uint32),
        ("r_min_uohm", ctypes.c_uint32),
        ("r_bol_uohm", ctypes.c_uint32),
        ("r_eol_uohm", ctypes.c_uint32),
    ]


class IrResult(ctypes.Structure):
    _fields_ = [
        ("quality", ctypes.c_int),
        ("dv_uv", ctypes.c_int32),
        ("dv_sigma_uv", ctypes.c_uint32),
        ("r_uohm", ctypes.c_int32),
        ("r_sigma_uohm", ctypes.c_uint32),
        ("soh_permille", ctypes.c_uint16),
    ]

    @property
    def q(self) -> IrQuality:
        return IrQuality(self.quality)

    def __repr__(self) -> str:
        return (f"IrResult({self.q.name}, dV={self.dv_uv}+-{self.dv_sigma_uv}uV, "
                f"R={self.r_uohm}+-{self.r_sigma_uohm}uOhm, SoH={self.soh_permille / 10:.1f}%)")


_lib.cir_default_config.argtypes = [ctypes.POINTER(IrConfig)]
_lib.cir_estimate.argtypes = [ctypes.POINTER(ctypes.c_uint16), ctypes.c_size_t, ctypes.c_size_t,
                              ctypes.c_uint32, ctypes.POINTER(IrConfig), ctypes.POINTER(IrResult)]
_lib.cir_estimate.restype = ctypes.c_int


def ir_default_config(**overrides) -> IrConfig:
    cfg = IrConfig()
    _lib.cir_default_config(ctypes.byref(cfg))
    for key, value in overrides.items():
        setattr(cfg, key, value)
    return cfg


def ir_estimate(samples, step_index: int, current_ua: int,
                cfg: IrConfig | None = None) -> tuple[IrStatus, IrResult]:
    arr = np.ascontiguousarray(samples, dtype=np.uint16)
    cfg = cfg if cfg is not None else ir_default_config()
    res = IrResult()
    st = _lib.cir_estimate(arr.ctypes.data_as(ctypes.POINTER(ctypes.c_uint16)), arr.size,
                           step_index, current_ua, ctypes.byref(cfg), ctypes.byref(res))
    return IrStatus(st), res


def classify_raw(ptr, n: int, cfg_ptr, res_ptr) -> Status:
    """Unchecked call for NULL-pointer tests."""
    return Status(_lib.cnc_classify(ptr, n, cfg_ptr, res_ptr))
