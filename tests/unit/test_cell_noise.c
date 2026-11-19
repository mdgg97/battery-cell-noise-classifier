/* Host-side unit tests for cell_noise.c — deterministic, hand-built signals. */
#include "cell_noise.h"
#include "minitest.h"

#include <string.h>

#define BASE 37000u /* 3.700 V in 100 uV LSB */

static uint16_t buf[CNC_MAX_SAMPLES + 1];
static cnc_config_t cfg;
static cnc_result_t res;

static void fill_const(size_t n, uint16_t v)
{
    size_t i;
    for (i = 0; i < n; i++) { buf[i] = v; }
}

/* +-1 LSB toggling: realistic "quiet" signal, RMS about 50 uV */
static void fill_quiet(size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) { buf[i] = (uint16_t)(BASE + (i & 1u)); }
}

static void fill_ramp(size_t n, uint16_t start, uint16_t step)
{
    size_t i;
    for (i = 0; i < n; i++) { buf[i] = (uint16_t)(start + i * step); }
}

static cnc_class_t classify(size_t n)
{
    cnc_status_t st = cnc_classify(buf, n, &cfg, &res);
    MT_CHECK_EQ(st, CNC_STATUS_OK);
    return res.cls;
}

/* ---- isqrt -------------------------------------------------------------- */

static void test_isqrt_values(void)
{
    MT_CHECK_EQ(cnc_isqrt64(0u), 0);
    MT_CHECK_EQ(cnc_isqrt64(1u), 1);
    MT_CHECK_EQ(cnc_isqrt64(3u), 1);
    MT_CHECK_EQ(cnc_isqrt64(4u), 2);
    MT_CHECK_EQ(cnc_isqrt64(15u), 3);
    MT_CHECK_EQ(cnc_isqrt64(16u), 4);
    MT_CHECK_EQ(cnc_isqrt64(1000000u), 1000);
    MT_CHECK_EQ(cnc_isqrt64(999999u), 999);
    MT_CHECK_EQ(cnc_isqrt64(UINT64_MAX), 4294967295u);
}

/* ---- API contract ------------------------------------------------------- */

static void test_null_pointers_rejected(void)
{
    fill_quiet(64);
    MT_CHECK_EQ(cnc_classify(NULL, 64, &cfg, &res), CNC_STATUS_ERR_NULL);
    MT_CHECK_EQ(res.cls, CNC_CLASS_INVALID);
    MT_CHECK_EQ(cnc_classify(buf, 64, NULL, &res), CNC_STATUS_ERR_NULL);
    MT_CHECK_EQ(cnc_classify(buf, 64, &cfg, NULL), CNC_STATUS_ERR_NULL);
    MT_CHECK_EQ(cnc_default_config(NULL), CNC_STATUS_ERR_NULL);
    MT_CHECK_EQ(cnc_validate_config(NULL), CNC_STATUS_ERR_NULL);
}

static void test_length_limits(void)
{
    fill_quiet(CNC_MAX_SAMPLES + 1);
    MT_CHECK_EQ(cnc_classify(buf, 0, &cfg, &res), CNC_STATUS_ERR_LENGTH);
    MT_CHECK_EQ(cnc_classify(buf, CNC_MIN_SAMPLES - 1, &cfg, &res), CNC_STATUS_ERR_LENGTH);
    MT_CHECK_EQ(cnc_classify(buf, CNC_MIN_SAMPLES, &cfg, &res), CNC_STATUS_OK);
    MT_CHECK_EQ(cnc_classify(buf, CNC_MAX_SAMPLES, &cfg, &res), CNC_STATUS_OK);
    MT_CHECK_EQ(cnc_classify(buf, CNC_MAX_SAMPLES + 1, &cfg, &res), CNC_STATUS_ERR_LENGTH);
}

static void test_error_result_is_failsafe(void)
{
    /* pre-fill result with garbage that looks like OK */
    memset(&res, 0xA5, sizeof(res));
    res.cls = CNC_CLASS_OK;
    fill_quiet(64);
    (void)cnc_classify(buf, 3, &cfg, &res);
    MT_CHECK_EQ(res.cls, CNC_CLASS_INVALID);
    MT_CHECK_EQ(res.flags, 0);
    MT_CHECK_EQ(res.rms_uv, 0);
    MT_CHECK_EQ(res.mean_uv, 0);
    MT_CHECK_EQ(res.outlier_count, 0);
}

