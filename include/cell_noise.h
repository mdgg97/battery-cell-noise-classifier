/**
 * @file cell_noise.h
 * @brief Battery cell measurement noise classifier (embedded C, no heap, integer-only).
 *
 * The classifier takes a window of cell voltage samples, as delivered by a
 * battery monitoring AFE, and decides whether the *measurement* looks healthy
 * or shows one of several typical fault patterns:
 *
 *   - OUT_OF_RANGE    a sample lies outside the plausible cell voltage range
 *                     (open wire, ADC saturation, wrong channel mapping)
 *   - STUCK           the signal does not move at all (frozen ADC / stale register)
 *   - SPIKE           a few isolated outliers on an otherwise clean signal (EMI burst)
 *   - ELEVATED_NOISE  broadband noise above the allowed RMS level (bad grounding, loose contact)
 *   - DRIFT           a steady trend across the window (thermal drift, reference drift)
 *   - OK              none of the above
 *
 * Design rules (see docs/requirements.md):
 *   - no dynamic memory, no floating point, no recursion
 *   - deterministic run time: O(n) with n <= CNC_MAX_SAMPLES
 *   - fail-safe: invalid input never yields CNC_CLASS_OK
 *
 * Sample format: unsigned 16-bit, 1 LSB = CNC_LSB_UV microvolts (100 uV).
 * All thresholds and features are expressed in microvolts.
 */
#ifndef CELL_NOISE_H
#define CELL_NOISE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CNC_LSB_UV       100u  /**< microvolts per sample LSB */
#define CNC_MIN_SAMPLES  8u    /**< smallest window that gives meaningful statistics */
#define CNC_MAX_SAMPLES  256u  /**< largest window (bounds stack use and int64 headroom) */

/** Classification result, ordered by precedence (highest first after INVALID). */
typedef enum {
    CNC_CLASS_INVALID        = 0, /**< input or config rejected (fail-safe default) */
    CNC_CLASS_OUT_OF_RANGE   = 1,
    CNC_CLASS_STUCK          = 2,
    CNC_CLASS_SPIKE          = 3,
    CNC_CLASS_ELEVATED_NOISE = 4,
    CNC_CLASS_DRIFT          = 5,
    CNC_CLASS_OK             = 6
} cnc_class_t;

/** Individual findings; several may be set at once, the class is the highest-precedence one. */
enum {
    CNC_FLAG_OUT_OF_RANGE   = 1u << 0,
    CNC_FLAG_STUCK          = 1u << 1,
    CNC_FLAG_SPIKE          = 1u << 2,
    CNC_FLAG_ELEVATED_NOISE = 1u << 3,
    CNC_FLAG_DRIFT          = 1u << 4
};

typedef enum {
    CNC_STATUS_OK = 0,
    CNC_STATUS_ERR_NULL,    /**< a required pointer was NULL */
    CNC_STATUS_ERR_LENGTH,  /**< n outside [CNC_MIN_SAMPLES, CNC_MAX_SAMPLES] */
    CNC_STATUS_ERR_CONFIG   /**< configuration is inconsistent */
} cnc_status_t;

typedef struct {
    uint32_t v_min_uv;          /**< lowest plausible cell voltage */
    uint32_t v_max_uv;          /**< highest plausible cell voltage */
    uint32_t stuck_max_p2p_uv;  /**< peak-to-peak at or below this => STUCK (0 = all samples identical) */
    uint32_t rms_limit_uv;      /**< detrended RMS above this => ELEVATED_NOISE */
    uint32_t spike_limit_uv;    /**< |residual| above this counts as an outlier */
    uint16_t spike_max_count;   /**< 1..spike_max_count outliers on a clean signal => SPIKE */
    uint32_t drift_limit_uv;    /**< |trend over the window| above this => DRIFT */
} cnc_config_t;

typedef struct {
    cnc_class_t cls;
    uint8_t     flags;          /**< CNC_FLAG_* bitmask */
    uint32_t    mean_uv;        /**< arithmetic mean */
    uint32_t    p2p_uv;         /**< max - min */
    uint32_t    rms_uv;         /**< RMS of residuals after removing the linear trend */
    uint32_t    rms_clean_uv;   /**< same, excluding outliers */
    int32_t     drift_uv;       /**< fitted change from first to last sample (signed) */
    uint16_t    outlier_count;  /**< samples with |residual| > spike_limit_uv */
} cnc_result_t;

/** Fill @p cfg with defaults for a Li-ion NMC cell (2.5 V .. 4.3 V). */
cnc_status_t cnc_default_config(cnc_config_t *cfg);

/** Check a configuration for internal consistency. */
cnc_status_t cnc_validate_config(const cnc_config_t *cfg);

/**
 * Classify one window of samples.
 *
 * On any error the function still writes @p out (if not NULL) with
 * cls = CNC_CLASS_INVALID and all features zero.
 */
cnc_status_t cnc_classify(const uint16_t *samples, size_t n,
                          const cnc_config_t *cfg, cnc_result_t *out);

/** Human-readable class name, never NULL. */
const char *cnc_class_name(cnc_class_t cls);

/** Integer square root, floor(sqrt(x)). Exposed for unit testing. */
uint32_t cnc_isqrt64(uint64_t x);

#ifdef __cplusplus
}
#endif

#endif /* CELL_NOISE_H */
