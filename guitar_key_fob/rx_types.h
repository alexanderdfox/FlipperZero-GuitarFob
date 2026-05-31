#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define GUITAR_RX_CARRIER_US_HZ 315000000UL
#define GUITAR_RX_CARRIER_EU_HZ 433920000UL

#define GUITAR_RX_RIFF_MAX_NOTES 6
#define GUITAR_RX_RIFF_STR_LEN 48

#define GUITAR_RX_MAX_CAPTURE 8192
#define GUITAR_RX_PREAMBLE_TE_SHORT 350
#define GUITAR_RX_PREAMBLE_TE_LONG 1050
#define GUITAR_RX_PLUCK_GAP_US 60000
#define GUITAR_RX_PACKET_SILENCE_US 120000
#define GUITAR_RX_SAMPLE_US 500

typedef enum {
    GuitarRxActionUnlock,
    GuitarRxActionLock,
    GuitarRxActionTrunk,
    GuitarRxActionAlarm,
    GuitarRxActionCount,
} GuitarRxAction;

typedef struct {
    float hz;
    float bend_hz;
} GuitarRxNote;

typedef struct {
    uint8_t count;
    GuitarRxNote notes[GUITAR_RX_RIFF_MAX_NOTES];
} GuitarRxRiff;

typedef struct {
    uint16_t rolling_code;
    GuitarRxRiff riff;
    GuitarRxAction action;
    bool action_valid;
    bool valid;
} GuitarRxFrame;

typedef struct {
    uint32_t carrier_hz;
    bool speaker_enabled;
    char riff_unlock[GUITAR_RX_RIFF_STR_LEN];
    char riff_lock[GUITAR_RX_RIFF_STR_LEN];
    char riff_trunk[GUITAR_RX_RIFF_STR_LEN];
    char riff_alarm[GUITAR_RX_RIFF_STR_LEN];
} GuitarRxSettings;
