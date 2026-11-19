/**
 * @file cell_ir.c
 * @brief Internal resistance from a balancing step (see cell_ir.h).
 *
 * Overflow budget (window <= 256 samples, y <= 65535 LSB):
 *   k * sum(y^2)                       <= 2.8e14
 *   (k*sum(y^2) - sum(y)^2) * 100^2    <= 2.8e18   (< INT64_MAX = 9.2e18)
 *   |sum_post*k - sum_pre*m| * 1e8     <= 4.3e17
 */
#include "cell_ir.h"

#define DEFAULT_GUARD_SAMPLES       1u
#define DEFAULT_MIN_CURRENT_UA      10000u  /* 10 mA */
#define DEFAULT_MAX_REL_SIGMA_PML   50u     /* 5 % */
#define DEFAULT_R_WIRE_UOHM         0u      /* set per board after calibration */
#define DEFAULT_R_MIN_UOHM          20000u  /* R_bol / 2 */
#define DEFAULT_R_BOL_UOHM          40000u  /* 40 mOhm */
#define DEFAULT_R_EOL_UOHM          80000u  /* 2 x begin of life */

#define UOHM_PER_OHM      1000000
#define NO_RESPONSE_SIGMA 2   /* a real step must exceed 2 sigma */

typedef struct {
    int64_t count;
    int64_t sum;
    int64_t sum_sq;
} window_stats_t;

static int64_t div_round(int64_t num, int64_t den)
{
    if (num >= 0) {
        return (num + den / 2) / den;
    }
    return -((-num + den / 2) / den);
}

static int32_t saturate_i32(int64_t v)
{
    if (v > INT32_MAX) { return INT32_MAX; }
    if (v < INT32_MIN) { return INT32_MIN; }
    return (int32_t)v;
}

static window_stats_t stats(const uint16_t *samples, size_t from, size_t to)
{
    window_stats_t s = {0, 0, 0};
    for (size_t i = from; i < to; i++) {
        const int64_t y = (int64_t)samples[i];
        s.count++;
        s.sum += y;
        s.sum_sq += y * y;
    }
    return s;
}

/* Variance of the window MEAN in uV^2 (sample variance / count). */
static uint64_t var_of_mean_uv2(const window_stats_t *s)
{
    const int64_t num = s->count * s->sum_sq - s->sum * s->sum; /* >= 0 */
    const int64_t den = s->count * s->count * (s->count - 1);
    return (uint64_t)(num * (int64_t)CNC_LSB_UV * (int64_t)CNC_LSB_UV / den);
}

static int window_is_clean(const uint16_t *samples, size_t from, size_t to, const cnc_config_t *noise)
{
    cnc_result_t r;
    return cnc_classify(&samples[from], to - from, noise, &r) == CNC_STATUS_OK
        && r.cls == CNC_CLASS_OK;
}

static void set_invalid(cir_result_t *out)
{
    if (out != NULL) {
        static const cir_result_t zero_result; /* quality = INVALID */
        *out = zero_result;
    }
}

/* ---- public API --------------------------------------------------------- */

cir_status_t cir_default_config(cir_config_t *cfg)
{
    if (cfg == NULL) {
        return CIR_STATUS_ERR_NULL;
    }
    (void)cnc_default_config(&cfg->noise);
    cfg->guard_samples          = DEFAULT_GUARD_SAMPLES;
    cfg->min_current_ua         = DEFAULT_MIN_CURRENT_UA;
    cfg->max_rel_sigma_permille = DEFAULT_MAX_REL_SIGMA_PML;
    cfg->r_wire_uohm            = DEFAULT_R_WIRE_UOHM;
    cfg->r_min_uohm             = DEFAULT_R_MIN_UOHM;
    cfg->r_bol_uohm             = DEFAULT_R_BOL_UOHM;
    cfg->r_eol_uohm             = DEFAULT_R_EOL_UOHM;
    return CIR_STATUS_OK;
}

cir_status_t cir_validate_config(const cir_config_t *cfg)
{
    if (cfg == NULL) {
        return CIR_STATUS_ERR_NULL;
    }
    const int ok = cnc_validate_config(&cfg->noise) == CNC_STATUS_OK
                && cfg->guard_samples <= CNC_MAX_SAMPLES
                && cfg->min_current_ua > 0u
                && cfg->max_rel_sigma_permille > 0u
                && cfg->max_rel_sigma_permille <= 1000u
                && cfg->r_min_uohm < cfg->r_bol_uohm
                && cfg->r_bol_uohm < cfg->r_eol_uohm
                && cfg->r_eol_uohm <= (uint32_t)INT32_MAX;
    return ok ? CIR_STATUS_OK : CIR_STATUS_ERR_CONFIG;
}

