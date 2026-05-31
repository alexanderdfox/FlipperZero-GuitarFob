#include "riff.h"

#include <furi.h>
#include <stdlib.h>
#include <string.h>

#define TAG "GuitarRiff"

static bool guitar_riff_parse_note(GuitarRiffNote* note, const char* token) {
    furi_check(note);
    furi_check(token);

    while(*token == ' ') token++;
    if(*token == '\0') return false;

    char* end = NULL;
    const float hz = strtof(token, &end);
    if(end == token || hz < 50.0f || hz > 2000.0f) return false;

    note->hz = hz;
    note->bend_hz = 0.0f;

    if(*end == '+') {
        const float bend = strtof(end + 1, &end);
        if(bend > 0.0f && bend <= 100.0f) {
            note->bend_hz = bend;
        }
    }

    return true;
}

void guitar_riff_defaults(GuitarSettings* settings) {
    furi_check(settings);

    strncpy(settings->riff_unlock, "196", GUITAR_RIFF_STR_LEN - 1);
    strncpy(settings->riff_lock, "247", GUITAR_RIFF_STR_LEN - 1);
    strncpy(settings->riff_trunk, "330+8", GUITAR_RIFF_STR_LEN - 1);
    strncpy(settings->riff_alarm, "196,247,330,196", GUITAR_RIFF_STR_LEN - 1);

    settings->riff_unlock[GUITAR_RIFF_STR_LEN - 1] = '\0';
    settings->riff_lock[GUITAR_RIFF_STR_LEN - 1] = '\0';
    settings->riff_trunk[GUITAR_RIFF_STR_LEN - 1] = '\0';
    settings->riff_alarm[GUITAR_RIFF_STR_LEN - 1] = '\0';
}

bool guitar_riff_parse(GuitarRiff* out, const char* str) {
    furi_check(out);
    furi_check(str);

    memset(out, 0, sizeof(GuitarRiff));

    char work[GUITAR_RIFF_STR_LEN];
    strncpy(work, str, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    char* cursor = work;
    while(*cursor && out->count < GUITAR_RIFF_MAX_NOTES) {
        char token[16];
        size_t ti = 0;

        while(*cursor && *cursor != ',' && ti < sizeof(token) - 1) {
            token[ti++] = *cursor++;
        }
        token[ti] = '\0';
        if(*cursor == ',') cursor++;

        if(token[0] != '\0' && guitar_riff_parse_note(&out->notes[out->count], token)) {
            out->count++;
        }
    }

    return out->count > 0;
}

const char* guitar_riff_string_for_action(const GuitarSettings* settings, GuitarAction action) {
    furi_check(settings);

    switch(action) {
    case GuitarActionUnlock:
        return settings->riff_unlock;
    case GuitarActionLock:
        return settings->riff_lock;
    case GuitarActionTrunk:
        return settings->riff_trunk;
    case GuitarActionAlarm:
        return settings->riff_alarm;
    default:
        return settings->riff_unlock;
    }
}

char* guitar_riff_string_mut_for_action(GuitarSettings* settings, GuitarAction action) {
    furi_check(settings);

    switch(action) {
    case GuitarActionUnlock:
        return settings->riff_unlock;
    case GuitarActionLock:
        return settings->riff_lock;
    case GuitarActionTrunk:
        return settings->riff_trunk;
    case GuitarActionAlarm:
        return settings->riff_alarm;
    default:
        return settings->riff_unlock;
    }
}

bool guitar_riff_for_action(GuitarRiff* out, GuitarAction action, const GuitarSettings* settings) {
    furi_check(out);
    furi_check(settings);

    const char* str = guitar_riff_string_for_action(settings, action);
    if(!guitar_riff_parse(out, str)) {
        GuitarSettings defaults;
        guitar_riff_defaults(&defaults);
        return guitar_riff_parse(out, guitar_riff_string_for_action(&defaults, action));
    }
    return true;
}

const char* guitar_action_name(GuitarAction action) {
    switch(action) {
    case GuitarActionUnlock:
        return "Unlock";
    case GuitarActionLock:
        return "Lock";
    case GuitarActionTrunk:
        return "Trunk";
    case GuitarActionAlarm:
        return "Alarm";
    default:
        return "Action";
    }
}

void guitar_riff_preview(
    const GuitarSettings* settings,
    GuitarAction action,
    char* buf,
    size_t buf_len) {
    furi_check(settings);
    furi_check(buf);
    furi_check(buf_len > 0);

    const char* riff = guitar_riff_string_for_action(settings, action);
    const size_t len = strlen(riff);
    if(len >= buf_len) {
        const size_t copy_len = buf_len > 4 ? buf_len - 4 : 0;
        strncpy(buf, riff, copy_len);
        buf[copy_len] = '\0';
        if(buf_len > 3) {
            buf[copy_len] = '.';
            buf[copy_len + 1] = '.';
            buf[copy_len + 2] = '.';
            buf[copy_len + 3] = '\0';
        }
    } else {
        strncpy(buf, riff, buf_len - 1);
        buf[buf_len - 1] = '\0';
    }
}
