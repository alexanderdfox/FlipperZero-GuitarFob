/**
 * @file guitar_riff_rx.c
 * @brief Guitar Key Fob receiver — decode and sync riffs over Sub-GHz.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026, Alexander Fox
 */

#include "rx_types.h"
#include "rx_decoder.h"
#include "rx_settings.h"

#include <furi.h>
#include <furi_hal_power.h>
#include <furi_hal_speaker.h>
#include <furi_hal_subghz.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>

#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>

#define TAG "GuitarRiffRx"

#define GUITAR_RX_EVENT_FRAME 100

typedef enum {
    GuitarRxViewMenu,
    GuitarRxViewListen,
    GuitarRxViewSync,
    GuitarRxViewSettings,
    GuitarRxViewCount,
} GuitarRxView;

typedef enum {
    GuitarRxMenuListen,
    GuitarRxMenuSync,
    GuitarRxMenuSettings,
} GuitarRxMenuItem;

typedef enum {
    GuitarRxSyncUnlock,
    GuitarRxSyncLock,
    GuitarRxSyncTrunk,
    GuitarRxSyncAlarm,
} GuitarRxSyncItem;

typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* menu;
    Submenu* sync_menu;
    VariableItemList* settings_list;
    Widget* widget;
    NotificationApp* notifications;
    GuitarRxSettings settings;
    FuriString* status_text;
    char last_riff_str[GUITAR_RX_RIFF_STR_LEN];
    uint16_t last_code;
    GuitarRxAction last_match;
    bool has_frame;
    bool listening;
    uint32_t active_view;
} GuitarRiffRxApp;

static void guitar_rx_update_widget(GuitarRiffRxApp* app) {
    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget, 0, 0, 128, 64, furi_string_get_cstr(app->status_text));
}

static void guitar_rx_play_riff(const GuitarRxRiff* riff) {
    if(!furi_hal_speaker_acquire(500)) return;

    for(uint8_t n = 0; n < riff->count; n++) {
        const float hz = riff->notes[n].hz;
        const float bend = riff->notes[n].bend_hz;
        const uint16_t steps = 40;

        for(uint16_t s = 0; s < steps; s++) {
            const float t = (float)s / (float)steps;
            float freq = hz;
            if(bend > 0.0f) freq += bend * t;
            const float vol = 0.55f * (1.0f - t * 0.85f);
            furi_hal_speaker_start(freq, vol);
            furi_delay_ms(8);
        }
        furi_hal_speaker_stop();
        furi_delay_ms(60);
    }

    furi_hal_speaker_release();
}

static void guitar_rx_on_frame(GuitarRiffRxApp* app, const GuitarRxFrame* frame) {
    guitar_rx_riff_format(&frame->riff, app->last_riff_str, sizeof(app->last_riff_str));
    app->last_code = frame->rolling_code;
    app->last_match = guitar_rx_match_action(&frame->riff, &app->settings);
    app->has_frame = true;

    if(app->settings.speaker_enabled) {
        guitar_rx_play_riff(&frame->riff);
    }

    if(app->last_match < GuitarRxActionCount) {
        furi_string_printf(
            app->status_text,
            "Riff received!\n\nNotes: %s\nCode: %u\nMatch: %s\n\nOK=Sync menu",
            app->last_riff_str,
            app->last_code,
            guitar_rx_action_name(app->last_match));
        notification_message(app->notifications, &sequence_success);
    } else {
        furi_string_printf(
            app->status_text,
            "Riff received!\n\nNotes: %s\nCode: %u\nMatch: none\n\nOK=Sync menu",
            app->last_riff_str,
            app->last_code);
        notification_message(app->notifications, &sequence_single_vibro);
    }

    guitar_rx_update_widget(app);
}

static void guitar_rx_capture_callback(bool level, uint32_t duration, void* context) {
    GuitarRiffRxApp* app = context;
    if(guitar_rx_decoder_feed(level, duration)) {
        view_dispatcher_send_custom_event(app->view_dispatcher, GUITAR_RX_EVENT_FRAME);
    }
}

