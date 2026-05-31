#include "guitar_app.h"
#include "rx_decoder.h"
#include "rx_settings.h"
#include "rx_ui.h"
#include "guitar_key_fob_icons.h"

#include <furi_hal_power.h>
#include <furi_hal_speaker.h>
#include <furi_hal_subghz.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <notification/notification_messages.h>

#define GUITAR_RX_EVENT_FRAME 100

typedef enum {
    GuitarRxMenuListen,
    GuitarRxMenuSync,
    GuitarRxMenuSettings,
} GuitarRxMenuItem;

static void guitar_rx_refresh_listen(GuitarApp* app);
static void guitar_rx_sync_menu_populate(GuitarApp* app);
static void guitar_rx_settings_populate(GuitarApp* app);

static void guitar_rx_refresh_listen(GuitarApp* app) {
    rx_ui_show_listen(
        app->rx_widget, &I_rx_logo_17x32, app->rx_settings.carrier_hz, app->rx_listen_dots);
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

static void guitar_rx_on_frame(GuitarApp* app, const GuitarRxFrame* frame) {
    guitar_rx_riff_format(&frame->riff, app->rx_last_riff_str, sizeof(app->rx_last_riff_str));
    app->rx_last_code = frame->rolling_code;
    app->rx_last_match = guitar_rx_identify_button(frame, &app->rx_settings);
    app->rx_has_frame = true;

    if(app->rx_settings.speaker_enabled) {
        guitar_rx_play_riff(&frame->riff);
    }

    const bool identified = app->rx_last_match < GuitarRxActionCount;
    furi_string_printf(
        app->rx_status_text,
        "Notes: %s\nCode: %u\nBack -> Sync menu",
        app->rx_last_riff_str,
        app->rx_last_code);

    rx_ui_show_status(
        app->rx_widget,
        &I_rx_logo_17x32,
        identified,
        identified ? guitar_rx_action_button(app->rx_last_match) : "Unknown Button",
        furi_string_get_cstr(app->rx_status_text));

    notification_message(
        app->notifications, identified ? &sequence_success : &sequence_single_vibro);
}

static void guitar_rx_capture_callback(bool level, uint32_t duration, void* context) {
    GuitarApp* app = context;
    if(guitar_rx_decoder_feed(level, duration)) {
        view_dispatcher_send_custom_event(app->view_dispatcher, GUITAR_RX_EVENT_FRAME);
    }
}

static bool guitar_rx_radio_start(GuitarApp* app) {
    if(!furi_hal_subghz_is_frequency_valid(app->rx_settings.carrier_hz)) return false;

    subghz_devices_init();
    const SubGhzDevice* device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    furi_check(device);

    subghz_devices_begin(device);
    subghz_devices_reset(device);
    subghz_devices_load_preset(device, FuriHalSubGhzPresetOok650Async, NULL);
    subghz_devices_set_frequency(device, app->rx_settings.carrier_hz);
    subghz_devices_flush_rx(device);
    subghz_devices_set_rx(device);

    furi_hal_power_suppress_charge_enter();
    guitar_rx_decoder_reset();
    subghz_devices_start_async_rx(device, guitar_rx_capture_callback, app);
    app->rx_listening = true;
    app->rx_listen_dots = 0;
    return true;
}

void guitar_rx_stop_radio(GuitarApp* app) {
    if(!app->rx_listening) return;

    const SubGhzDevice* device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(device) {
        subghz_devices_stop_async_rx(device);
        subghz_devices_sleep(device);
        subghz_devices_end(device);
    }
    subghz_devices_deinit();
    furi_hal_power_suppress_charge_exit();
    app->rx_listening = false;
}

static void guitar_rx_menu_callback(void* context, uint32_t index) {
    GuitarApp* app = context;
    furi_assert(app);

    switch(index) {
    case GuitarRxMenuListen:
        if(guitar_rx_radio_start(app)) {
            guitar_rx_refresh_listen(app);
            guitar_app_switch_view(app, GuitarViewRxListen);
        } else {
            rx_ui_show_status(
                app->rx_widget,
                &I_rx_logo_17x32,
                false,
                "Blocked",
                "Frequency not\nallowed in region");
            guitar_app_switch_view(app, GuitarViewRxListen);
        }
        break;
    case GuitarRxMenuSync:
        if(!app->rx_has_frame) {
            rx_ui_show_banner(
                app->rx_widget, &I_rx_logo_17x32, "No Riff Yet", "Listen first, then sync");
            guitar_app_switch_view(app, GuitarViewRxListen);
            return;
        }
        guitar_app_switch_view(app, GuitarViewRxSync);
        break;
    case GuitarRxMenuSettings:
        guitar_app_switch_view(app, GuitarViewRxSettings);
        break;
    default:
        break;
    }
}

static void guitar_rx_sync_callback(void* context, uint32_t index) {
    GuitarApp* app = context;
    furi_assert(app);

    const GuitarRxAction action = (GuitarRxAction)index;
    guitar_rx_settings_sync_profile(&app->rx_settings, action, app->rx_last_riff_str);
    guitar_rx_settings_save(&app->rx_settings);

    furi_string_printf(
        app->rx_status_text,
        "Profile: %s\nRiff: %s",
        guitar_rx_action_name(action),
        app->rx_last_riff_str);

    rx_ui_show_status(
        app->rx_widget,
        &I_rx_logo_17x32,
        true,
        "Synced",
        furi_string_get_cstr(app->rx_status_text));
    guitar_app_switch_view(app, GuitarViewRxListen);
    notification_message(app->notifications, &sequence_success);
}

static void guitar_rx_setting_change_carrier(VariableItem* item) {
    GuitarApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->rx_settings.carrier_hz = (index == 1) ? GUITAR_RX_CARRIER_EU_HZ : GUITAR_RX_CARRIER_US_HZ;
    variable_item_set_current_value_text(item, index == 1 ? "433.92 MHz" : "315 MHz");
}

static void guitar_rx_setting_change_speaker(VariableItem* item) {
    GuitarApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->rx_settings.speaker_enabled = index == 1;
    variable_item_set_current_value_text(item, app->rx_settings.speaker_enabled ? "On" : "Off");
}

static void guitar_rx_settings_populate(GuitarApp* app) {
    variable_item_list_reset(app->rx_settings_list);

    VariableItem* item = variable_item_list_add(
        app->rx_settings_list, "Carrier", 2, guitar_rx_setting_change_carrier, app);
    variable_item_set_current_value_index(
        item, app->rx_settings.carrier_hz == GUITAR_RX_CARRIER_EU_HZ ? 1 : 0);
    variable_item_set_current_value_text(
        item, app->rx_settings.carrier_hz == GUITAR_RX_CARRIER_EU_HZ ? "433.92 MHz" : "315 MHz");

    item = variable_item_list_add(
        app->rx_settings_list, "Speaker", 2, guitar_rx_setting_change_speaker, app);
    variable_item_set_current_value_index(item, app->rx_settings.speaker_enabled ? 1 : 0);
    variable_item_set_current_value_text(item, app->rx_settings.speaker_enabled ? "On" : "Off");
}

static void guitar_rx_sync_menu_populate(GuitarApp* app) {
    submenu_reset(app->rx_sync_menu);
    submenu_set_header(app->rx_sync_menu, "Sync to Profile");

    for(GuitarRxAction action = 0; action < GuitarRxActionCount; action++) {
        char label[32];
        snprintf(label, sizeof(label), "%s", guitar_rx_action_name(action));
        submenu_add_item(app->rx_sync_menu, label, action, guitar_rx_sync_callback, app);
    }

    UNUSED(app);
}

void guitar_rx_init(GuitarApp* app) {
    app->rx_menu = submenu_alloc();
    app->rx_sync_menu = submenu_alloc();
    app->rx_settings_list = variable_item_list_alloc();
    app->rx_widget = widget_alloc();
    app->rx_status_text = furi_string_alloc();
    app->rx_last_match = GuitarRxActionCount;

    guitar_rx_settings_load(&app->rx_settings);

    submenu_set_header(app->rx_menu, "Receive");
    submenu_add_item(app->rx_menu, "Listen", GuitarRxMenuListen, guitar_rx_menu_callback, app);
    submenu_add_item(app->rx_menu, "Sync Riff", GuitarRxMenuSync, guitar_rx_menu_callback, app);
    submenu_add_item(app->rx_menu, "Settings", GuitarRxMenuSettings, guitar_rx_menu_callback, app);

    guitar_rx_settings_populate(app);
    guitar_rx_sync_menu_populate(app);
}

void guitar_rx_refresh_settings(GuitarApp* app) {
    guitar_rx_settings_populate(app);
    guitar_rx_sync_menu_populate(app);
}

void guitar_rx_add_views(GuitarApp* app) {
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewRxMenu, submenu_get_view(app->rx_menu));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewRxListen, widget_get_view(app->rx_widget));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewRxSync, submenu_get_view(app->rx_sync_menu));
    view_dispatcher_add_view(
        app->view_dispatcher,
        GuitarViewRxSettings,
        variable_item_list_get_view(app->rx_settings_list));
}

