#pragma once

#include <stddef.h>
#include "guitar_types.h"
#include <toolbox/level_duration.h>

/** Build pluck params from a parsed riff note index. */
void guitar_pluck_params_from_riff_note(
    GuitarPluckParams* out,
    const GuitarRiff* riff,
    uint8_t note_index,
    const GuitarSettings* settings);

/** Composite guitar amplitude at time t (seconds), including envelope and harmonics. */
float guitar_pluck_amplitude(const GuitarPluckParams* params, float t_sec);

/**
 * Build merged ASK LevelDuration buffer from a pluck waveform.
 * Carrier is ON when amplitude exceeds threshold; OFF otherwise.
 * Returns number of durations written (0 on error).
 */
size_t guitar_build_pluck_ask(
    LevelDuration* out,
    size_t out_max,
    const GuitarPluckParams* params,
    uint8_t amplitude_threshold_pct);

/**
 * Append a short digital rolling-code preamble (OOK) before the pluck.
 * Uses a simple incrementing counter XOR'd with a device fingerprint.
 */
size_t guitar_build_rolling_preamble(
    LevelDuration* out,
    size_t out_max,
    uint32_t* rolling_code_io);
