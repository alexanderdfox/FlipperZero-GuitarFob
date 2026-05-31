#pragma once

#include "guitar_types.h"

/** Begin audible riff playback (acquires speaker). */
void guitar_speaker_pluck_begin_riff(const GuitarRiff* riff, const GuitarSettings* settings);

/** Update speaker tone to match riff at elapsed time. */
void guitar_speaker_pluck_update(float elapsed_s);

/** Stop speaker and release ownership. */
void guitar_speaker_pluck_end(void);

/** Estimated total duration of a riff in seconds. */
float guitar_riff_total_duration_s(const GuitarRiff* riff, const GuitarSettings* settings);
