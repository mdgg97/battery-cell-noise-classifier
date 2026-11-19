"""Requirements traceability: every REQ-xxx in docs/requirements.md needs at least one test.

Scans the C unit tests and the pytest SIL tests for requirement IDs and prints a matrix.
Exit code 1 if a requirement is uncovered.
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REQ_ID = re.compile(r"REQ-\d{3}")
# Covered by build/static checks rather than test functions
CHECKED_BY_TOOLING = {
    "REQ-030": "make lint",
    "REQ-032": "Makefile -Werror",
    "REQ-033": "make memcheck",
    "REQ-034": "make divcount",
    "REQ-035": "make arm qemu (CI)",
}


def requirement_ids() -> list[str]:
    text = (ROOT / "docs" / "requirements.md").read_text()
    return sorted(set(re.findall(r"^\| (REQ-\d{3}) \|", text, re.MULTILINE)))


def coverage() -> dict[str, set[str]]:
    found: dict[str, set[str]] = defaultdict(set)
    sources = list((ROOT / "tests").rglob("*.c")) + list((ROOT / "tests").rglob("test_*.py"))
    for path in sources:
        for req in REQ_ID.findall(path.read_text()):
            found[req].add(path.name)
    return found


def main() -> int:
    found = coverage()
    missing = []
    print(f"{'Requirement':<12} Covered by")
    for req in requirement_ids():
        where = sorted(found.get(req, set()))
        if req in CHECKED_BY_TOOLING:
            where.append(CHECKED_BY_TOOLING[req])
        print(f"{req:<12} {', '.join(where) if where else '-- MISSING --'}")
        if not where:
            missing.append(req)
    if missing:
        print(f"\nUncovered requirements: {', '.join(missing)}")
        return 1
    print("\nAll requirements traced.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
