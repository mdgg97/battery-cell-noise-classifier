/**
 * @file cell_ir.h
 * @brief Cell internal resistance from a passive balancing step, and SoH from resistance.
 *
 * Idea: the firmware switches the balancing bleed current on at a known sample.
 * The cell voltage drops by dV = I * R. Averaging the samples before and after
 * the step gives dV with an uncertainty small enough even for mA currents:
 *
 *   R      = -dV / I                         (I = balancing current, discharge positive)
 *   sigma  = sqrt(var_pre/k + var_post/m) / I
 *   SoH_R  = (R_eol - R) / (R_eol - R_bol)   clamped to 0..100 %
 *
 * Caveats handled by configuration / quality codes:
 *   - With AFE-internal balancing the current also flows through the sense wire,
 *     so the measured value is R_cell + R_wire -> r_wire_uohm is subtracted.
 *   - Only a quiet window measures resistance: both windows must classify OK
 *     with the noise classifier (no spike, drift or excess noise).
 *   - R measured this way is the resistance at the post-window time scale
 *     (ohmic + fast polarisation), not a pure ohmic value.
 *
 * Integer-only, no heap, a constant number of 64-bit divisions.
 */
#ifndef CELL_IR_H
#define CELL_IR_H

#include <stddef.h>
#include <stdint.h>

#include "cell_noise.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CIR_MIN_WINDOW 8u   /* samples needed before and after the step */

typedef enum {
    CIR_QUALITY_INVALID = 0,     /**< input or config rejected (fail-safe default) */
    CIR_QUALITY_OK,              /**< R and SoH are valid */
    CIR_QUALITY_LOW_CURRENT,     /**< balancing current below min_current_ua */
    CIR_QUALITY_NOISY_WINDOW,    /**< a window is not clean (spike, drift, noise, range, stuck) */
    CIR_QUALITY_NO_RESPONSE,     /**< no plausible voltage drop: balancing switch or bleed resistor fault? */
    CIR_QUALITY_UNCERTAIN        /**< relative uncertainty above max_rel_sigma_permille */
} cir_quality_t;

typedef enum {
    CIR_STATUS_OK = 0,
    CIR_STATUS_ERR_NULL,
    CIR_STATUS_ERR_WINDOW,       /**< step index leaves fewer than CIR_MIN_WINDOW samples on a side */
    CIR_STATUS_ERR_CONFIG
} cir_status_t;

typedef struct {
    cnc_config_t noise;               /**< classifier settings used as window quality gate */
    uint16_t guard_samples;           /**< samples skipped after the step (switching transient) */
    uint32_t min_current_ua;          /**< smaller currents give too small a voltage step */
    uint32_t max_rel_sigma_permille;  /**< accept R only if sigma/R is below this */
    uint32_t r_wire_uohm;             /**< sense-wire resistance in the balancing path */
    uint32_t r_min_uohm;              /**< plausibility: a real cell is never below this (default R_bol / 2) */
    uint32_t r_bol_uohm;              /**< resistance of the new cell (begin of life) */
    uint32_t r_eol_uohm;              /**< resistance at end of life */
} cir_config_t;

typedef struct {
    cir_quality_t quality;
    int32_t  dv_uv;           /**< post - pre mean voltage (negative when discharging) */
    uint32_t dv_sigma_uv;     /**< standard uncertainty of dv */
    int32_t  r_uohm;          /**< cell resistance, wire resistance removed */
    uint32_t r_sigma_uohm;    /**< standard uncertainty of r */
    uint16_t soh_permille;    /**< 1000 = new cell, 0 = end of life (valid only if quality OK) */
} cir_result_t;

/** Defaults: 18650-size NMC cell, R_bol 40 mOhm, R_eol 2 x R_bol, R_min R_bol / 2, 10 mA minimum current, 5 % max uncertainty. */
cir_status_t cir_default_config(cir_config_t *cfg);

cir_status_t cir_validate_config(const cir_config_t *cfg);

/**
 * Estimate the internal resistance from one window with a balancing step.
 *
 * @param samples     raw cell voltage samples (CNC_LSB_UV per LSB)
 * @param n           number of samples (16 .. CNC_MAX_SAMPLES)
 * @param step_index  first sample taken with the balancing current on
 * @param current_ua  balancing current in microamps (discharge positive)
 *
 * On error the result is written with quality INVALID and all values zero.
 */
cir_status_t cir_estimate(const uint16_t *samples, size_t n, size_t step_index,
                          uint32_t current_ua, const cir_config_t *cfg, cir_result_t *out);

/** State of health from resistance, in permille, clamped to 0..1000. */
uint16_t cir_soh_permille(int32_t r_uohm, uint32_t r_bol_uohm, uint32_t r_eol_uohm);

const char *cir_quality_name(cir_quality_t q);

#ifdef __cplusplus
}
#endif

#endif /* CELL_IR_H */
