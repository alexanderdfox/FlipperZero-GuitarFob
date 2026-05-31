#include "rf_tx.h"

#include "speaker_pluck.h"
#include "waveform.h"

#include <furi.h>
#include <furi_hal_power.h>
#include <furi_hal_subghz.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>

#define TAG "GuitarRfTx"

void guitar_rf_encoder_reset(GuitarRfEncoder* encoder) {
    furi_check(encoder);
    encoder->index = 0;
    encoder->complete = false;
}

void guitar_rf_encoder_clear(GuitarRfEncoder* encoder) {
    furi_check(encoder);
    encoder->count = 0;
    guitar_rf_encoder_reset(encoder);
}

LevelDuration guitar_rf_encoder_yield(void* context) {
    GuitarRfEncoder* encoder = context;
    furi_check(encoder);

    if(encoder->index >= encoder->count) {
        encoder->complete = true;
        return level_duration_reset();
    }

    return encoder->buffer[encoder->index++];
}

bool guitar_rf_encoder_build_pluck(
    GuitarRfEncoder* encoder,
    const GuitarPluckParams* params,
    GuitarSettings* settings) {
    furi_check(encoder);
    furi_check(params);
    furi_check(settings);

    size_t offset = encoder->count;

    if(offset == 0) {
        offset = guitar_build_rolling_preamble(
            encoder->buffer, GUITAR_MAX_RAW_DURATIONS, &settings->rolling_code);
    } else {
        /* Brief silence between riff notes */
        if(offset + 1 < GUITAR_MAX_RAW_DURATIONS) {
            encoder->buffer[offset++] = level_duration_make(false, 80000);
        }
    }

    const size_t pluck_count = guitar_build_pluck_ask(
        encoder->buffer + offset,
        GUITAR_MAX_RAW_DURATIONS - offset,
        params,
        15);

    encoder->count = offset + pluck_count;
    guitar_rf_encoder_reset(encoder);

    return pluck_count > 0;
}

bool guitar_rf_transmit(
    GuitarRfEncoder* encoder,
    GuitarSettings* settings,
    const GuitarRiff* riff) {
    furi_check(encoder);
    furi_check(settings);

    if(encoder->count == 0) return false;

    if(!furi_hal_subghz_is_frequency_valid(settings->carrier_hz)) {
        FURI_LOG_W(TAG, "Frequency %lu blocked in this region", settings->carrier_hz);
        return false;
    }

    subghz_devices_init();
    const SubGhzDevice* device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    furi_check(device);

    subghz_devices_begin(device);
    subghz_devices_reset(device);
    subghz_devices_load_preset(device, FuriHalSubGhzPresetOok650Async, NULL);
    subghz_devices_set_frequency(device, settings->carrier_hz);

    guitar_rf_encoder_reset(encoder);

    furi_hal_power_suppress_charge_enter();

    bool ok = false;
    if(subghz_devices_start_async_tx(device, guitar_rf_encoder_yield, encoder)) {
        const uint32_t start_tick = furi_get_tick();
        const float max_duration_s =
            riff ? (guitar_riff_total_duration_s(riff, settings) + 0.5f) : 2.5f;

        if(settings->speaker_enabled && riff) {
            guitar_speaker_pluck_begin_riff(riff, settings);
        }

        while(!subghz_devices_is_async_complete_tx(device) && !encoder->complete) {
            const float elapsed =
                (float)(furi_get_tick() - start_tick) / 1000.0f;

            if(settings->speaker_enabled && riff) {
                guitar_speaker_pluck_update(elapsed);
            }

            if(elapsed >= max_duration_s) break;

            furi_delay_ms(2);
        }

        if(settings->speaker_enabled) {
            guitar_speaker_pluck_end();
        }

        subghz_devices_stop_async_tx(device);
        ok = encoder->complete;
    } else {
        FURI_LOG_W(TAG, "Async TX refused (region lock?)");
    }

    subghz_devices_sleep(device);
    subghz_devices_end(device);
    subghz_devices_deinit();

    furi_hal_power_suppress_charge_exit();

    guitar_rf_encoder_clear(encoder);
    return ok;
}
