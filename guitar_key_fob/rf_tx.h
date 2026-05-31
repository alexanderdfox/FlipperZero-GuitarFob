#pragma once

#include <stddef.h>
#include "guitar_types.h"
#include <toolbox/level_duration.h>

typedef struct {
    LevelDuration buffer[GUITAR_MAX_RAW_DURATIONS];
    size_t count;
    size_t index;
    volatile bool complete;
} GuitarRfEncoder;

/** Reset encoder index for a new transmission. */
void guitar_rf_encoder_reset(GuitarRfEncoder* encoder);

/** Clear buffer and reset state. */
void guitar_rf_encoder_clear(GuitarRfEncoder* encoder);

/** Async TX yield callback — pass to subghz_devices_start_async_tx(). */
LevelDuration guitar_rf_encoder_yield(void* context);

/**
 * Build full TX buffer: rolling preamble + guitar pluck ASK waveform.
 * For alarm actions, call multiple times with different pluck params.
 */
bool guitar_rf_encoder_build_pluck(
    GuitarRfEncoder* encoder,
    const GuitarPluckParams* params,
    GuitarSettings* settings,
    GuitarAction action);

/**
 * Transmit the prepared buffer at the configured carrier frequency.
 * riff drives audible feedback (may be NULL to skip).
 */
bool guitar_rf_transmit(
    GuitarRfEncoder* encoder,
    GuitarSettings* settings,
    const GuitarRiff* riff);
