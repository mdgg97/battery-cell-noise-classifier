/* Unit tests for cell_ir.c — internal resistance from a balancing step. */
#include "cell_ir.h"
#include "minitest.h"

#include <string.h>

#define BASE      37000u   /* 3.700 V */
#define STEP      32u      /* balancing switched on at this sample */
#define N         65u      /* 32 samples before, 1 guard, 32 after */
#define I_50MA    50000u   /* 50 mA balancing current */

static uint16_t buf[CNC_MAX_SAMPLES + 1];
static cir_config_t cfg;
static cir_result_t res;

/* Quiet +-1 LSB toggle; from STEP on the voltage is drop_lsb lower. */
static void fill_step(size_t n, size_t step, uint16_t drop_lsb)
{
    for (size_t i = 0; i < n; i++) {
        buf[i] = (uint16_t)(BASE + (i & 1u) - (i >= step ? drop_lsb : 0u));
    }
}

static cir_quality_t estimate(uint32_t current_ua)
{
    MT_CHECK_EQ(cir_estimate(buf, N, STEP, current_ua, &cfg, &res), CIR_STATUS_OK);
    return res.quality;
}

/* ---- API contract ------------------------------------------------------- */

static void test_ir_null_and_failsafe(void)
{
    fill_step(N, STEP, 20);
    memset(&res, 0xA5, sizeof(res));
    MT_CHECK_EQ(cir_estimate(NULL, N, STEP, I_50MA, &cfg, &res), CIR_STATUS_ERR_NULL);
    MT_CHECK_EQ(res.quality, CIR_QUALITY_INVALID);
    MT_CHECK_EQ(res.r_uohm, 0);
    MT_CHECK_EQ(res.soh_permille, 0);
    MT_CHECK_EQ(cir_estimate(buf, N, STEP, I_50MA, NULL, &res), CIR_STATUS_ERR_NULL);
    MT_CHECK_EQ(cir_estimate(buf, N, STEP, I_50MA, &cfg, NULL), CIR_STATUS_ERR_NULL);
    MT_CHECK_EQ(cir_default_config(NULL), CIR_STATUS_ERR_NULL);
}

static void test_ir_window_limits(void)
{
    fill_step(CNC_MAX_SAMPLES + 1, STEP, 20);
    MT_CHECK_EQ(cir_estimate(buf, N, CIR_MIN_WINDOW - 1, I_50MA, &cfg, &res), CIR_STATUS_ERR_WINDOW);
    MT_CHECK_EQ(cir_estimate(buf, N, CIR_MIN_WINDOW, I_50MA, &cfg, &res), CIR_STATUS_OK);
    /* post window: N - (step + guard) must be >= 8 */
    MT_CHECK_EQ(cir_estimate(buf, N, N - CIR_MIN_WINDOW - 1, I_50MA, &cfg, &res), CIR_STATUS_OK);
    MT_CHECK_EQ(cir_estimate(buf, N, N - CIR_MIN_WINDOW, I_50MA, &cfg, &res), CIR_STATUS_ERR_WINDOW);
    MT_CHECK_EQ(cir_estimate(buf, N, N, I_50MA, &cfg, &res), CIR_STATUS_ERR_WINDOW);
    MT_CHECK_EQ(cir_estimate(buf, CNC_MAX_SAMPLES + 1, STEP, I_50MA, &cfg, &res), CIR_STATUS_ERR_WINDOW);
    MT_CHECK_EQ(res.quality, CIR_QUALITY_INVALID);
}

static void test_ir_config_validation(void)
{
    cir_config_t c;
    cir_default_config(&c);
    MT_CHECK_EQ(cir_validate_config(&c), CIR_STATUS_OK);
    cir_default_config(&c); c.r_eol_uohm = c.r_bol_uohm;
    MT_CHECK_EQ(cir_validate_config(&c), CIR_STATUS_ERR_CONFIG);
    MT_CHECK_EQ(cir_validate_config(NULL), CIR_STATUS_ERR_NULL);
    cir_default_config(&c); c.guard_samples = CNC_MAX_SAMPLES + 1;
    MT_CHECK_EQ(cir_validate_config(&c), CIR_STATUS_ERR_CONFIG);
    cir_default_config(&c); c.max_rel_sigma_permille = 0;
    MT_CHECK_EQ(cir_validate_config(&c), CIR_STATUS_ERR_CONFIG);
    cir_default_config(&c); c.r_min_uohm = c.r_bol_uohm;
    MT_CHECK_EQ(cir_validate_config(&c), CIR_STATUS_ERR_CONFIG);
    cir_default_config(&c); c.min_current_ua = 0;
    MT_CHECK_EQ(cir_validate_config(&c), CIR_STATUS_ERR_CONFIG);
    cir_default_config(&c); c.max_rel_sigma_permille = 1001;
    MT_CHECK_EQ(cir_validate_config(&c), CIR_STATUS_ERR_CONFIG);
    cir_default_config(&c); c.noise.rms_limit_uv = 0; /* embedded noise config is checked too */
    MT_CHECK_EQ(cir_validate_config(&c), CIR_STATUS_ERR_CONFIG);
    fill_step(N, STEP, 20);
    MT_CHECK_EQ(cir_estimate(buf, N, STEP, I_50MA, &c, &res), CIR_STATUS_ERR_CONFIG);
    MT_CHECK_EQ(res.quality, CIR_QUALITY_INVALID);
}

/* ---- estimation --------------------------------------------------------- */

/* 2 mV drop at 50 mA -> exactly 40 mOhm, the begin-of-life value */
static void test_ir_exact_resistance(void)
{
    fill_step(N, STEP, 20);
    MT_CHECK_EQ(estimate(I_50MA), CIR_QUALITY_OK);
    MT_CHECK_EQ(res.dv_uv, -2000);
    MT_CHECK_EQ(res.r_uohm, 40000);
    MT_CHECK_EQ(res.soh_permille, 1000);
    /* +-1 LSB toggle, 32 samples per window: sample variance 0.258 LSB^2,
     * variance of each mean 80.6 uV^2, sum 161 uV^2 -> sigma(dV) = 12 uV -> 240 uOhm */
    MT_CHECK_EQ(res.dv_sigma_uv, 12);
    MT_CHECK_EQ(res.r_sigma_uohm, 240);
}

static void test_ir_wire_compensation(void)
{
    fill_step(N, STEP, 30);             /* 3 mV at 50 mA = 60 mOhm measured */
    cfg.r_wire_uohm = 15000;            /* 15 mOhm of it is the sense wire */
    MT_CHECK_EQ(estimate(I_50MA), CIR_QUALITY_OK);
    MT_CHECK_EQ(res.r_uohm, 45000);
}

static void test_ir_soh_mapping(void)
{
    MT_CHECK_EQ(cir_soh_permille(30000, 40000, 80000), 1000);  /* better than new: clamp */
    MT_CHECK_EQ(cir_soh_permille(40000, 40000, 80000), 1000);
    MT_CHECK_EQ(cir_soh_permille(60000, 40000, 80000), 500);
    MT_CHECK_EQ(cir_soh_permille(79999, 40000, 80000), 0);     /* 0.025 % rounds to 0 */
    MT_CHECK_EQ(cir_soh_permille(80000, 40000, 80000), 0);
    MT_CHECK_EQ(cir_soh_permille(90000, 40000, 80000), 0);
    MT_CHECK_EQ(cir_soh_permille(50000, 80000, 40000), 0);     /* inverted config */

    fill_step(N, STEP, 30);             /* 60 mOhm -> 50 % */
    MT_CHECK_EQ(estimate(I_50MA), CIR_QUALITY_OK);
    MT_CHECK_EQ(res.soh_permille, 500);
}

/* ---- quality gates ------------------------------------------------------ */

static void test_ir_low_current(void)
{
    fill_step(N, STEP, 20);
    MT_CHECK_EQ(estimate(cfg.min_current_ua - 1), CIR_QUALITY_LOW_CURRENT);
    MT_CHECK_EQ(res.soh_permille, 0);
}

static void test_ir_no_response_switch_fault(void)
{
    fill_step(N, STEP, 0);              /* balancing commanded, voltage did not move */
    MT_CHECK_EQ(estimate(I_50MA), CIR_QUALITY_NO_RESPONSE);
    fill_step(N, STEP, 0);
    for (size_t i = STEP; i < N; i++) { buf[i] = (uint16_t)(buf[i] + 20u); } /* rises: wrong direction */
    MT_CHECK_EQ(estimate(I_50MA), CIR_QUALITY_NO_RESPONSE);
}

static void test_ir_noisy_windows_rejected(void)
{
    fill_step(N, STEP, 20);
    buf[10] = (uint16_t)(buf[10] + 100);  /* EMI spike before the step */
    MT_CHECK_EQ(estimate(I_50MA), CIR_QUALITY_NOISY_WINDOW);

    fill_step(N, STEP, 20);
    buf[50] = (uint16_t)(buf[50] + 100);  /* ... and after the step */
    MT_CHECK_EQ(estimate(I_50MA), CIR_QUALITY_NOISY_WINDOW);
}