void guitar_rx_remove_views(GuitarApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewRxMenu);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewRxListen);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewRxSync);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewRxSettings);
}

void guitar_rx_free(GuitarApp* app) {
    guitar_rx_stop_radio(app);
    guitar_rx_settings_save(&app->rx_settings);
    submenu_free(app->rx_menu);
    submenu_free(app->rx_sync_menu);
    variable_item_list_free(app->rx_settings_list);
    widget_free(app->rx_widget);
    furi_string_free(app->rx_status_text);
}

bool guitar_rx_handle_navigation(GuitarApp* app) {
    if(app->active_view == GuitarViewRxSync) {
        guitar_app_switch_view(app, GuitarViewRxListen);
        return true;
    }

    if(app->active_view == GuitarViewRxSettings) {
        guitar_rx_settings_save(&app->rx_settings);
        guitar_app_switch_view(app, GuitarViewRxMenu);
        return true;
    }

    if(app->active_view == GuitarViewRxListen) {
        guitar_rx_stop_radio(app);
        guitar_app_switch_view(app, GuitarViewRxMenu);
        return true;
    }

    if(app->active_view == GuitarViewRxMenu) {
        guitar_rx_stop_radio(app);
        guitar_rx_settings_save(&app->rx_settings);
        guitar_app_switch_view(app, GuitarViewMain);
        return true;
    }

    return false;
}

bool guitar_rx_handle_custom_event(GuitarApp* app, uint32_t event) {
    if(event != GUITAR_RX_EVENT_FRAME) return false;

    const GuitarRxFrame* frame = guitar_rx_decoder_get_frame();
    if(frame && frame->valid) {
        guitar_rx_on_frame(app, frame);
    }
    return true;
}

void guitar_rx_handle_tick(GuitarApp* app) {
    if(app->active_view != GuitarViewRxListen || !app->rx_listening) return;
    app->rx_listen_dots++;
    guitar_rx_refresh_listen(app);
}
