#include "rx_decoder.h"

#include <furi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "GuitarRxDec"

#define GUITAR_RX_DEVICE_ID 0x617469UL
#define GUITAR_RX_CODE_XOR ((uint16_t)((GUITAR_RX_DEVICE_ID << 8) ^ 0xA5A5u))
#define GUITAR_RX_CODE_VERSION 0x2u
#define GUITAR_RX_MATCH_THRESHOLD 0.45f

static LevelDuration g_capture[GUITAR_RX_MAX_CAPTURE];
static size_t g_capture_count = 0;
static uint32_t g_silence_us = 0;
static bool g_had_activity = false;
static GuitarRxFrame g_frame;

static bool guitar_rx_duration_near(uint32_t value, uint32_t target, uint32_t pct) {
    const uint32_t delta = (target * pct) / 100;
    return value >= (target - delta) && value <= (target + delta);
}

static bool guitar_rx_riff_parse_note(GuitarRxNote* note, const char* token) {
    while(*token == ' ') token++;
    if(*token == '\0') return false;

    char* end = NULL;
    const float hz = strtof(token, &end);
    if(end == token || hz < 50.0f || hz > 2000.0f) return false;

    note->hz = hz;
    note->bend_hz = 0.0f;
    if(*end == '+') {
        const float bend = strtof(end + 1, &end);
        if(bend > 0.0f && bend <= 100.0f) note->bend_hz = bend;
    }
    return true;
}

void guitar_rx_riff_format(const GuitarRxRiff* riff, char* out, size_t out_len) {
    furi_check(riff);
    furi_check(out);
    furi_check(out_len > 0);

    out[0] = '\0';
    for(uint8_t i = 0; i < riff->count; i++) {
        char part[16];
        if(riff->notes[i].bend_hz > 0.5f) {
            snprintf(
                part,
                sizeof(part),
                "%.0f+%.0f",
                (double)riff->notes[i].hz,
                (double)riff->notes[i].bend_hz);
        } else {
            snprintf(part, sizeof(part), "%.0f", (double)riff->notes[i].hz);
        }

        if(out[0] != '\0') {
            size_t len = strlen(out);
            if(len + 1 < out_len) {
                out[len] = ',';
                out[len + 1] = '\0';
                len++;
            }
            const size_t part_len = strlen(part);
            if(len + part_len < out_len) {
                memcpy(out + len, part, part_len + 1);
            }
        } else {
            strncpy(out, part, out_len - 1);
            out[out_len - 1] = '\0';
        }
    }
}