static void test_ir_guard_sample_excluded(void)
{
    fill_step(N, STEP, 20);
    buf[STEP] = (uint16_t)(buf[STEP] - 150); /* switching transient on the first sample */
    MT_CHECK_EQ(estimate(I_50MA), CIR_QUALITY_OK);
    MT_CHECK_EQ(res.r_uohm, 40000);
}

/* Plausible R but poor signal-to-noise: +-3 LSB noise, 10 mA, 40 mOhm -> sigma 19 % > 5 % */
static void test_ir_uncertain_low_snr(void)
{
    for (size_t i = 0; i < N; i++) {
        buf[i] = (uint16_t)(((i & 1u) ? BASE + 3u : BASE - 3u) - (i >= STEP ? 4u : 0u));
    }
    MT_CHECK_EQ(estimate(10000), CIR_QUALITY_UNCERTAIN);
    MT_CHECK_EQ(res.r_uohm, 40000);
    MT_CHECK(res.r_sigma_uohm > 2000);
}

/* A drop that is statistically clear but too small for a real cell is a fault, not a cell */
static void test_ir_implausibly_small_r(void)
{
    fill_step(N, STEP, 1);              /* 100 uV at 50 mA = 2 mOhm, below R_min 20 mOhm */
    MT_CHECK_EQ(estimate(I_50MA), CIR_QUALITY_NO_RESPONSE);
    MT_CHECK_EQ(res.r_uohm, 2000);
}

/* 20 mV at 1 uA would be 20 kOhm = 2e10 uOhm: must saturate, not wrap (REQ-031) */
static void test_ir_saturation_no_wrap(void)
{
    fill_step(N, STEP, 200);
    MT_CHECK_EQ(estimate(1), CIR_QUALITY_LOW_CURRENT);
    MT_CHECK_EQ(res.r_uohm, INT32_MAX);
    fill_step(N, STEP, 0);
    for (size_t i = STEP; i < N; i++) { buf[i] = (uint16_t)(buf[i] + 200u); }
    MT_CHECK_EQ(estimate(1), CIR_QUALITY_LOW_CURRENT);
    MT_CHECK_EQ(res.r_uohm, INT32_MIN);
}

static void test_ir_quality_names(void)
{
    MT_CHECK(strcmp(cir_quality_name(CIR_QUALITY_INVALID), "INVALID") == 0);
    MT_CHECK(strcmp(cir_quality_name(CIR_QUALITY_OK), "OK") == 0);
    MT_CHECK(strcmp(cir_quality_name(CIR_QUALITY_LOW_CURRENT), "LOW_CURRENT") == 0);
    MT_CHECK(strcmp(cir_quality_name(CIR_QUALITY_NOISY_WINDOW), "NOISY_WINDOW") == 0);
    MT_CHECK(strcmp(cir_quality_name(CIR_QUALITY_NO_RESPONSE), "NO_RESPONSE") == 0);
    MT_CHECK(strcmp(cir_quality_name(CIR_QUALITY_UNCERTAIN), "UNCERTAIN") == 0);
    MT_CHECK(strcmp(cir_quality_name((cir_quality_t)99), "UNKNOWN") == 0);
}

#define RUN(fn, req) do { cir_default_config(&cfg); MT_RUN(fn, req); } while (0)

int run_ir_tests(void)
{
    printf("\ncell_ir unit tests\n\n");
    RUN(test_ir_null_and_failsafe,         "REQ-040");
    RUN(test_ir_window_limits,             "REQ-040");
    RUN(test_ir_config_validation,         "REQ-040");
    RUN(test_ir_exact_resistance,          "REQ-041");
    RUN(test_ir_wire_compensation,         "REQ-042");
    RUN(test_ir_soh_mapping,               "REQ-043");
    RUN(test_ir_low_current,               "REQ-044");
    RUN(test_ir_no_response_switch_fault,  "REQ-045");
    RUN(test_ir_noisy_windows_rejected,    "REQ-044");
    RUN(test_ir_guard_sample_excluded,     "REQ-041");
    RUN(test_ir_uncertain_low_snr,         "REQ-044");
    RUN(test_ir_implausibly_small_r,       "REQ-045");
    RUN(test_ir_saturation_no_wrap,        "REQ-031");
    RUN(test_ir_quality_names,             "REQ-045");
    return MT_SUMMARY();
}
