/**
 * @file cell_noise.c
 * @brief Battery cell measurement noise classifier.
 *
 * Processing steps for one window:
 *   1. validate input                         validate_request()
 *   2. range check and peak-to-peak           scan_extremes()
 *   3. least-squares line over all samples    fit_line()
 *      -> mean, drift, RMS, outliers          measure_residuals()
 *   4. robust refit without outliers          clean_rms()
 *   5. findings -> class                      evaluate_flags(), class_from_flags()
 *
 * Integer-only, no heap. Per-sample work uses only multiplications and
 * shifts: the fitted line is converted once per fit into Q18 fixed point, so
 * a Cortex-M does not call the slow 64-bit software division (__aeabi_ldivmod)
 * for every sample.
 *
 * Overflow budget (n <= 256, y <= 65535 LSB, Q18 = 2^18 = 262144):
 *   |bnum| = |m*Sxy - Sx*Sy|      <= 1.4e11
 *   bnum * 100 * 2^18             <= 3.7e18   (< INT64_MAX = 9.2e18)
 *   y * 100 * 2^18                <= 1.8e12
 *   slope_q18 * |m*i - Sx|        <= 1.8e12
 * The same bounds hold for any subset of the samples.
 * Fixed-point error of a residual: <= 0.07 uV before rounding to 1 uV.
 */
#include "cell_noise.h"

/* No libc dependency: the library builds freestanding (-ffreestanding, no <string.h>). */

/* ---- defaults: Li-ion NMC cell ------------------------------------------ */

#define DEFAULT_V_MIN_UV         2500000u /* 2.5 V */
#define DEFAULT_V_MAX_UV         4300000u /* 4.3 V */
#define DEFAULT_STUCK_MAX_P2P_UV 0u       /* all samples identical */
#define DEFAULT_RMS_LIMIT_UV     500u     /* 0.5 mV RMS */
#define DEFAULT_SPIKE_LIMIT_UV   5000u    /* 5 mV */
#define DEFAULT_SPIKE_MAX_COUNT  2u
#define DEFAULT_DRIFT_LIMIT_UV   3000u    /* 3 mV per window */

/* Largest spike_max_count: keeps outliers "isolated" and leaves >= 4 points for the refit. */
#define MAX_SPIKE_COUNT (CNC_MIN_SAMPLES / 2u)

/* ---- small helpers ------------------------------------------------------ */

#define Q18_SHIFT 18
#define Q18_ONE   ((int64_t)1 << Q18_SHIFT)

/* Round-half-away-from-zero division, den > 0. */
static int64_t div_round(int64_t num, int64_t den)
{
    if (num >= 0) {
        return (num + den / 2) / den;
    }
    return -((-num + den / 2) / den);
}

/* Q18 -> integer, rounded half away from zero; shifts only, no division. */
static int64_t q18_round(int64_t v)
{
    if (v >= 0) {
        return (v + Q18_ONE / 2) >> Q18_SHIFT;
    }
    return -((-v + Q18_ONE / 2) >> Q18_SHIFT);
}

static uint32_t abs_u32(int64_t v)
{
    return (uint32_t)(v < 0 ? -v : v);
}

static uint32_t rms_from_sum(uint64_t sum_sq, int64_t count)
{
    return cnc_isqrt64(sum_sq / (uint64_t)count);
}

uint32_t cnc_isqrt64(uint64_t x)
{
    uint64_t res = 0u;
    uint64_t bit = (uint64_t)1 << 62;

    while (bit > x) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (x >= res + bit) {
            x -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)res;
}

/* ---- outlier set (bitmask, one bit per sample) -------------------------- */

typedef struct {
    uint32_t bits[CNC_MAX_SAMPLES / 32u];
} sample_set_t;

static void set_add(sample_set_t *s, size_t i)
{
    s->bits[i / 32u] |= (uint32_t)1u << (i % 32u);
}

static int set_contains(const sample_set_t *s, size_t i)
{
    return (s != NULL) && ((s->bits[i / 32u] >> (i % 32u)) & 1u) != 0u;
}

/* ---- least-squares line ------------------------------------------------- */

typedef struct {
    int64_t m;      /* number of points */
    int64_t sx;     /* sum of x (sample index) */
    int64_t sy;     /* sum of y */
    int64_t d;      /* m*Sxx - Sx^2 (> 0 for m >= 2) */
    int64_t bnum;   /* m*Sxy - Sx*Sy; slope = bnum / d */
    int64_t mean_q18;  /* mean in uV, Q18 */
    int64_t slope_q18; /* uV per unit of (m*i - Sx), Q18: fitted(i) = mean + slope*(m*i - Sx) */
} line_fit_t;

