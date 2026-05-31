#include "speaker_pluck.h"

#include "waveform.h"

#include <furi.h>
#include <furi_hal_speaker.h>
#include <math.h>

#define TAG "GuitarSpeaker"

static GuitarPluckParams g_speaker_notes[GUITAR_RIFF_MAX_NOTES];
static float g_speaker_note_start[GUITAR_RIFF_MAX_NOTES];
static uint8_t g_speaker_note_count = 0;
static bool g_speaker_active = false;

float guitar_riff_total_duration_s(const GuitarRiff* riff, const GuitarSettings* settings) {
    furi_check(riff);
    furi_check(settings);

    float total = 0.0f;
    for(uint8_t i = 0; i < riff->count; i++) {
        GuitarPluckParams params;
        guitar_pluck_params_from_riff_note(&params, riff, i, settings);
        total += params.duration_s;
        if(i + 1 < riff->count) {
            total += 0.08f;
        }
    }
    return total;
}

void guitar_speaker_pluck_begin_riff(const GuitarRiff* riff, const GuitarSettings* settings) {
    furi_check(riff);
    furi_check(settings);

    g_speaker_note_count = riff->count;
    float t = 0.0f;
    for(uint8_t i = 0; i < riff->count; i++) {
        guitar_pluck_params_from_riff_note(&g_speaker_notes[i], riff, i, settings);
        g_speaker_note_start[i] = t;
        t += g_speaker_notes[i].duration_s;
        if(i + 1 < riff->count) t += 0.08f;
    }

    if(furi_hal_speaker_acquire(500)) {
        g_speaker_active = true;
    }
}

static uint8_t guitar_speaker_note_at(float elapsed_s) {
    for(uint8_t i = 0; i < g_speaker_note_count; i++) {
        const float start = g_speaker_note_start[i];
        const float end = start + g_speaker_notes[i].duration_s;
        if(elapsed_s >= start && elapsed_s <= end) return i;
    }
    if(g_speaker_note_count > 0) return g_speaker_note_count - 1;
    return 0;
}

void guitar_speaker_pluck_update(float elapsed_s) {
    if(!g_speaker_active || g_speaker_note_count == 0) return;

    const uint8_t idx = guitar_speaker_note_at(elapsed_s);
    const float local_t = elapsed_s - g_speaker_note_start[idx];
    const GuitarPluckParams* params = &g_speaker_notes[idx];

    const float amp = guitar_pluck_amplitude(params, local_t);
    if(amp < 0.01f) {
        furi_hal_speaker_stop();
        return;
    }

    float freq = params->base_hz;
    if(params->pitch_bend_hz > 0.0f && params->pitch_bend_duration_s > 0.0f) {
        const float bend = fminf(local_t / params->pitch_bend_duration_s, 1.0f);
        freq += params->pitch_bend_hz * bend;
    }

    const float volume = 0.15f + (amp * 0.55f);
    furi_hal_speaker_start(freq, volume);
}

void guitar_speaker_pluck_end(void) {
    if(g_speaker_active) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
        g_speaker_active = false;
    }
    g_speaker_note_count = 0;
}