static bool guitar_rx_radio_start(GuitarRiffRxApp* app) {
    if(!furi_hal_subghz_is_frequency_valid(app->settings.carrier_hz)) return false;

    subghz_devices_init();
    const SubGhzDevice* device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    furi_check(device);

    subghz_devices_begin(device);
    subghz_devices_reset(device);
    subghz_devices_load_preset(device, FuriHalSubGhzPresetOok650Async, NULL);
    subghz_devices_set_frequency(device, app->settings.carrier_hz);
    subghz_devices_flush_rx(device);
    subghz_devices_set_rx(device);

    furi_hal_power_suppress_charge_enter();
    guitar_rx_decoder_reset();
    subghz_devices_start_async_rx(device, guitar_rx_capture_callback, app);
    app->listening = true;
    return true;
}

static void guitar_rx_radio_stop(GuitarRiffRxApp* app) {
    if(!app->listening) return;

    const SubGhzDevice* device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(device) {
        subghz_devices_stop_async_rx(device);
        subghz_devices_sleep(device);
        subghz_devices_end(device);
    }
    subghz_devices_deinit();
    furi_hal_power_suppress_charge_exit();
    app->listening = false;
}

static void guitar_rx_switch_view(GuitarRiffRxApp* app, uint32_t view) {
    app->active_view = view;
    view_dispatcher_switch_to_view(app->view_dispatcher, view);
}

static void guitar_rx_menu_callback(void* context, uint32_t index) {
    GuitarRiffRxApp* app = context;
    furi_assert(app);

    switch(index) {
    case GuitarRxMenuListen:
        furi_string_printf(
            app->status_text,
            "Listening...\n%lu MHz\n\nPress TX on\nother Flipper",
            app->settings.carrier_hz / 1000000);
        guitar_rx_update_widget(app);
        if(guitar_rx_radio_start(app)) {
            guitar_rx_switch_view(app, GuitarRxViewListen);
        } else {
            furi_string_set_str(app->status_text, "Frequency blocked\nin this region");
            guitar_rx_update_widget(app);
            guitar_rx_switch_view(app, GuitarRxViewListen);
        }
        break;
    case GuitarRxMenuSync:
        if(!app->has_frame) {
            furi_string_set_str(app->status_text, "No riff yet.\nListen first.");
            guitar_rx_update_widget(app);
            guitar_rx_switch_view(app, GuitarRxViewListen);
            return;
        }
        guitar_rx_switch_view(app, GuitarRxViewSync);
        break;
    case GuitarRxMenuSettings:
        guitar_rx_switch_view(app, GuitarRxViewSettings);
        break;
    default:
        break;
    }
}

static void guitar_rx_sync_callback(void* context, uint32_t index) {
    GuitarRiffRxApp* app = context;
    furi_assert(app);

    GuitarRxAction action = GuitarRxActionUnlock;
    switch(index) {
    case GuitarRxSyncLock:
        action = GuitarRxActionLock;
        break;
    case GuitarRxSyncTrunk:
        action = GuitarRxActionTrunk;
        break;
    case GuitarRxSyncAlarm:
        action = GuitarRxActionAlarm;
        break;
    case GuitarRxSyncUnlock:
    default:
        action = GuitarRxActionUnlock;
        break;
    }

    guitar_rx_settings_sync_profile(&app->settings, action, app->last_riff_str);
    guitar_rx_settings_save(&app->settings);

    furi_string_printf(
        app->status_text,
        "Synced riff to\n%s profile:\n%s",
        guitar_rx_action_name(action),
        app->last_riff_str);
    guitar_rx_update_widget(app);
    guitar_rx_switch_view(app, GuitarRxViewListen);
    notification_message(app->notifications, &sequence_success);
}

static void guitar_rx_setting_change_carrier(VariableItem* item) {
    GuitarRiffRxApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.carrier_hz = (index == 1) ? GUITAR_RX_CARRIER_EU_HZ : GUITAR_RX_CARRIER_US_HZ;
    variable_item_set_current_value_text(item, index == 1 ? "433.92 MHz" : "315 MHz");
}

static void guitar_rx_setting_change_speaker(VariableItem* item) {
    GuitarRiffRxApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.speaker_enabled = index == 1;
    variable_item_set_current_value_text(item, app->settings.speaker_enabled ? "On" : "Off");
}

