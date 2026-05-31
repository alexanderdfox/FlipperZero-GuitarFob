#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** US ISM band (315 MHz) and EU band (433.92 MHz). */
#define GUITAR_CARRIER_US_HZ 315000000UL
#define GUITAR_CARRIER_EU_HZ 433920000UL

/** Guitar note frequencies used as default riff tones (Hz). */
#define GUITAR_NOTE_UNLOCK_HZ 196.0f /* G3 */
#define GUITAR_NOTE_LOCK_HZ 247.0f /* B3 */
#define GUITAR_NOTE_TRUNK_HZ 330.0f /* E4 */

/** Default envelope and modulation parameters. */
#define GUITAR_DEFAULT_ATTACK_MS 12
#define GUITAR_DEFAULT_DECAY_MS 550
#define GUITAR_DEFAULT_HARMONIC2_PCT 50
#define GUITAR_DEFAULT_HARMONIC3_PCT 25
#define GUITAR_DEFAULT_VIBRATO_DEPTH_PCT 8
#define GUITAR_DEFAULT_VIBRATO_HZ 5

/** Custom riff limits: up to 6 notes, e.g. "196,247,330+8". */
#define GUITAR_RIFF_MAX_NOTES 6
#define GUITAR_RIFF_STR_LEN 48

/** ASK sample period in microseconds (500 us = 2 kHz baseband). */
#define GUITAR_ASK_SAMPLE_US 500

/** Maximum pluck duration and resulting RAW buffer size. */
#define GUITAR_MAX_PLUCK_MS 900
#define GUITAR_MAX_ASK_SAMPLES ((GUITAR_MAX_PLUCK_MS * 1000) / GUITAR_ASK_SAMPLE_US + 64)
#define GUITAR_MAX_RAW_DURATIONS 6144

typedef enum {
    GuitarActionUnlock,
    GuitarActionLock,
    GuitarActionTrunk,
    GuitarActionAlarm,
    GuitarActionCount,
} GuitarAction;

typedef enum {
    GuitarCarrierUS,
    GuitarCarrierEU,
} GuitarCarrierBand;

typedef struct {
    float hz;
    float bend_hz;
} GuitarRiffNote;

typedef struct {
    uint8_t count;
    GuitarRiffNote notes[GUITAR_RIFF_MAX_NOTES];
} GuitarRiff;

typedef struct {
    float base_hz;
    float harmonic2_ratio;
    float harmonic3_ratio;
    float pitch_bend_hz;
    float pitch_bend_duration_s;
    float vibrato_depth;
    float vibrato_hz;
    float attack_s;
    float decay_tau_s;
    float duration_s;
} GuitarPluckParams;

typedef struct {
    uint32_t carrier_hz;
    uint16_t attack_ms;
    uint16_t decay_ms;
    uint8_t harmonic2_pct;
    uint8_t harmonic3_pct;
    uint8_t vibrato_depth_pct;
    uint8_t vibrato_hz;
    uint32_t rolling_code;
    bool speaker_enabled;
    char riff_unlock[GUITAR_RIFF_STR_LEN];
    char riff_lock[GUITAR_RIFF_STR_LEN];
    char riff_trunk[GUITAR_RIFF_STR_LEN];
    char riff_alarm[GUITAR_RIFF_STR_LEN];
} GuitarSettings;
