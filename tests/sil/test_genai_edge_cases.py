"""Data-driven edge cases designed with GenAI assistance (see docs/genai-test-design.md)."""
import json
from pathlib import Path

import numpy as np
import pytest

from cnc import Cls, Flag, classify, default_config
from signals import SignalSpec, generate

CASES = json.loads((Path(__file__).parent / "genai_edge_cases.json").read_text())["cases"]


def _build(case):
    spec = SignalSpec(**case.get("signal", {}))
    if "raw_alternate" in case:
        a, b = case["raw_alternate"]
        return np.array([a if i % 2 == 0 else b for i in range(spec.n)], dtype=np.uint16)
    return generate(spec)


def _param(case):
    marks = [pytest.mark.req("REQ-005", "REQ-006", "REQ-007", "REQ-008", "REQ-009", "REQ-010")]
    if case["status"] == "known_limitation":
        marks.append(pytest.mark.xfail(strict=True, reason=f"{case['ref']}: {case['title']}"))
    return pytest.param(case, id=f"{case['id']}-{case['title'][:40]}", marks=marks)


@pytest.mark.parametrize("case", [_param(c) for c in CASES])
def test_edge_case(case):
    cfg = default_config(**case.get("config", {}))
    _, res = classify(_build(case), cfg)
    assert res.klass == Cls[case["expected"]], res
    if "expected_flags" in case:
        want = Flag(0)
        for name in case["expected_flags"]:
            want |= Flag[name]
        assert Flag(res.flags) == want, res


def test_every_limitation_is_documented():
    doc = (Path(__file__).parents[2] / "docs" / "limitations.md").read_text()
    for case in CASES:
        if "ref" in case:
            assert f"## {case['ref']}" in doc, f"{case['id']} references undocumented {case['ref']}"