static void test_config_validation(void)
{
    cnc_config_t c;

    cnc_default_config(&c);
    MT_CHECK_EQ(cnc_validate_config(&c), CNC_STATUS_OK);

    cnc_default_config(&c); c.v_min_uv = c.v_max_uv;
    MT_CHECK_EQ(cnc_validate_config(&c), CNC_STATUS_ERR_CONFIG);
    cnc_default_config(&c); c.v_max_uv = 6553501u;
    MT_CHECK_EQ(cnc_validate_config(&c), CNC_STATUS_ERR_CONFIG);
    cnc_default_config(&c); c.rms_limit_uv = 0;
    MT_CHECK_EQ(cnc_validate_config(&c), CNC_STATUS_ERR_CONFIG);
    cnc_default_config(&c); c.drift_limit_uv = 0;
    MT_CHECK_EQ(cnc_validate_config(&c), CNC_STATUS_ERR_CONFIG);
    cnc_default_config(&c); c.spike_limit_uv = c.rms_limit_uv;
    MT_CHECK_EQ(cnc_validate_config(&c), CNC_STATUS_ERR_CONFIG);
    cnc_default_config(&c); c.spike_max_count = 0;
    MT_CHECK_EQ(cnc_validate_config(&c), CNC_STATUS_ERR_CONFIG);
    cnc_default_config(&c); c.spike_max_count = CNC_MIN_SAMPLES / 2 + 1;
    MT_CHECK_EQ(cnc_validate_config(&c), CNC_STATUS_ERR_CONFIG);
    cnc_default_config(&c); c.stuck_max_p2p_uv = c.spike_limit_uv;
    MT_CHECK_EQ(cnc_validate_config(&c), CNC_STATUS_ERR_CONFIG);

    /* classify must refuse a bad config, fail-safe */
    cnc_default_config(&c); c.rms_limit_uv = 0;
    fill_quiet(64);
    MT_CHECK_EQ(cnc_classify(buf, 64, &c, &res), CNC_STATUS_ERR_CONFIG);
    MT_CHECK_EQ(res.cls, CNC_CLASS_INVALID);
}

/* ---- classes ------------------------------------------------------------ */

static void test_quiet_signal_is_ok(void)
{
    fill_quiet(64);
    MT_CHECK_EQ(classify(64), CNC_CLASS_OK);
    MT_CHECK_EQ(res.flags, 0);
    MT_CHECK(res.rms_uv >= 45 && res.rms_uv <= 55);
    MT_CHECK_EQ(res.p2p_uv, 100);
    MT_CHECK_EQ(res.mean_uv, 3700050);
}

static void test_constant_signal_is_stuck(void)
{
    fill_const(64, BASE);
    MT_CHECK_EQ(classify(64), CNC_CLASS_STUCK);
    MT_CHECK_EQ(res.rms_uv, 0);
    MT_CHECK_EQ(res.drift_uv, 0);
    MT_CHECK_EQ(res.mean_uv, 3700000);
}

static void test_stuck_threshold_configurable(void)
{
    fill_quiet(64); /* p2p = 100 uV */
    cfg.stuck_max_p2p_uv = 100;
    MT_CHECK_EQ(classify(64), CNC_CLASS_STUCK);
    cfg.stuck_max_p2p_uv = 99;
    MT_CHECK_EQ(classify(64), CNC_CLASS_OK);
}

static void test_range_boundaries(void)
{
    fill_quiet(64);
    buf[10] = 43000; /* exactly v_max: allowed */
    buf[11] = 43000;
    buf[12] = 43000;
    (void)classify(64);
    MT_CHECK((res.flags & CNC_FLAG_OUT_OF_RANGE) == 0);

    fill_quiet(64);
    buf[10] = 43001; /* 1 LSB above v_max */
    MT_CHECK_EQ(classify(64), CNC_CLASS_OUT_OF_RANGE);

    fill_quiet(64);
    buf[20] = 25000; /* exactly v_min */
    (void)classify(64);
    MT_CHECK((res.flags & CNC_FLAG_OUT_OF_RANGE) == 0);

    fill_quiet(64);
    buf[20] = 24999;
    MT_CHECK_EQ(classify(64), CNC_CLASS_OUT_OF_RANGE);
}

static void test_open_wire_zero_reading(void)
{
    fill_const(64, 0); /* open wire reads 0 V: range + stuck, range wins */
    MT_CHECK_EQ(classify(64), CNC_CLASS_OUT_OF_RANGE);
    MT_CHECK(res.flags & CNC_FLAG_STUCK);
}

