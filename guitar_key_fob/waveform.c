#include "waveform.h"

#include <furi.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAG "GuitarWaveform"

/** Fast sine via 256-entry LUT (one full period). */
static float guitar_sin_lut(float phase_rad) {
    static const int8_t lut[256] = {
        0,   3,   6,   9,   12,  16,  19,  22,  25,  28,  31,  34,  37,  40,  43,  46,  49,  52,  55,  57,
        60,  63,  65,  68,  71,  73,  76,  78,  81,  83,  85,  88,  90,  92,  94,  96,  98,  100, 102, 104,
        106, 107, 109, 111, 112, 113, 115, 116, 117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126,
        126, 127, 127, 127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118, 117,
        116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100, 98,  96,  94,  92,  90,  88,  85,  83,  81,
        78,  76,  73,  71,  68,  65,  63,  60,  57,  55,  52,  49,  46,  43,  40,  37,  34,  31,  28,  25,
        22,  19,  16,  12,  9,   6,   3,   0,   -3,  -6,  -9,  -12, -16, -19, -22, -25, -28, -31, -34, -37,
        -40, -43, -46, -49, -52, -55, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88, -90,
        -92, -94, -96, -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116, -117, -118, -120,
        -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127, -127, -127, -127, -126, -126,
        -126, -125, -125, -124, -123, -122, -122, -121, -120, -118, -117, -116, -115, -113, -112, -111, -109,
        -107, -106, -104, -102, -100, -98, -96, -94, -92, -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65,
        -63, -60, -57, -55, -52, -49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12, -9, -6, -3,
    };

    const float normalized = phase_rad / (2.0f * (float)M_PI);
    float wrapped = normalized - floorf(normalized);
    if(wrapped < 0.0f) wrapped += 1.0f;
    const uint32_t idx = (uint32_t)(wrapped * 256.0f) & 0xFF;
    return (float)lut[idx] / 127.0f;
}

/** Exponential pluck envelope: fast attack, slow decay. */
static float guitar_envelope(float t_sec, float attack_s, float decay_tau_s) {
    if(t_sec < 0.0f) return 0.0f;
    if(t_sec < attack_s) {
        return t_sec / attack_s;
    }
    const float decay_t = t_sec - attack_s;
    return expf(-decay_t / decay_tau_s);
}

void guitar_pluck_params_from_riff_note(
    GuitarPluckParams* out,
    const GuitarRiff* riff,
    uint8_t note_index,
    const GuitarSettings* settings) {
    furi_check(out);
    furi_check(riff);
    furi_check(settings);
    furi_check(note_index < riff->count);

    memset(out, 0, sizeof(GuitarPluckParams));

    out->attack_s = (float)settings->attack_ms / 1000.0f;
    out->decay_tau_s = (float)settings->decay_ms / 1000.0f;
    out->duration_s = out->attack_s + out->decay_tau_s * 4.0f;
    if(out->duration_s > ((float)GUITAR_MAX_PLUCK_MS / 1000.0f)) {
        out->duration_s = (float)GUITAR_MAX_PLUCK_MS / 1000.0f;
    }

    out->harmonic2_ratio = (float)settings->harmonic2_pct / 100.0f;
    out->harmonic3_ratio = (float)settings->harmonic3_pct / 100.0f;
    out->vibrato_depth = (float)settings->vibrato_depth_pct / 100.0f;
    out->vibrato_hz = (float)settings->vibrato_hz;

    out->base_hz = riff->notes[note_index].hz;
    out->pitch_bend_hz = riff->notes[note_index].bend_hz;
    if(out->pitch_bend_hz > 0.0f) {
        out->pitch_bend_duration_s = out->duration_s;
    }

    if(riff->count > 1) {
        out->duration_s = out->attack_s + out->decay_tau_s * 2.5f;
        if(out->duration_s > 0.35f) out->duration_s = 0.35f;
        if(out->pitch_bend_hz > 0.0f) {
            out->pitch_bend_duration_s = out->duration_s;
        }
    }
}

