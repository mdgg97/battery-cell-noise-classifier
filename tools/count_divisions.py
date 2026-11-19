"""REQ-034: the number of 64-bit divisions per window must be constant.

Cortex-M cores have no 64-bit divide instruction; every 64-bit division is a
slow software routine (__aeabi_ldivmod). This check instruments the two
helpers that divide (div_round, rms_from_sum), classifies windows of
different lengths on the host and fails if the count depends on n.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIVIDING_HELPERS = ("static int64_t div_round(int64_t num, int64_t den)",
                    "static uint32_t rms_from_sum(uint64_t sum_sq, int64_t count)")
MAIN = r"""
#include <stdio.h>
#include "cell_noise.h"
extern long cnt_div;
int main(void) {
    static uint16_t s[CNC_MAX_SAMPLES];
    cnc_config_t c; cnc_result_t r; cnc_default_config(&c);
    const size_t ns[] = {CNC_MIN_SAMPLES, 64, CNC_MAX_SAMPLES};
    for (int k = 0; k < 3; k++) {
        size_t n = ns[k];
        for (size_t i = 0; i < n; i++) s[i] = (uint16_t)(37000 + (i & 1u));
        s[n / 2] = (uint16_t)(s[n / 2] + 100);  /* one spike: exercises the refit too */
        cnt_div = 0;
        cnc_classify(s, n, &c, &r);
        printf("%zu %ld\n", n, cnt_div);
    }
    return 0;
}
"""


def instrument(src: str) -> str:
    src = src.replace('#include "cell_noise.h"', '#include "cell_noise.h"\nlong cnt_div;', 1)
    for sig in DIVIDING_HELPERS:
        if sig not in src:
            sys.exit(f"helper not found, update tools/count_divisions.py: {sig}")
        src = src.replace(sig + "\n{", sig + "\n{\n    cnt_div++;", 1)
    return src


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "lib.c").write_text(instrument((ROOT / "src" / "cell_noise.c").read_text()))
        (tmp / "main.c").write_text(MAIN)
        exe = tmp / "count"
        subprocess.run(["cc", "-std=c99", "-O0", f"-I{ROOT / 'include'}", str(tmp / "lib.c"),
                        str(tmp / "main.c"), "-o", str(exe)], check=True)
        out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout

    counts = {int(n): int(c) for n, c in (line.split() for line in out.split("\n") if line)}
    for n, c in counts.items():
        print(f"n = {n:3d}: {c} 64-bit divisions per window")
    if len(set(counts.values())) != 1:
        print("REQ-034 violated: divisions grow with the window length")
        return 1
    print("REQ-034 ok: constant number of divisions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
