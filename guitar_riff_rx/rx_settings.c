#include "rx_settings.h"

#include "rx_decoder.h"

#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define TAG "GuitarRxSet"
#define GUITAR_RX_SETTINGS_PATH EXT_PATH("apps_data/guitar_riff_rx/settings.txt")

void guitar_rx_settings_defaults(GuitarRxSettings* settings) {
    furi_check(settings);

    settings->carrier_hz = GUITAR_RX_CARRIER_US_HZ;
    settings->speaker_enabled = true;

    strncpy(settings->riff_unlock, "196", GUITAR_RX_RIFF_STR_LEN - 1);
    strncpy(settings->riff_lock, "247", GUITAR_RX_RIFF_STR_LEN - 1);
    strncpy(settings->riff_trunk, "330+8", GUITAR_RX_RIFF_STR_LEN - 1);
    strncpy(settings->riff_alarm, "196,247,330,196", GUITAR_RX_RIFF_STR_LEN - 1);

    settings->riff_unlock[GUITAR_RX_RIFF_STR_LEN - 1] = '\0';
    settings->riff_lock[GUITAR_RX_RIFF_STR_LEN - 1] = '\0';
    settings->riff_trunk[GUITAR_RX_RIFF_STR_LEN - 1] = '\0';
    settings->riff_alarm[GUITAR_RX_RIFF_STR_LEN - 1] = '\0';
}

void guitar_rx_settings_sync_profile(
    GuitarRxSettings* settings,
    GuitarRxAction action,
    const char* riff_str) {
    furi_check(settings);
    furi_check(riff_str);

    char* dest = guitar_rx_profile_mut_for_action(settings, action);
    strncpy(dest, riff_str, GUITAR_RX_RIFF_STR_LEN - 1);
    dest[GUITAR_RX_RIFF_STR_LEN - 1] = '\0';
}

void guitar_rx_settings_load(GuitarRxSettings* settings) {
    furi_check(settings);
    guitar_rx_settings_defaults(settings);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file = flipper_format_file_alloc(storage);

    if(!flipper_format_file_open_existing(file, GUITAR_RX_SETTINGS_PATH)) {
        flipper_format_free(file);
        furi_record_close(RECORD_STORAGE);
        guitar_rx_settings_save(settings);
        return;
    }

    uint32_t value = 0;
    if(flipper_format_read_uint32(file, "CarrierHz", &value, 1)) settings->carrier_hz = value;

    uint32_t speaker = 1;
    if(flipper_format_read_uint32(file, "Speaker", &speaker, 1)) {
        settings->speaker_enabled = speaker != 0;
    }

    FuriString* riff = furi_string_alloc();
    if(flipper_format_read_string(file, "RiffUnlock", riff)) {
        strncpy(settings->riff_unlock, furi_string_get_cstr(riff), GUITAR_RX_RIFF_STR_LEN - 1);
    }
    if(flipper_format_read_string(file, "RiffLock", riff)) {
        strncpy(settings->riff_lock, furi_string_get_cstr(riff), GUITAR_RX_RIFF_STR_LEN - 1);
    }
    if(flipper_format_read_string(file, "RiffTrunk", riff)) {
        strncpy(settings->riff_trunk, furi_string_get_cstr(riff), GUITAR_RX_RIFF_STR_LEN - 1);
    }
    if(flipper_format_read_string(file, "RiffAlarm", riff)) {
        strncpy(settings->riff_alarm, furi_string_get_cstr(riff), GUITAR_RX_RIFF_STR_LEN - 1);
    }
    furi_string_free(riff);

    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
}

void guitar_rx_settings_save(const GuitarRxSettings* settings) {
    furi_check(settings);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(storage, EXT_PATH("apps_data/guitar_riff_rx"));

    FlipperFormat* file = flipper_format_file_alloc(storage);
    if(!flipper_format_file_open_always(file, GUITAR_RX_SETTINGS_PATH)) {
        flipper_format_free(file);
        furi_record_close(RECORD_STORAGE);
        return;
    }

    uint32_t carrier = settings->carrier_hz;
    uint32_t speaker = settings->speaker_enabled ? 1 : 0;
    flipper_format_write_uint32(file, "CarrierHz", &carrier, 1);
    flipper_format_write_uint32(file, "Speaker", &speaker, 1);
    flipper_format_write_string_cstr(file, "RiffUnlock", settings->riff_unlock);
    flipper_format_write_string_cstr(file, "RiffLock", settings->riff_lock);
    flipper_format_write_string_cstr(file, "RiffTrunk", settings->riff_trunk);
    flipper_format_write_string_cstr(file, "RiffAlarm", settings->riff_alarm);

    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
}