static void guitar_rx_settings_populate(GuitarRiffRxApp* app) {
    variable_item_list_reset(app->settings_list);

    VariableItem* item = variable_item_list_add(
        app->settings_list, "Carrier", 2, guitar_rx_setting_change_carrier, app);
    variable_item_set_current_value_index(
        item, app->settings.carrier_hz == GUITAR_RX_CARRIER_EU_HZ ? 1 : 0);
    variable_item_set_current_value_text(
        item, app->settings.carrier_hz == GUITAR_RX_CARRIER_EU_HZ ? "433.92 MHz" : "315 MHz");

    item = variable_item_list_add(
        app->settings_list, "Speaker", 2, guitar_rx_setting_change_speaker, app);
    variable_item_set_current_value_index(item, app->settings.speaker_enabled ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.speaker_enabled ? "On" : "Off");
}

static void guitar_rx_sync_menu_populate(GuitarRiffRxApp* app) {
    submenu_reset(app->sync_menu);

    char label[64];
    for(GuitarRxAction action = 0; action < GuitarRxActionCount; action++) {
        snprintf(
            label,
            sizeof(label),
            "Sync to %s",
            guitar_rx_action_name(action));
        submenu_add_item(app->sync_menu, label, action, guitar_rx_sync_callback, app);
    }

    UNUSED(app);
}

static bool guitar_rx_custom_event(void* context, uint32_t event) {
    GuitarRiffRxApp* app = context;
    if(event == GUITAR_RX_EVENT_FRAME) {
        const GuitarRxFrame* frame = guitar_rx_decoder_get_frame();
        if(frame && frame->valid) {
            guitar_rx_on_frame(app, frame);
        }
        return true;
    }
    return false;
}

static bool guitar_rx_navigation(void* context) {
    GuitarRiffRxApp* app = context;

    if(app->active_view == GuitarRxViewSync) {
        guitar_rx_switch_view(app, GuitarRxViewListen);
        return true;
    }

    if(app->active_view == GuitarRxViewSettings) {
        guitar_rx_settings_save(&app->settings);
        guitar_rx_switch_view(app, GuitarRxViewMenu);
        return true;
    }

    if(app->active_view == GuitarRxViewListen) {
        guitar_rx_radio_stop(app);
        guitar_rx_switch_view(app, GuitarRxViewMenu);
        return true;
    }

    guitar_rx_radio_stop(app);
    guitar_rx_settings_save(&app->settings);
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static GuitarRiffRxApp* guitar_rx_app_alloc(void) {
    GuitarRiffRxApp* app = malloc(sizeof(GuitarRiffRxApp));
    memset(app, 0, sizeof(GuitarRiffRxApp));

    app->view_dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->sync_menu = submenu_alloc();
    app->settings_list = variable_item_list_alloc();
    app->widget = widget_alloc();
    app->status_text = furi_string_alloc();
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->last_match = GuitarRxActionCount;

    guitar_rx_settings_load(&app->settings);

    submenu_add_item(app->menu, "Listen", GuitarRxMenuListen, guitar_rx_menu_callback, app);
    submenu_add_item(app->menu, "Sync Riff", GuitarRxMenuSync, guitar_rx_menu_callback, app);
    submenu_add_item(app->menu, "Settings", GuitarRxMenuSettings, guitar_rx_menu_callback, app);

    guitar_rx_settings_populate(app);
    guitar_rx_sync_menu_populate(app);

    furi_string_set_str(app->status_text, "Guitar Riff RX\n\nSelect Listen");

    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, guitar_rx_custom_event);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, guitar_rx_navigation);

    view_dispatcher_add_view(app->view_dispatcher, GuitarRxViewMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarRxViewListen, widget_get_view(app->widget));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarRxViewSync, submenu_get_view(app->sync_menu));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarRxViewSettings, variable_item_list_get_view(app->settings_list));

    return app;
}

static void guitar_rx_app_free(GuitarRiffRxApp* app) {
    guitar_rx_radio_stop(app);
    guitar_rx_settings_save(&app->settings);

    view_dispatcher_remove_view(app->view_dispatcher, GuitarRxViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarRxViewListen);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarRxViewSync);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarRxViewSettings);

    view_dispatcher_free(app->view_dispatcher);
    submenu_free(app->menu);
    submenu_free(app->sync_menu);
    variable_item_list_free(app->settings_list);
    widget_free(app->widget);
    furi_string_free(app->status_text);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t guitar_riff_rx_app(void* arg) {
    UNUSED(arg);

    GuitarRiffRxApp* app = guitar_rx_app_alloc();
    app->active_view = GuitarRxViewMenu;
    guitar_rx_switch_view(app, GuitarRxViewMenu);
    view_dispatcher_run(app->view_dispatcher);
    guitar_rx_app_free(app);
    return 0;
}
