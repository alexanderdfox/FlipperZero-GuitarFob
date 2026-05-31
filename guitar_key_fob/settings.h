#pragma once

#include "guitar_types.h"

/** Apply defaults for a fresh settings struct. */
void guitar_settings_defaults(GuitarSettings* settings);

/** Load settings from SD card (creates defaults if missing). */
void guitar_settings_load(GuitarSettings* settings);

/** Persist settings to SD card. */
void guitar_settings_save(const GuitarSettings* settings);

/** Restore factory defaults and save to SD card. */
void guitar_settings_reset(GuitarSettings* settings);

/** Map carrier band enum to Hz. */
uint32_t guitar_settings_carrier_hz(GuitarCarrierBand band);
