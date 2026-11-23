/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Dan Michael
 * Narrow, derived 0xFX engine_internal.h header for standalone amp DSP.
 * Original: https://github.com/averagenative/0xFX (commit 5b248779c96dce6e64d0ed29cfa598014dc4d3c4)
 * See ../../licenses/0xFX-MIT.txt. No original 0xFX host/GUI/IR/FFT dependencies.
 */
#ifndef WYLDTONE_0XFX_AMP_INTERNAL_H
#define WYLDTONE_0XFX_AMP_INTERNAL_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FX_AMP_FULLERTON_CLEAN = 0,
    FX_AMP_BRIT_CRUNCH,
    FX_AMP_SOUTHWEST_LEAD,
    FX_AMP_ESSEX_CHIME,
    FX_AMP_TWEED_BLUES,
    FX_AMP_MERIDIAN_HIGH_GAIN,
    FX_AMP_CITRUS_ROAR,
    FX_AMP_CITRUS_TERROR,
    FX_AMP_REGENT_800,
    FX_AMP_SOLAR_MONOLITH,
    FX_AMP_ECLIPSE_DRONE,
    FX_AMP_EMERALD_DELUXE,
    FX_AMP_COUNT
} fx_amp_type_t;

typedef enum {
    FX_AMP_PARAM_GAIN = 0,
    FX_AMP_PARAM_VOLUME,
    FX_AMP_PARAM_BASS,
    FX_AMP_PARAM_MID,
    FX_AMP_PARAM_TREBLE,
    FX_AMP_PARAM_PRESENCE,
    FX_AMP_PARAM_SAG,
    FX_AMP_PARAM_MASTER,
    FX_AMP_PARAM_BRIGHT,
    FX_AMP_PARAM_CUT,
    FX_AMP_PARAM_TONE,
    FX_AMP_PARAM_FEEDBACK,
    FX_AMP_PARAM_COUNT
} fx_amp_param_t;

typedef struct {
    float b0,b1,b2,a1,a2;
    float z1,z2;
} fx_biquad_t;

#define AMP_MAX_PREAMP_STAGES 4
#define AMP_TONE_BANDS 3

typedef struct {
    float b0,b1,b2,b3;
    float a1,a2,a3;
    float z1,z2,z3;
} fx_tone_stack_3rd_t;

typedef struct {
    fx_amp_type_t type;
    float params[FX_AMP_PARAM_COUNT];
    float dc_block_z1[AMP_MAX_PREAMP_STAGES];
    int num_stages;
    fx_tone_stack_3rd_t tone_stack;
    fx_biquad_t tone_simple;
    fx_biquad_t presence_filter;
    float tone_sr;
    float tone_cache[4];
    float power_envelope;
    float sag_voltage;
    float feedback_z1;
} fx_amp_state_t;

void fx_amp_init(fx_amp_state_t* amp, fx_amp_type_t type);
void fx_amp_process(fx_amp_state_t* amp, float* buf, int n, float sr);
int fx_amp_get_param_count(fx_amp_type_t type);
const char* fx_amp_get_param_name(fx_amp_type_t type, fx_amp_param_t param);
const char* fx_amp_get_type_name(fx_amp_type_t type);

void fx_biquad_lowshelf(fx_biquad_t* bq, float freq, float gain_db, float sr);
void fx_biquad_highshelf(fx_biquad_t* bq, float freq, float gain_db, float sr);
void fx_biquad_peak(fx_biquad_t* bq, float freq, float gain_db, float q, float sr);
void fx_biquad_highpass(fx_biquad_t* bq, float freq, float q, float sr);
void fx_biquad_lowpass(fx_biquad_t* bq, float freq, float q, float sr);

static inline float fx_biquad_process(fx_biquad_t* bq, float in) {
    float out = bq->b0 * in + bq->z1;
    bq->z1 = bq->b1 * in - bq->a1 * out + bq->z2;
    bq->z2 = bq->b2 * in - bq->a2 * out;
    if (bq->z1 > -1e-15f && bq->z1 < 1e-15f) bq->z1 = 0.0f;
    if (bq->z2 > -1e-15f && bq->z2 < 1e-15f) bq->z2 = 0.0f;
    return out;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif
