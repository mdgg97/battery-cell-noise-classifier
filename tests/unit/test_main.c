/* Test runner: one binary for the host and for the Cortex-M4 target. */
#include <stdio.h>

int run_noise_tests(void);
int run_ir_tests(void);

int main(void)
{
    const int failed_suites = run_noise_tests() + run_ir_tests();
    printf("\n== %s: %d suite(s) failed\n", failed_suites ? "FAILED" : "ALL PASSED", failed_suites);
    return failed_suites;
}