float guitar_pluck_amplitude(const GuitarPluckParams* params, float t_sec) {
    furi_check(params);

    if(t_sec < 0.0f || t_sec > params->duration_s) return 0.0f;

    const float env = guitar_envelope(t_sec, params->attack_s, params->decay_tau_s);
    if(env <= 0.0001f) return 0.0f;

    float freq = params->base_hz;
    if(params->pitch_bend_hz > 0.0f && params->pitch_bend_duration_s > 0.0f) {
        const float bend = fminf(t_sec / params->pitch_bend_duration_s, 1.0f);
        freq += params->pitch_bend_hz * bend;
    }

    if(params->vibrato_depth > 0.0f && params->vibrato_hz > 0.0f) {
        const float vib =
            params->vibrato_depth * guitar_sin_lut(2.0f * (float)M_PI * params->vibrato_hz * t_sec);
        freq *= (1.0f + 0.02f * vib);
    }

    const float w = 2.0f * (float)M_PI * freq;
    const float fundamental = guitar_sin_lut(w * t_sec);
    const float h2 = guitar_sin_lut(2.0f * w * t_sec) * params->harmonic2_ratio;
    const float h3 = guitar_sin_lut(3.0f * w * t_sec) * params->harmonic3_ratio;

    float sample = fundamental + h2 + h3;
    const float norm = 1.0f + params->harmonic2_ratio + params->harmonic3_ratio;
    sample /= norm;

    return env * fabsf(sample);
}

static void guitar_append_duration(
    LevelDuration* out,
    size_t* out_count,
    size_t out_max,
    bool level,
    uint32_t duration_us) {
    if(duration_us == 0) return;

    if(*out_count > 0) {
        const bool last_level = level_duration_get_level(out[*out_count - 1]);
        if(last_level == level) {
            const uint32_t merged =
                level_duration_get_duration(out[*out_count - 1]) + duration_us;
            out[*out_count - 1] = level_duration_make(level, merged);
            return;
        }
    }

    if(*out_count >= out_max) return;
    out[*out_count] = level_duration_make(level, duration_us);
    (*out_count)++;
}

size_t guitar_build_pluck_ask(
    LevelDuration* out,
    size_t out_max,
    const GuitarPluckParams* params,
    uint8_t amplitude_threshold_pct) {
    furi_check(out);
    furi_check(params);

    size_t count = 0;
    const float threshold = (float)amplitude_threshold_pct / 100.0f;
    const float dt = (float)GUITAR_ASK_SAMPLE_US / 1000000.0f;
    const uint32_t sample_us = GUITAR_ASK_SAMPLE_US;

    for(float t = 0.0f; t <= params->duration_s; t += dt) {
        const float amp = guitar_pluck_amplitude(params, t);
        const bool on = amp >= threshold;
        guitar_append_duration(out, &count, out_max, on, sample_us);
    }

    return count;
}

size_t guitar_build_rolling_preamble(
    LevelDuration* out,
    size_t out_max,
    uint32_t* rolling_code_io) {
    furi_check(out);
    furi_check(rolling_code_io);

    const uint32_t te_short = 350;
    const uint32_t te_long = 1050;
    const uint32_t device_id = 0x617469UL; /* "ati" fingerprint mixed into code */
    uint32_t code = (*rolling_code_io)++;
    code ^= (device_id << 8) ^ 0xA5A5u;

    size_t count = 0;

    /* Sync: alternating short pulses */
    for(uint8_t i = 0; i < 8; i++) {
        guitar_append_duration(out, &count, out_max, true, te_short);
        guitar_append_duration(out, &count, out_max, false, te_short);
    }

    /* 16-bit rolling identifier (MSB first, simple OOK) */
    for(int bit = 15; bit >= 0; bit--) {
        const bool bit_val = (code >> bit) & 1;
        guitar_append_duration(out, &count, out_max, true, bit_val ? te_long : te_short);
        guitar_append_duration(out, &count, out_max, false, te_short);
    }

    /* Gap before analog pluck */
    guitar_append_duration(out, &count, out_max, false, 2000);

    return count;
}