bool guitar_rx_riff_parse(GuitarRxRiff* out, const char* str) {
    furi_check(out);
    furi_check(str);

    memset(out, 0, sizeof(GuitarRxRiff));

    char work[GUITAR_RX_RIFF_STR_LEN];
    strncpy(work, str, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    char* cursor = work;
    while(*cursor && out->count < GUITAR_RX_RIFF_MAX_NOTES) {
        char token[16];
        size_t ti = 0;
        while(*cursor && *cursor != ',' && ti < sizeof(token) - 1) {
            token[ti++] = *cursor++;
        }
        token[ti] = '\0';
        if(*cursor == ',') cursor++;

        if(token[0] != '\0' && guitar_rx_riff_parse_note(&out->notes[out->count], token)) {
            out->count++;
        }
    }

    return out->count > 0;
}

static bool guitar_rx_decode_preamble(const LevelDuration* data, size_t count, uint16_t* code_out) {
    furi_check(code_out);

    size_t idx = 0;
    uint8_t sync_pairs = 0;

    while(idx < count && sync_pairs < 8) {
        if(!level_duration_get_level(data[idx])) return false;
        if(!guitar_rx_duration_near(level_duration_get_duration(data[idx]), GUITAR_RX_PREAMBLE_TE_SHORT, 35))
            return false;
        idx++;
        if(idx >= count || level_duration_get_level(data[idx])) return false;
        if(!guitar_rx_duration_near(level_duration_get_duration(data[idx]), GUITAR_RX_PREAMBLE_TE_SHORT, 35))
            return false;
        idx++;
        sync_pairs++;
    }

    uint16_t code = 0;
    for(int bit = 15; bit >= 0; bit--) {
        if(idx >= count || !level_duration_get_level(data[idx])) return false;
        const uint32_t high = level_duration_get_duration(data[idx]);
        const bool bit_val = guitar_rx_duration_near(high, GUITAR_RX_PREAMBLE_TE_LONG, 35);
        code = (uint16_t)((code << 1) | (bit_val ? 1 : 0));
        idx++;

        if(idx >= count || level_duration_get_level(data[idx])) return false;
        if(!guitar_rx_duration_near(level_duration_get_duration(data[idx]), GUITAR_RX_PREAMBLE_TE_SHORT, 35))
            return false;
        idx++;
    }

    *code_out = code;
    return true;
}

static size_t guitar_rx_skip_gap(const LevelDuration* data, size_t count, size_t start) {
    uint32_t gap = 0;
    size_t idx = start;
    while(idx < count && gap < 5000) {
        if(level_duration_get_level(data[idx])) break;
        gap += level_duration_get_duration(data[idx]);
        idx++;
    }
    return idx;
}

static size_t guitar_rx_find_pluck_end(const LevelDuration* data, size_t count, size_t start) {
    size_t idx = start;
    uint32_t quiet = 0;

    while(idx < count) {
        if(level_duration_get_level(data[idx])) {
            quiet = 0;
        } else {
            quiet += level_duration_get_duration(data[idx]);
            if(quiet >= GUITAR_RX_PLUCK_GAP_US) return idx;
        }
        idx++;
    }
    return count;
}

static float guitar_rx_estimate_freq(
    const LevelDuration* data,
    size_t start,
    size_t end,
    float* bend_out) {
    uint32_t rise_times[48];
    uint8_t rise_count = 0;
    uint32_t t = 0;
    bool prev = false;

    for(size_t i = 0; i < end; i++) {
        const bool level = level_duration_get_level(data[i]);
        const uint32_t dur = level_duration_get_duration(data[i]);

        if(i >= start) {
            if(level && !prev) {
                if(rise_count < COUNT_OF(rise_times)) rise_times[rise_count++] = t;
            }
        }

        t += dur;
        prev = level;
    }

    if(bend_out) *bend_out = 0.0f;
    if(rise_count < 4) return 0.0f;

    float intervals[32];
    uint8_t interval_count = 0;
    for(uint8_t i = 1; i < rise_count; i++) {
        const uint32_t dt = rise_times[i] - rise_times[i - 1];
        if(dt >= 1500 && dt <= 12000) {
            intervals[interval_count++] = (float)dt / 1000000.0f;
            if(interval_count >= COUNT_OF(intervals)) break;
        }
    }

    if(interval_count < 2) return 0.0f;

    float sum = 0.0f;
    for(uint8_t i = 0; i < interval_count; i++) sum += intervals[i];
    const float avg = sum / (float)interval_count;
    const float freq = 1.0f / avg;

    if(bend_out && interval_count >= 4) {
        float first = 0.0f;
        float last = 0.0f;
        uint8_t half = interval_count / 2;
        for(uint8_t i = 0; i < half; i++) first += intervals[i];
        for(uint8_t i = interval_count - half; i < interval_count; i++) last += intervals[i];
        first /= (float)half;
        last /= (float)half;
        if(first > 0.0001f && last > 0.0001f) {
            const float f0 = 1.0f / first;
            const float f1 = 1.0f / last;
            if(f1 > f0) *bend_out = f1 - f0;
        }
    }

    return freq;
}

static uint16_t guitar_rx_unmask_code(uint16_t encoded) {
    return encoded ^ GUITAR_RX_CODE_XOR;
}

static uint16_t guitar_rx_decode_counter(uint16_t encoded) {
    return guitar_rx_unmask_code(encoded) & 0x0FFF;
}

GuitarRxAction guitar_rx_decode_action(uint16_t encoded) {
    const uint16_t unmasked = guitar_rx_unmask_code(encoded);
    if(((unmasked >> 12) & 0x3) != GUITAR_RX_CODE_VERSION) {
        return GuitarRxActionCount;
    }

    const uint8_t action = (unmasked >> 14) & 0x3;
    if(action >= GuitarRxActionCount) return GuitarRxActionCount;
    return (GuitarRxAction)action;
}

static bool guitar_rx_decode_capture(const LevelDuration* data, size_t count, GuitarRxFrame* frame) {
    furi_check(data);
    furi_check(frame);

    memset(frame, 0, sizeof(GuitarRxFrame));

    if(!guitar_rx_decode_preamble(data, count, &frame->rolling_code)) {
        return false;
    }

    const uint16_t encoded = frame->rolling_code;
    frame->rolling_code = guitar_rx_decode_counter(encoded);
    frame->action = guitar_rx_decode_action(encoded);
    frame->action_valid = frame->action < GuitarRxActionCount;

    size_t idx = 0;
    uint8_t sync_pairs = 0;
    while(idx < count && sync_pairs < 8) {
        idx += 2;
        sync_pairs++;
    }
    idx += 16 * 2;
    idx = guitar_rx_skip_gap(data, count, idx);

    while(idx < count && frame->riff.count < GUITAR_RX_RIFF_MAX_NOTES) {
        while(idx < count && !level_duration_get_level(data[idx]))
            idx++;
        if(idx >= count) break;

        const size_t pluck_end = guitar_rx_find_pluck_end(data, count, idx);
        float bend = 0.0f;
        const float hz = guitar_rx_estimate_freq(data, idx, pluck_end, &bend);
        if(hz >= 80.0f && hz <= 900.0f) {
            frame->riff.notes[frame->riff.count].hz = hz;
            frame->riff.notes[frame->riff.count].bend_hz = (bend >= 3.0f) ? bend : 0.0f;
            frame->riff.count++;
        }

        idx = pluck_end;
        if(pluck_end >= count) break;
    }

    frame->valid = frame->riff.count > 0;
    return frame->valid;
}

void guitar_rx_decoder_reset(void) {
    g_capture_count = 0;
    g_silence_us = 0;
    g_had_activity = false;
    memset(&g_frame, 0, sizeof(g_frame));
}

bool guitar_rx_decoder_feed(bool level, uint32_t duration_us) {
    if(g_capture_count < GUITAR_RX_MAX_CAPTURE) {
        g_capture[g_capture_count++] = level_duration_make(level, duration_us);
    }

    if(level || duration_us < 5000) {
        g_had_activity = true;
        g_silence_us = 0;
    } else {
        g_silence_us += duration_us;
    }

    if(g_had_activity && g_silence_us >= GUITAR_RX_PACKET_SILENCE_US && g_capture_count > 32) {
        guitar_rx_decode_capture(g_capture, g_capture_count, &g_frame);
        g_capture_count = 0;
        g_silence_us = 0;
        g_had_activity = false;
        return g_frame.valid;
    }

    if(g_capture_count >= GUITAR_RX_MAX_CAPTURE) {
        guitar_rx_decoder_reset();
    }

    return false;
}

const GuitarRxFrame* guitar_rx_decoder_get_frame(void) {
    return &g_frame;
}

float guitar_rx_riff_match_score(const GuitarRxRiff* received, const char* profile_str) {
    furi_check(received);
    furi_check(profile_str);

    GuitarRxRiff profile;
    if(!guitar_rx_riff_parse(&profile, profile_str)) return 0.0f;
    if(profile.count == 0 || received->count == 0) return 0.0f;
    if(profile.count != received->count) return 0.0f;

    float score = 0.0f;
    for(uint8_t i = 0; i < profile.count; i++) {
        const float dh = fabsf(received->notes[i].hz - profile.notes[i].hz);
        const float db = fabsf(received->notes[i].bend_hz - profile.notes[i].bend_hz);
        if(dh > 25.0f) return 0.0f;
        score += 1.0f - (dh / 25.0f);
        if(db > 15.0f) score -= 0.2f;
    }

    return score / (float)profile.count;
}

GuitarRxAction guitar_rx_match_action(const GuitarRxRiff* received, const GuitarRxSettings* settings) {
    furi_check(received);
    furi_check(settings);

    float best = GUITAR_RX_MATCH_THRESHOLD;
    GuitarRxAction best_action = GuitarRxActionCount;

    for(GuitarRxAction action = 0; action < GuitarRxActionCount; action++) {
        const char* profile = guitar_rx_profile_for_action(settings, action);
        const float score = guitar_rx_riff_match_score(received, profile);
        if(score > best) {
            best = score;
            best_action = action;
        }
    }

    return best_action;
}

GuitarRxAction guitar_rx_guess_from_notes(const GuitarRxRiff* received) {
    furi_check(received);

    if(received->count == 0) return GuitarRxActionCount;
    if(received->count >= 3) return GuitarRxActionAlarm;

    const float hz = received->notes[0].hz;
    const float bend = received->notes[0].bend_hz;
    if(bend >= 3.0f) return GuitarRxActionTrunk;
    if(hz < 220.0f) return GuitarRxActionUnlock;
    if(hz < 290.0f) return GuitarRxActionLock;
    return GuitarRxActionTrunk;
}

GuitarRxAction guitar_rx_identify_button(
    const GuitarRxFrame* frame,
    const GuitarRxSettings* settings) {
    furi_check(frame);
    furi_check(settings);

    if(frame->action_valid) return frame->action;

    const GuitarRxAction from_profile = guitar_rx_match_action(&frame->riff, settings);
    if(from_profile < GuitarRxActionCount) return from_profile;

    return guitar_rx_guess_from_notes(&frame->riff);
}

const char* guitar_rx_action_button(GuitarRxAction action) {
    switch(action) {
    case GuitarRxActionUnlock:
        return "Unlock (G3)";
    case GuitarRxActionLock:
        return "Lock (B3)";
    case GuitarRxActionTrunk:
        return "Trunk (E4)";
    case GuitarRxActionAlarm:
        return "Alarm Riff";
    default:
        return "Unknown";
    }
}

const char* guitar_rx_action_name(GuitarRxAction action) {
    switch(action) {
    case GuitarRxActionUnlock:
        return "Unlock";
    case GuitarRxActionLock:
        return "Lock";
    case GuitarRxActionTrunk:
        return "Trunk";
    case GuitarRxActionAlarm:
        return "Alarm";
    default:
        return "Unknown";
    }
}

const char* guitar_rx_profile_for_action(const GuitarRxSettings* settings, GuitarRxAction action) {
    furi_check(settings);
    switch(action) {
    case GuitarRxActionUnlock:
        return settings->riff_unlock;
    case GuitarRxActionLock:
        return settings->riff_lock;
    case GuitarRxActionTrunk:
        return settings->riff_trunk;
    case GuitarRxActionAlarm:
        return settings->riff_alarm;
    default:
        return settings->riff_unlock;
    }
}

char* guitar_rx_profile_mut_for_action(GuitarRxSettings* settings, GuitarRxAction action) {
    furi_check(settings);
    switch(action) {
    case GuitarRxActionUnlock:
        return settings->riff_unlock;
    case GuitarRxActionLock:
        return settings->riff_lock;
    case GuitarRxActionTrunk:
        return settings->riff_trunk;
    case GuitarRxActionAlarm:
        return settings->riff_alarm;
    default:
        return settings->riff_unlock;
    }
}
