#pragma once

#include "rx_types.h"

void guitar_rx_settings_defaults(GuitarRxSettings* settings);
void guitar_rx_settings_load(GuitarRxSettings* settings);
void guitar_rx_settings_save(const GuitarRxSettings* settings);

/** Restore factory defaults and save to SD card. */
void guitar_rx_settings_reset(GuitarRxSettings* settings);

/** Sync received riff string into a profile slot. */
void guitar_rx_settings_sync_profile(
    GuitarRxSettings* settings,
    GuitarRxAction action,
    const char* riff_str);