static void test_single_spike(void)
{
    fill_quiet(64);
    buf[30] = (uint16_t)(BASE + 100); /* +10 mV */
    MT_CHECK_EQ(classify(64), CNC_CLASS_SPIKE);
    MT_CHECK_EQ(res.outlier_count, 1);
    MT_CHECK(res.rms_clean_uv <= cfg.rms_limit_uv);
    MT_CHECK(res.rms_uv > cfg.rms_limit_uv); /* the spike alone inflates plain RMS */
}

/* DEF-001 regression: one spike in the shortest window must stay a SPIKE */
static void test_spike_in_min_window_def001(void)
{
    fill_quiet(CNC_MIN_SAMPLES);
    buf[4] = (uint16_t)(BASE + 100);
    MT_CHECK_EQ(classify(CNC_MIN_SAMPLES), CNC_CLASS_SPIKE);
    MT_CHECK_EQ(res.outlier_count, 1);
    MT_CHECK(res.rms_clean_uv < 100);
}

static void test_negative_spike(void)
{
    fill_quiet(64);
    buf[5] = (uint16_t)(BASE - 100);
    MT_CHECK_EQ(classify(64), CNC_CLASS_SPIKE);
}

/* Two symmetric +3.2 mV spikes on a flat line: slope stays 0, mean rises by
 * 3.2/32 mV, so each spike residual is exactly 3100 uV. "Above the limit"
 * must be strict. (Added after a mutation probe: >= survived the unit tests.) */
static void test_spike_limit_is_exclusive(void)
{
    fill_const(64, BASE);
    buf[10] = (uint16_t)(BASE + 32);
    buf[53] = (uint16_t)(BASE + 32);

    cfg.spike_limit_uv = 3100;
    (void)classify(64);
    MT_CHECK_EQ(res.outlier_count, 0);

    cfg.spike_limit_uv = 3099;
    MT_CHECK_EQ(classify(64), CNC_CLASS_SPIKE);
    MT_CHECK_EQ(res.outlier_count, 2);
    MT_CHECK_EQ(res.rms_clean_uv, 0);
}

static void test_spike_count_limit(void)
{
    fill_quiet(64);
    buf[10] = (uint16_t)(BASE + 100);
    buf[40] = (uint16_t)(BASE + 100);
    MT_CHECK_EQ(classify(64), CNC_CLASS_SPIKE);
    MT_CHECK_EQ(res.outlier_count, 2);

    buf[50] = (uint16_t)(BASE + 100); /* third outlier: burst, not isolated */
    MT_CHECK_EQ(classify(64), CNC_CLASS_ELEVATED_NOISE);
    MT_CHECK_EQ(res.outlier_count, 3);
}

static void test_elevated_noise(void)
{
    size_t i;
    /* +-1 mV square wave: RMS 1000 uV > 500 uV, no single outlier */
    for (i = 0; i < 64; i++) { buf[i] = (uint16_t)((i & 1u) ? BASE + 10 : BASE - 10); }
    MT_CHECK_EQ(classify(64), CNC_CLASS_ELEVATED_NOISE);
    MT_CHECK_EQ(res.outlier_count, 0);
    MT_CHECK(res.rms_uv >= 990 && res.rms_uv <= 1010);
}

static void test_pure_ramp_is_drift_without_noise(void)
{
    fill_ramp(64, BASE, 1); /* 100 uV per sample, 6.3 mV over window */
    MT_CHECK_EQ(classify(64), CNC_CLASS_DRIFT);
    MT_CHECK_EQ(res.drift_uv, 6300);
    MT_CHECK_EQ(res.rms_uv, 0);
}

static void test_falling_ramp_negative_drift(void)
{
    size_t i;
    for (i = 0; i < 64; i++) { buf[i] = (uint16_t)(BASE - i); }
    MT_CHECK_EQ(classify(64), CNC_CLASS_DRIFT);
    MT_CHECK_EQ(res.drift_uv, -6300);
}

static void test_small_ramp_below_limit(void)
{
    fill_ramp(16, BASE, 1); /* 1.5 mV over window, limit 3 mV */
    MT_CHECK_EQ(classify(16), CNC_CLASS_OK);
    MT_CHECK_EQ(res.drift_uv, 1500);
}