/* Fit a line through (i, samples[i]) for every i not in @p excluded (may be NULL). */
static line_fit_t fit_line(const uint16_t *samples, size_t n, const sample_set_t *excluded)
{
    line_fit_t f = {0, 0, 0, 0, 0, 0, 0};
    int64_t sxx = 0;
    int64_t sxy = 0;

    for (size_t i = 0u; i < n; i++) {
        if (set_contains(excluded, i)) {
            continue;
        }
        const int64_t x = (int64_t)i;
        const int64_t y = (int64_t)samples[i];
        f.m++;
        f.sx += x;
        f.sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    f.d = f.m * sxx - f.sx * f.sx;
    f.bnum = f.m * sxy - f.sx * f.sy;
    /* the only divisions of the fit: once per window, not once per sample */
    f.mean_q18  = div_round(f.sy * (int64_t)CNC_LSB_UV * Q18_ONE, f.m);
    f.slope_q18 = div_round(f.bnum * (int64_t)CNC_LSB_UV * Q18_ONE, f.m * f.d);
    return f;
}

/* Residual of sample i against the line, in microvolts (rounded). No division. */
static int64_t residual_uv(const line_fit_t *f, size_t i, uint16_t y)
{
    const int64_t y_q18 = (int64_t)y * (int64_t)CNC_LSB_UV * Q18_ONE;
    const int64_t fitted_q18 = f->mean_q18 + f->slope_q18 * (f->m * (int64_t)i - f->sx);
    return q18_round(y_q18 - fitted_q18);
}

static uint32_t fit_mean_uv(const line_fit_t *f)
{
    return (uint32_t)div_round(f->sy * (int64_t)CNC_LSB_UV, f->m);
}

/* Fitted change from the first to the last sample. */
static int32_t fit_drift_uv(const line_fit_t *f)
{
    return (int32_t)div_round(f->bnum * (f->m - 1) * (int64_t)CNC_LSB_UV, f->d);
}

/* ---- processing steps --------------------------------------------------- */

static cnc_status_t validate_request(const uint16_t *samples, size_t n,
                                     const cnc_config_t *cfg, const cnc_result_t *out)
{
    if (samples == NULL || cfg == NULL || out == NULL) {
        return CNC_STATUS_ERR_NULL;
    }
    if (n < CNC_MIN_SAMPLES || n > CNC_MAX_SAMPLES) {
        return CNC_STATUS_ERR_LENGTH;
    }
    return cnc_validate_config(cfg);
}

/* Peak-to-peak; returns non-zero if any sample is outside [v_min, v_max]. */
static int scan_extremes(const uint16_t *samples, size_t n,
                         const cnc_config_t *cfg, uint32_t *p2p_uv)
{
    uint16_t ymin = UINT16_MAX;
    uint16_t ymax = 0u;

    for (size_t i = 0u; i < n; i++) {
        if (samples[i] < ymin) { ymin = samples[i]; }
        if (samples[i] > ymax) { ymax = samples[i]; }
    }
    *p2p_uv = (uint32_t)(ymax - ymin) * CNC_LSB_UV;

    const uint32_t lo_uv = (uint32_t)ymin * CNC_LSB_UV;
    const uint32_t hi_uv = (uint32_t)ymax * CNC_LSB_UV;
    return lo_uv < cfg->v_min_uv || hi_uv > cfg->v_max_uv;
}

/* RMS of residuals over all samples; collects samples beyond the spike limit. */
static void measure_residuals(const uint16_t *samples, size_t n, const line_fit_t *fit,
                              uint32_t spike_limit_uv, cnc_result_t *out,
                              sample_set_t *outliers)
{
    uint64_t sum_sq = 0u;

    for (size_t i = 0u; i < n; i++) {
        const int64_t r_uv = residual_uv(fit, i, samples[i]);
        sum_sq += (uint64_t)(r_uv * r_uv);
        if (abs_u32(r_uv) > spike_limit_uv) {
            out->outlier_count++;
            set_add(outliers, i);
        }
    }
    out->rms_uv = rms_from_sum(sum_sq, fit->m);
}

/*
 * RMS of the non-outlier samples against a line fitted WITHOUT the outliers.
 * Without the refit, one spike in a short window bends the line and inflates
 * every other residual (DEF-001, found by edge case GA-06).
 */
static uint32_t clean_rms(const uint16_t *samples, size_t n, const sample_set_t *outliers)
{
    const line_fit_t clean = fit_line(samples, n, outliers);
    uint64_t sum_sq = 0u;

    for (size_t i = 0u; i < n; i++) {
        if (!set_contains(outliers, i)) {
            const int64_t r_uv = residual_uv(&clean, i, samples[i]);
            sum_sq += (uint64_t)(r_uv * r_uv);
        }
    }
    return rms_from_sum(sum_sq, clean.m);
}

static int is_isolated_spike_count(uint16_t count, const cnc_config_t *cfg)
{
    return count > 0u && count <= cfg->spike_max_count;
}

static uint8_t evaluate_flags(const cnc_result_t *r, const cnc_config_t *cfg)
{
    uint8_t flags = 0u;

    if (r->p2p_uv <= cfg->stuck_max_p2p_uv) {
        flags |= CNC_FLAG_STUCK;
    }
    if (is_isolated_spike_count(r->outlier_count, cfg) && r->rms_clean_uv <= cfg->rms_limit_uv) {
        flags |= CNC_FLAG_SPIKE;
    } else if (r->rms_clean_uv > cfg->rms_limit_uv || r->outlier_count > cfg->spike_max_count) {
        flags |= CNC_FLAG_ELEVATED_NOISE;
    }
    if (abs_u32(r->drift_uv) > cfg->drift_limit_uv) {
        flags |= CNC_FLAG_DRIFT;
    }
    return flags;
}

/* Highest-precedence finding wins (REQ-010). */
static cnc_class_t class_from_flags(uint8_t flags)
{
    static const struct {
        uint8_t     flag;
        cnc_class_t cls;
    } precedence[] = {
        { CNC_FLAG_OUT_OF_RANGE,   CNC_CLASS_OUT_OF_RANGE   },
        { CNC_FLAG_STUCK,          CNC_CLASS_STUCK          },
        { CNC_FLAG_SPIKE,          CNC_CLASS_SPIKE          },
        { CNC_FLAG_ELEVATED_NOISE, CNC_CLASS_ELEVATED_NOISE },
        { CNC_FLAG_DRIFT,          CNC_CLASS_DRIFT          },
    };

    for (size_t i = 0u; i < sizeof(precedence) / sizeof(precedence[0]); i++) {
        if ((flags & precedence[i].flag) != 0u) {
            return precedence[i].cls;
        }
    }
    return CNC_CLASS_OK;
}

/* ---- public API --------------------------------------------------------- */

cnc_status_t cnc_default_config(cnc_config_t *cfg)
{
    if (cfg == NULL) {
        return CNC_STATUS_ERR_NULL;
    }
    cfg->v_min_uv         = DEFAULT_V_MIN_UV;
    cfg->v_max_uv         = DEFAULT_V_MAX_UV;
    cfg->stuck_max_p2p_uv = DEFAULT_STUCK_MAX_P2P_UV;
    cfg->rms_limit_uv     = DEFAULT_RMS_LIMIT_UV;
    cfg->spike_limit_uv   = DEFAULT_SPIKE_LIMIT_UV;
    cfg->spike_max_count  = DEFAULT_SPIKE_MAX_COUNT;
    cfg->drift_limit_uv   = DEFAULT_DRIFT_LIMIT_UV;
    return CNC_STATUS_OK;
}

cnc_status_t cnc_validate_config(const cnc_config_t *cfg)
{
    if (cfg == NULL) {
        return CNC_STATUS_ERR_NULL;
    }
    const int voltage_range_ok = cfg->v_min_uv < cfg->v_max_uv
                              && cfg->v_max_uv <= (uint32_t)UINT16_MAX * CNC_LSB_UV;
    const int limits_ok        = cfg->rms_limit_uv > 0u && cfg->drift_limit_uv > 0u;
    const int spike_ok         = cfg->spike_limit_uv > cfg->rms_limit_uv  /* stands out from allowed noise */
                              && cfg->spike_max_count >= 1u
                              && cfg->spike_max_count <= MAX_SPIKE_COUNT;
    const int stuck_ok         = cfg->stuck_max_p2p_uv < cfg->spike_limit_uv;

    return (voltage_range_ok && limits_ok && spike_ok && stuck_ok)
         ? CNC_STATUS_OK : CNC_STATUS_ERR_CONFIG;
}

cnc_status_t cnc_classify(const uint16_t *samples, size_t n,
                          const cnc_config_t *cfg, cnc_result_t *out)
{
    const cnc_status_t st = validate_request(samples, n, cfg, out);
    if (out != NULL) {
        static const cnc_result_t zero_result; /* all zero: cls = INVALID */
        *out = zero_result; /* fail-safe until proven otherwise */
    }
    if (st != CNC_STATUS_OK) {
        return st;
    }

    sample_set_t outliers = {{0u}};

    const int out_of_range = scan_extremes(samples, n, cfg, &out->p2p_uv);
    const line_fit_t fit = fit_line(samples, n, NULL);
    out->mean_uv  = fit_mean_uv(&fit);
    out->drift_uv = fit_drift_uv(&fit);
    measure_residuals(samples, n, &fit, cfg->spike_limit_uv, out, &outliers);

    out->rms_clean_uv = is_isolated_spike_count(out->outlier_count, cfg)
                      ? clean_rms(samples, n, &outliers)
                      : out->rms_uv; /* no outliers, or a burst with no meaningful clean part */

    out->flags = evaluate_flags(out, cfg);
    if (out_of_range) {
        out->flags |= CNC_FLAG_OUT_OF_RANGE;
    }
    out->cls = class_from_flags(out->flags);
    return CNC_STATUS_OK;
}

const char *cnc_class_name(cnc_class_t cls)
{
    switch (cls) {
    case CNC_CLASS_INVALID:        return "INVALID";
    case CNC_CLASS_OUT_OF_RANGE:   return "OUT_OF_RANGE";
    case CNC_CLASS_STUCK:          return "STUCK";
    case CNC_CLASS_SPIKE:          return "SPIKE";
    case CNC_CLASS_ELEVATED_NOISE: return "ELEVATED_NOISE";
    case CNC_CLASS_DRIFT:          return "DRIFT";
    case CNC_CLASS_OK:             return "OK";
    default:                       return "UNKNOWN";
    }
}