uint16_t cir_soh_permille(int32_t r_uohm, uint32_t r_bol_uohm, uint32_t r_eol_uohm)
{
    if (r_eol_uohm <= r_bol_uohm || r_uohm >= (int64_t)r_eol_uohm) {
        return 0u;
    }
    if (r_uohm <= (int64_t)r_bol_uohm) {
        return 1000u;
    }
    const int64_t used = (int64_t)r_eol_uohm - r_uohm;
    const int64_t span = (int64_t)r_eol_uohm - (int64_t)r_bol_uohm;
    return (uint16_t)div_round(used * 1000, span);
}

cir_status_t cir_estimate(const uint16_t *samples, size_t n, size_t step_index,
                          uint32_t current_ua, const cir_config_t *cfg, cir_result_t *out)
{
    set_invalid(out);
    if (samples == NULL || cfg == NULL || out == NULL) {
        return CIR_STATUS_ERR_NULL;
    }
    if (cir_validate_config(cfg) != CIR_STATUS_OK) {
        return CIR_STATUS_ERR_CONFIG;
    }
    const size_t post_start = step_index + cfg->guard_samples;
    if (n > CNC_MAX_SAMPLES || step_index < CIR_MIN_WINDOW
        || post_start > n || n - post_start < CIR_MIN_WINDOW) {
        return CIR_STATUS_ERR_WINDOW;
    }

    /* Voltage step: difference of the window means, exact in integer. */
    const window_stats_t pre  = stats(samples, 0u, step_index);
    const window_stats_t post = stats(samples, post_start, n);
    const int64_t dv_num = post.sum * pre.count - pre.sum * post.count; /* LSB * k * m */
    const int64_t km = pre.count * post.count;

    out->dv_uv       = saturate_i32(div_round(dv_num * (int64_t)CNC_LSB_UV, km));
    out->dv_sigma_uv = cnc_isqrt64(var_of_mean_uv2(&pre) + var_of_mean_uv2(&post));

    /* Resistance: R = -dV / I, then remove the sense-wire share. */
    const int64_t current = (int64_t)current_ua;
    const int64_t r_meas = div_round(-dv_num * (int64_t)CNC_LSB_UV * UOHM_PER_OHM, km * current);
    out->r_uohm       = saturate_i32(r_meas - (int64_t)cfg->r_wire_uohm);
    out->r_sigma_uohm = (uint32_t)saturate_i32(
        div_round((int64_t)out->dv_sigma_uv * UOHM_PER_OHM, current));

    /* Quality gates, in order of what makes the value meaningless first. */
    if (current_ua < cfg->min_current_ua) {
        out->quality = CIR_QUALITY_LOW_CURRENT;
    } else if (!window_is_clean(samples, 0u, step_index, &cfg->noise)
            || !window_is_clean(samples, post_start, n, &cfg->noise)) {
        out->quality = CIR_QUALITY_NOISY_WINDOW;
    } else if (-(int64_t)out->dv_uv <= NO_RESPONSE_SIGMA * (int64_t)out->dv_sigma_uv
            || out->r_uohm < (int64_t)cfg->r_min_uohm) {
        /* statistics alone are not enough: 1 in ~40 fault windows exceeds 2 sigma by
         * chance, so the drop must also be physically plausible for a real cell */
        out->quality = CIR_QUALITY_NO_RESPONSE;
    } else if ((int64_t)out->r_sigma_uohm * 1000
               > (int64_t)cfg->max_rel_sigma_permille * out->r_uohm) {
        out->quality = CIR_QUALITY_UNCERTAIN;
    } else {
        out->quality = CIR_QUALITY_OK;
        out->soh_permille = cir_soh_permille(out->r_uohm, cfg->r_bol_uohm, cfg->r_eol_uohm);
    }
    return CIR_STATUS_OK;
}

const char *cir_quality_name(cir_quality_t q)
{
    switch (q) {
    case CIR_QUALITY_INVALID:      return "INVALID";
    case CIR_QUALITY_OK:           return "OK";
    case CIR_QUALITY_LOW_CURRENT:  return "LOW_CURRENT";
    case CIR_QUALITY_NOISY_WINDOW: return "NOISY_WINDOW";
    case CIR_QUALITY_NO_RESPONSE:  return "NO_RESPONSE";
    case CIR_QUALITY_UNCERTAIN:    return "UNCERTAIN";
    default:                       return "UNKNOWN";
    }
}
