#pragma once

#include "rx_types.h"
#include <toolbox/level_duration.h>

/** Reset decoder capture state. */
void guitar_rx_decoder_reset(void);

/** Feed one async-RX duration sample. Returns true when a full frame is ready. */
bool guitar_rx_decoder_feed(bool level, uint32_t duration_us);

/** Consume the last decoded frame (valid until next reset). */
const GuitarRxFrame* guitar_rx_decoder_get_frame(void);

/** Format riff as string, e.g. "196,330+8". */
void guitar_rx_riff_format(const GuitarRxRiff* riff, char* out, size_t out_len);

/** Parse riff string into notes. */
bool guitar_rx_riff_parse(GuitarRxRiff* out, const char* str);

/** Compare received riff to a stored profile string; returns match score 0.0–1.0. */
float guitar_rx_riff_match_score(const GuitarRxRiff* received, const char* profile_str);

/** Pick best matching action from synced profiles, or GuitarRxActionCount if none. */
GuitarRxAction guitar_rx_match_action(const GuitarRxRiff* received, const GuitarRxSettings* settings);

/** Identify button: preamble (v2 TX), profile match, then note guess. */
GuitarRxAction guitar_rx_identify_button(
    const GuitarRxFrame* frame,
    const GuitarRxSettings* settings);

/** Action display name. */
const char* guitar_rx_action_name(GuitarRxAction action);

/** Action with note label, e.g. "Unlock (G3)". */
const char* guitar_rx_action_button(GuitarRxAction action);

/** Get profile string for action. */
const char* guitar_rx_profile_for_action(const GuitarRxSettings* settings, GuitarRxAction action);

/** Writable profile string for action. */
char* guitar_rx_profile_mut_for_action(GuitarRxSettings* settings, GuitarRxAction action);
