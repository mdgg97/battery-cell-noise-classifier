/* Minimal unit test helper: no dependencies, runs on host or target. */
#ifndef MINITEST_H
#define MINITEST_H

#include <stdio.h>

static int mt_failed = 0;
static int mt_checks = 0;
static int mt_tests = 0;
static int mt_current_failed = 0;

#define MT_CHECK(cond)                                                        \
    do {                                                                      \
        mt_checks++;                                                          \
        if (!(cond)) {                                                        \
            mt_current_failed = 1;                                            \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                     \
    } while (0)

#define MT_CHECK_EQ(a, b)                                                     \
    do {                                                                      \
        long long mt_a = (long long)(a), mt_b = (long long)(b);               \
        mt_checks++;                                                          \
        if (mt_a != mt_b) {                                                   \
            mt_current_failed = 1;                                            \
            printf("    FAIL %s:%d: %s == %s (%lld != %lld)\n",               \
                   __FILE__, __LINE__, #a, #b, mt_a, mt_b);                   \
        }                                                                     \
    } while (0)

/* name: test function; req: requirement ID string for traceability */
#define MT_RUN(fn, req)                                                       \
    do {                                                                      \
        mt_tests++;                                                           \
        mt_current_failed = 0;                                                \
        fn();                                                                 \
        printf("[%s] %-48s %s\n", mt_current_failed ? "FAIL" : " OK ",        \
               #fn, req);                                                     \
        mt_failed += mt_current_failed;                                       \
    } while (0)

#define MT_SUMMARY()                                                          \
    (printf("\n%d tests, %d checks, %d failed\n", mt_tests, mt_checks,       \
            mt_failed),                                                       \
     mt_failed == 0 ? 0 : 1)

#endif