static void test_spike_on_ramp_precedence(void)
{
    fill_ramp(64, BASE, 1);
    buf[32] = (uint16_t)(buf[32] + 100);
    MT_CHECK_EQ(classify(64), CNC_CLASS_SPIKE);
    MT_CHECK(res.flags & CNC_FLAG_DRIFT);
    MT_CHECK(res.flags & CNC_FLAG_SPIKE);
}

/* ---- overflow / extremes (REQ-031) -------------------------------------- */

static void test_full_scale_alternating_no_overflow(void)
{
    size_t i;
    cfg.v_min_uv = 0;
    cfg.v_max_uv = 6553500u;
    for (i = 0; i < CNC_MAX_SAMPLES; i++) { buf[i] = (i & 1u) ? 65535u : 0u; }
    (void)classify(CNC_MAX_SAMPLES);
    MT_CHECK_EQ(res.mean_uv, 3276750);
    MT_CHECK_EQ(res.p2p_uv, 6553500);
    /* residuals are +-3276750 uV minus a tiny slope term */
    MT_CHECK(res.rms_uv > 3270000u && res.rms_uv <= 3276750u);
}

static void test_full_scale_ramp_no_overflow(void)
{
    cfg.v_min_uv = 0;
    cfg.v_max_uv = 6553500u;
    fill_ramp(CNC_MAX_SAMPLES, 0, 257); /* 0 .. 65535 */
    MT_CHECK_EQ(classify(CNC_MAX_SAMPLES), CNC_CLASS_DRIFT);
    MT_CHECK_EQ(res.drift_uv, 6553500);
    MT_CHECK_EQ(res.rms_uv, 0);
}

static void test_class_names(void)
{
    MT_CHECK(strcmp(cnc_class_name(CNC_CLASS_INVALID), "INVALID") == 0);
    MT_CHECK(strcmp(cnc_class_name(CNC_CLASS_OUT_OF_RANGE), "OUT_OF_RANGE") == 0);
    MT_CHECK(strcmp(cnc_class_name(CNC_CLASS_STUCK), "STUCK") == 0);
    MT_CHECK(strcmp(cnc_class_name(CNC_CLASS_SPIKE), "SPIKE") == 0);
    MT_CHECK(strcmp(cnc_class_name(CNC_CLASS_ELEVATED_NOISE), "ELEVATED_NOISE") == 0);
    MT_CHECK(strcmp(cnc_class_name(CNC_CLASS_DRIFT), "DRIFT") == 0);
    MT_CHECK(strcmp(cnc_class_name(CNC_CLASS_OK), "OK") == 0);
    MT_CHECK(strcmp(cnc_class_name((cnc_class_t)99), "UNKNOWN") == 0);
}

/* Each test starts from the default configuration. */
#define RUN(fn, req) do { cnc_default_config(&cfg); MT_RUN(fn, req); } while (0)

int run_noise_tests(void)
{
    printf("cell_noise unit tests\n\n");
    RUN(test_isqrt_values,                      "REQ-031");
    RUN(test_null_pointers_rejected,            "REQ-002");
    RUN(test_length_limits,                     "REQ-001");
    RUN(test_error_result_is_failsafe,          "REQ-003");
    RUN(test_config_validation,                 "REQ-004");
    RUN(test_quiet_signal_is_ok,                "REQ-011");
    RUN(test_constant_signal_is_stuck,          "REQ-006");
    RUN(test_stuck_threshold_configurable,      "REQ-006");
    RUN(test_range_boundaries,                  "REQ-005");
    RUN(test_open_wire_zero_reading,            "REQ-005 REQ-010");
    RUN(test_single_spike,                      "REQ-007");
    RUN(test_spike_in_min_window_def001,        "REQ-007");
    RUN(test_negative_spike,                    "REQ-007");
    RUN(test_spike_limit_is_exclusive,          "REQ-007");
    RUN(test_spike_count_limit,                 "REQ-007 REQ-008");
    RUN(test_elevated_noise,                    "REQ-008");
    RUN(test_pure_ramp_is_drift_without_noise,  "REQ-009");
    RUN(test_falling_ramp_negative_drift,       "REQ-009");
    RUN(test_small_ramp_below_limit,            "REQ-009 REQ-011");
    RUN(test_spike_on_ramp_precedence,          "REQ-010");
    RUN(test_full_scale_alternating_no_overflow,"REQ-031");
    RUN(test_full_scale_ramp_no_overflow,       "REQ-031");
    RUN(test_class_names,                       "REQ-010");
    return MT_SUMMARY();
}
