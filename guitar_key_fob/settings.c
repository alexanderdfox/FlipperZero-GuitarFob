#include "settings.h"

#include "riff.h"

#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define TAG "GuitarSettings"
#define GUITAR_SETTINGS_PATH EXT_PATH("apps_data/guitar_key_fob/settings.txt")

void guitar_settings_defaults(GuitarSettings* settings) {
    furi_check(settings);

    settings->carrier_hz = GUITAR_CARRIER_US_HZ;
    settings->attack_ms = GUITAR_DEFAULT_ATTACK_MS;
    settings->decay_ms = GUITAR_DEFAULT_DECAY_MS;
    settings->harmonic2_pct = GUITAR_DEFAULT_HARMONIC2_PCT;
    settings->harmonic3_pct = GUITAR_DEFAULT_HARMONIC3_PCT;
    settings->vibrato_depth_pct = GUITAR_DEFAULT_VIBRATO_DEPTH_PCT;
    settings->vibrato_hz = GUITAR_DEFAULT_VIBRATO_HZ;
    settings->rolling_code = 1;
    settings->speaker_enabled = true;
    guitar_riff_defaults(settings);
}

uint32_t guitar_settings_carrier_hz(GuitarCarrierBand band) {
    return (band == GuitarCarrierEU) ? GUITAR_CARRIER_EU_HZ : GUITAR_CARRIER_US_HZ;
}

void guitar_settings_load(GuitarSettings* settings) {
    furi_check(settings);
    guitar_settings_defaults(settings);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file = flipper_format_file_alloc(storage);

    if(!flipper_format_file_open_existing(file, GUITAR_SETTINGS_PATH)) {
        FURI_LOG_I(TAG, "No settings file, using defaults");
        flipper_format_free(file);
        furi_record_close(RECORD_STORAGE);
        guitar_settings_save(settings);
        return;
    }

    uint32_t value = 0;
    if(flipper_format_read_uint32(file, "CarrierHz", &value, 1)) settings->carrier_hz = value;
    if(flipper_format_read_uint32(file, "AttackMs", &value, 1)) settings->attack_ms = (uint16_t)value;
    if(flipper_format_read_uint32(file, "DecayMs", &value, 1)) settings->decay_ms = (uint16_t)value;
    if(flipper_format_read_uint32(file, "Harm2Pct", &value, 1))
        settings->harmonic2_pct = (uint8_t)value;
    if(flipper_format_read_uint32(file, "Harm3Pct", &value, 1))
        settings->harmonic3_pct = (uint8_t)value;
    if(flipper_format_read_uint32(file, "VibratoPct", &value, 1))
        settings->vibrato_depth_pct = (uint8_t)value;
    if(flipper_format_read_uint32(file, "VibratoHz", &value, 1)) settings->vibrato_hz = (uint8_t)value;
    if(flipper_format_read_uint32(file, "RollingCode", &value, 1)) settings->rolling_code = value;

    uint32_t speaker = 1;
    if(flipper_format_read_uint32(file, "Speaker", &speaker, 1)) {
        settings->speaker_enabled = speaker != 0;
    }

    FuriString* riff_str = furi_string_alloc();
    if(flipper_format_read_string(file, "RiffUnlock", riff_str)) {
        strncpy(settings->riff_unlock, furi_string_get_cstr(riff_str), GUITAR_RIFF_STR_LEN - 1);
    }
    if(flipper_format_read_string(file, "RiffLock", riff_str)) {
        strncpy(settings->riff_lock, furi_string_get_cstr(riff_str), GUITAR_RIFF_STR_LEN - 1);
    }
    if(flipper_format_read_string(file, "RiffTrunk", riff_str)) {
        strncpy(settings->riff_trunk, furi_string_get_cstr(riff_str), GUITAR_RIFF_STR_LEN - 1);
    }
    if(flipper_format_read_string(file, "RiffAlarm", riff_str)) {
        strncpy(settings->riff_alarm, furi_string_get_cstr(riff_str), GUITAR_RIFF_STR_LEN - 1);
    }
    furi_string_free(riff_str);

    settings->riff_unlock[GUITAR_RIFF_STR_LEN - 1] = '\0';
    settings->riff_lock[GUITAR_RIFF_STR_LEN - 1] = '\0';
    settings->riff_trunk[GUITAR_RIFF_STR_LEN - 1] = '\0';
    settings->riff_alarm[GUITAR_RIFF_STR_LEN - 1] = '\0';

    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
}

void guitar_settings_save(const GuitarSettings* settings) {
    furi_check(settings);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(storage, EXT_PATH("apps_data/guitar_key_fob"));

    FlipperFormat* file = flipper_format_file_alloc(storage);
    if(!flipper_format_file_open_always(file, GUITAR_SETTINGS_PATH)) {
        FURI_LOG_E(TAG, "Failed to open settings for write");
        flipper_format_free(file);
        furi_record_close(RECORD_STORAGE);
        return;
    }

    flipper_format_write_uint32(file, "CarrierHz", &settings->carrier_hz, 1);
    uint32_t attack = settings->attack_ms;
    uint32_t decay = settings->decay_ms;
    uint32_t h2 = settings->harmonic2_pct;
    uint32_t h3 = settings->harmonic3_pct;
    uint32_t vib_pct = settings->vibrato_depth_pct;
    uint32_t vib_hz = settings->vibrato_hz;
    uint32_t rolling = settings->rolling_code;
    uint32_t speaker = settings->speaker_enabled ? 1 : 0;

    flipper_format_write_uint32(file, "AttackMs", &attack, 1);
    flipper_format_write_uint32(file, "DecayMs", &decay, 1);
    flipper_format_write_uint32(file, "Harm2Pct", &h2, 1);
    flipper_format_write_uint32(file, "Harm3Pct", &h3, 1);
    flipper_format_write_uint32(file, "VibratoPct", &vib_pct, 1);
    flipper_format_write_uint32(file, "VibratoHz", &vib_hz, 1);
    flipper_format_write_uint32(file, "RollingCode", &rolling, 1);
    flipper_format_write_uint32(file, "Speaker", &speaker, 1);

    flipper_format_write_string_cstr(file, "RiffUnlock", settings->riff_unlock);
    flipper_format_write_string_cstr(file, "RiffLock", settings->riff_lock);
    flipper_format_write_string_cstr(file, "RiffTrunk", settings->riff_trunk);
    flipper_format_write_string_cstr(file, "RiffAlarm", settings->riff_alarm);

    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
}

void guitar_settings_reset(GuitarSettings* settings) {
    furi_check(settings);
    guitar_settings_defaults(settings);
    guitar_settings_save(settings);
}
