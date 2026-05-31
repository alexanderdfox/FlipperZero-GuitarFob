/**
 * @file guitar_key_fob.c
 * @brief Guitar-pluck analog RF key fob for Flipper Zero Sub-GHz.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026, Alexander Fox
 */

#include "guitar_types.h"
#include "waveform.h"
#include "rf_tx.h"
#include "settings.h"
#include "riff.h"
#include "speaker_pluck.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/modules/text_input.h>
#include <notification/notification_messages.h>

#define TAG "GuitarKeyFob"

typedef enum {
    GuitarViewSubmenu,
    GuitarViewSettings,
    GuitarViewRiffMenu,
    GuitarViewTextInput,
    GuitarViewStatus,
    GuitarViewCount,
} GuitarView;

typedef enum {
    GuitarSubmenuUnlock,
    GuitarSubmenuLock,
    GuitarSubmenuTrunk,
    GuitarSubmenuAlarm,
    GuitarSubmenuSettings,
} GuitarSubmenuItem;

typedef enum {
    GuitarSettingCarrier,
    GuitarSettingAttack,
    GuitarSettingDecay,
    GuitarSettingHarm2,
    GuitarSettingHarm3,
    GuitarSettingVibrato,
    GuitarSettingSpeaker,
    GuitarSettingEditRiffs,
    GuitarSettingCount,
} GuitarSettingItem;

typedef enum {
    GuitarRiffMenuUnlock,
    GuitarRiffMenuLock,
    GuitarRiffMenuTrunk,
    GuitarRiffMenuAlarm,
} GuitarRiffMenuItem;

typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    Submenu* riff_menu;
    VariableItemList* settings_list;
    TextInput* text_input;
    Widget* status_widget;
    NotificationApp* notifications;
    GuitarSettings settings;
    GuitarRfEncoder encoder;
    FuriString* status_text;
    char riff_edit_buffer[GUITAR_RIFF_STR_LEN];
    GuitarAction editing_riff_action;
    uint32_t active_view;
} GuitarKeyFobApp;

static void guitar_setting_change_carrier(VariableItem* item);
static void guitar_setting_change_attack(VariableItem* item);
static void guitar_setting_change_decay(VariableItem* item);
static void guitar_setting_change_harm2(VariableItem* item);
static void guitar_setting_change_harm3(VariableItem* item);
static void guitar_setting_change_vibrato(VariableItem* item);
static void guitar_setting_change_speaker(VariableItem* item);
static void guitar_settings_populate(GuitarKeyFobApp* app);
static void guitar_riff_menu_populate(GuitarKeyFobApp* app);

static void guitar_switch_view(GuitarKeyFobApp* app, uint32_t view) {
    app->active_view = view;
    view_dispatcher_switch_to_view(app->view_dispatcher, view);
}

static void guitar_show_status(GuitarKeyFobApp* app, const char* message) {
    widget_reset(app->status_widget);
    furi_string_set_str(app->status_text, message);
    widget_add_text_scroll_element(
        app->status_widget, 0, 0, 128, 64, furi_string_get_cstr(app->status_text));
    guitar_switch_view(app, GuitarViewStatus);
}

static bool guitar_transmit_action(GuitarKeyFobApp* app, GuitarAction action) {
    GuitarRiff riff;
    if(!guitar_riff_for_action(&riff, action, &app->settings)) return false;

    guitar_rf_encoder_clear(&app->encoder);

    for(uint8_t i = 0; i < riff.count; i++) {
        GuitarPluckParams params;
        guitar_pluck_params_from_riff_note(&params, &riff, i, &app->settings);
        if(!guitar_rf_encoder_build_pluck(&app->encoder, &params, &app->settings)) {
            return false;
        }
    }

    const bool ok = guitar_rf_transmit(&app->encoder, &app->settings, &riff);
    guitar_settings_save(&app->settings);
    return ok;
}

static void guitar_submenu_callback(void* context, uint32_t index) {
    GuitarKeyFobApp* app = context;
    furi_assert(app);

    if(index == GuitarSubmenuSettings) {
        guitar_switch_view(app, GuitarViewSettings);
        return;
    }

    GuitarAction action = GuitarActionUnlock;
    switch(index) {
    case GuitarSubmenuUnlock:
        action = GuitarActionUnlock;
        break;
    case GuitarSubmenuLock:
        action = GuitarActionLock;
        break;
    case GuitarSubmenuTrunk:
        action = GuitarActionTrunk;
        break;
    case GuitarSubmenuAlarm:
        action = GuitarActionAlarm;
        break;
    default:
        return;
    }

    notification_message(app->notifications, &sequence_blink_start_cyan);

    const bool ok = guitar_transmit_action(app, action);
    char preview[32];
    guitar_riff_preview(&app->settings, action, preview, sizeof(preview));

    if(ok) {
        furi_string_printf(
            app->status_text,
            "%s sent\nRiff: %s\n%lu MHz\nCode: %lu",
            guitar_action_name(action),
            preview,
            app->settings.carrier_hz / 1000000,
            app->settings.rolling_code);
        notification_message(app->notifications, &sequence_success);
    } else {
        furi_string_printf(
            app->status_text,
            "TX failed\nCheck region\nor frequency lock");
        notification_message(app->notifications, &sequence_error);
    }

    guitar_show_status(app, furi_string_get_cstr(app->status_text));
    notification_message(app->notifications, &sequence_blink_stop);
}

static GuitarAction guitar_riff_menu_index_to_action(uint32_t index) {
    switch(index) {
    case GuitarRiffMenuLock:
        return GuitarActionLock;
    case GuitarRiffMenuTrunk:
        return GuitarActionTrunk;
    case GuitarRiffMenuAlarm:
        return GuitarActionAlarm;
    case GuitarRiffMenuUnlock:
    default:
        return GuitarActionUnlock;
    }
}

static void guitar_riff_text_input_callback(void* context) {
    GuitarKeyFobApp* app = context;
    furi_assert(app);

    GuitarRiff parsed;
    char* dest = guitar_riff_string_mut_for_action(&app->settings, app->editing_riff_action);

    if(guitar_riff_parse(&parsed, app->riff_edit_buffer)) {
        strncpy(dest, app->riff_edit_buffer, GUITAR_RIFF_STR_LEN - 1);
        dest[GUITAR_RIFF_STR_LEN - 1] = '\0';
        guitar_settings_save(&app->settings);
    }

    guitar_riff_menu_populate(app);
    guitar_switch_view(app, GuitarViewRiffMenu);
}

static void guitar_riff_menu_callback(void* context, uint32_t index) {
    GuitarKeyFobApp* app = context;
    furi_assert(app);

    app->editing_riff_action = guitar_riff_menu_index_to_action(index);
    const char* current = guitar_riff_string_for_action(&app->settings, app->editing_riff_action);

    strncpy(app->riff_edit_buffer, current, sizeof(app->riff_edit_buffer) - 1);
    app->riff_edit_buffer[sizeof(app->riff_edit_buffer) - 1] = '\0';

    FuriString* header = furi_string_alloc_printf(
        "%s Riff (Hz,+bend)", guitar_action_name(app->editing_riff_action));
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, furi_string_get_cstr(header));
    furi_string_free(header);

    text_input_set_result_callback(
        app->text_input,
        guitar_riff_text_input_callback,
        app,
        app->riff_edit_buffer,
        sizeof(app->riff_edit_buffer),
        true);

    guitar_switch_view(app, GuitarViewTextInput);
}

static void guitar_riff_menu_populate(GuitarKeyFobApp* app) {
    submenu_reset(app->riff_menu);

    char label[GUITAR_RIFF_STR_LEN + 16];
    const GuitarAction actions[] = {
        GuitarActionUnlock,
        GuitarActionLock,
        GuitarActionTrunk,
        GuitarActionAlarm,
    };
    const uint32_t indices[] = {
        GuitarRiffMenuUnlock,
        GuitarRiffMenuLock,
        GuitarRiffMenuTrunk,
        GuitarRiffMenuAlarm,
    };

    for(size_t i = 0; i < COUNT_OF(actions); i++) {
        char preview[20];
        guitar_riff_preview(&app->settings, actions[i], preview, sizeof(preview));
        snprintf(label, sizeof(label), "%s: %s", guitar_action_name(actions[i]), preview);
        submenu_add_item(app->riff_menu, label, indices[i], guitar_riff_menu_callback, app);
    }
}

static void guitar_settings_enter_callback(void* context, uint32_t index) {
    GuitarKeyFobApp* app = context;
    furi_assert(app);

    if(index == GuitarSettingEditRiffs) {
        guitar_riff_menu_populate(app);
        guitar_switch_view(app, GuitarViewRiffMenu);
    }
}

static void guitar_settings_populate(GuitarKeyFobApp* app) {
    variable_item_list_reset(app->settings_list);

    VariableItem* item = variable_item_list_add(
        app->settings_list, "Carrier", 2, guitar_setting_change_carrier, app);
    variable_item_set_current_value_index(
        item, app->settings.carrier_hz == GUITAR_CARRIER_EU_HZ ? 1 : 0);
    variable_item_set_current_value_text(
        item, app->settings.carrier_hz == GUITAR_CARRIER_EU_HZ ? "433.92 MHz" : "315 MHz");

    item = variable_item_list_add(
        app->settings_list, "Attack", 8, guitar_setting_change_attack, app);
    variable_item_set_current_value_index(item, (uint8_t)(app->settings.attack_ms - 8));
    FuriString* attack_text = furi_string_alloc_printf("%u ms", app->settings.attack_ms);
    variable_item_set_current_value_text(item, furi_string_get_cstr(attack_text));
    furi_string_free(attack_text);

    item = variable_item_list_add(
        app->settings_list, "Decay", 9, guitar_setting_change_decay, app);
    variable_item_set_current_value_index(
        item, (uint8_t)((app->settings.decay_ms - 300) / 50));
    FuriString* decay_text = furi_string_alloc_printf("%u ms", app->settings.decay_ms);
    variable_item_set_current_value_text(item, furi_string_get_cstr(decay_text));
    furi_string_free(decay_text);

    item = variable_item_list_add(
        app->settings_list, "2nd Harm", 11, guitar_setting_change_harm2, app);
    variable_item_set_current_value_index(item, app->settings.harmonic2_pct / 10);
    FuriString* h2 = furi_string_alloc_printf("%u%%", app->settings.harmonic2_pct);
    variable_item_set_current_value_text(item, furi_string_get_cstr(h2));
    furi_string_free(h2);

    item = variable_item_list_add(
        app->settings_list, "3rd Harm", 21, guitar_setting_change_harm3, app);
    variable_item_set_current_value_index(item, app->settings.harmonic3_pct / 5);
    FuriString* h3 = furi_string_alloc_printf("%u%%", app->settings.harmonic3_pct);
    variable_item_set_current_value_text(item, furi_string_get_cstr(h3));
    furi_string_free(h3);

    item = variable_item_list_add(
        app->settings_list, "Vibrato", 51, guitar_setting_change_vibrato, app);
    variable_item_set_current_value_index(item, app->settings.vibrato_depth_pct / 2);
    FuriString* vib = furi_string_alloc_printf("%u%%", app->settings.vibrato_depth_pct);
    variable_item_set_current_value_text(item, furi_string_get_cstr(vib));
    furi_string_free(vib);

    item = variable_item_list_add(
        app->settings_list, "Speaker", 2, guitar_setting_change_speaker, app);
    variable_item_set_current_value_index(item, app->settings.speaker_enabled ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.speaker_enabled ? "On" : "Off");

    variable_item_list_add(app->settings_list, "Edit Riffs", 1, NULL, app);

    variable_item_list_set_enter_callback(
        app->settings_list, guitar_settings_enter_callback, app);
}

static void guitar_setting_change_carrier(VariableItem* item) {
    GuitarKeyFobApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.carrier_hz =
        guitar_settings_carrier_hz(index == 1 ? GuitarCarrierEU : GuitarCarrierUS);
    variable_item_set_current_value_text(item, index == 1 ? "433.92 MHz" : "315 MHz");
}

static void guitar_setting_change_attack(VariableItem* item) {
    GuitarKeyFobApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.attack_ms = (uint16_t)(8 + index);
    FuriString* text = furi_string_alloc_printf("%u ms", app->settings.attack_ms);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void guitar_setting_change_decay(VariableItem* item) {
    GuitarKeyFobApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.decay_ms = (uint16_t)(300 + index * 50);
    FuriString* text = furi_string_alloc_printf("%u ms", app->settings.decay_ms);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void guitar_setting_change_harm2(VariableItem* item) {
    GuitarKeyFobApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.harmonic2_pct = (uint8_t)(index * 10);
    FuriString* text = furi_string_alloc_printf("%u%%", app->settings.harmonic2_pct);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void guitar_setting_change_harm3(VariableItem* item) {
    GuitarKeyFobApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.harmonic3_pct = (uint8_t)(index * 5);
    FuriString* text = furi_string_alloc_printf("%u%%", app->settings.harmonic3_pct);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void guitar_setting_change_vibrato(VariableItem* item) {
    GuitarKeyFobApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.vibrato_depth_pct = (uint8_t)(index * 2);
    FuriString* text = furi_string_alloc_printf("%u%%", app->settings.vibrato_depth_pct);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void guitar_setting_change_speaker(VariableItem* item) {
    GuitarKeyFobApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.speaker_enabled = index == 1;
    variable_item_set_current_value_text(item, app->settings.speaker_enabled ? "On" : "Off");
}

static bool guitar_navigation_callback(void* context) {
    GuitarKeyFobApp* app = context;
    furi_assert(app);

    if(app->active_view == GuitarViewTextInput) {
        guitar_switch_view(app, GuitarViewRiffMenu);
        return true;
    }

    if(app->active_view == GuitarViewRiffMenu) {
        guitar_switch_view(app, GuitarViewSettings);
        return true;
    }

    if(app->active_view == GuitarViewSubmenu) {
        guitar_settings_save(&app->settings);
        view_dispatcher_stop(app->view_dispatcher);
        return true;
    }

    guitar_settings_save(&app->settings);
    guitar_switch_view(app, GuitarViewSubmenu);
    return true;
}

static GuitarKeyFobApp* guitar_app_alloc(void) {
    GuitarKeyFobApp* app = malloc(sizeof(GuitarKeyFobApp));

    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->riff_menu = submenu_alloc();
    app->settings_list = variable_item_list_alloc();
    app->text_input = text_input_alloc();
    app->status_widget = widget_alloc();
    app->status_text = furi_string_alloc();
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->active_view = GuitarViewSubmenu;
    app->editing_riff_action = GuitarActionUnlock;

    guitar_settings_load(&app->settings);
    guitar_rf_encoder_clear(&app->encoder);

    submenu_add_item(app->submenu, "Unlock", GuitarSubmenuUnlock, guitar_submenu_callback, app);
    submenu_add_item(app->submenu, "Lock", GuitarSubmenuLock, guitar_submenu_callback, app);
    submenu_add_item(app->submenu, "Trunk", GuitarSubmenuTrunk, guitar_submenu_callback, app);
    submenu_add_item(app->submenu, "Alarm", GuitarSubmenuAlarm, guitar_submenu_callback, app);
    submenu_add_item(app->submenu, "Settings", GuitarSubmenuSettings, guitar_submenu_callback, app);

    guitar_settings_populate(app);
    guitar_riff_menu_populate(app);

    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, guitar_navigation_callback);

    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewSettings, variable_item_list_get_view(app->settings_list));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewRiffMenu, submenu_get_view(app->riff_menu));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewTextInput, text_input_get_view(app->text_input));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewStatus, widget_get_view(app->status_widget));

    return app;
}

static void guitar_app_free(GuitarKeyFobApp* app) {
    guitar_settings_save(&app->settings);

    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewRiffMenu);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewStatus);

    view_dispatcher_free(app->view_dispatcher);
    submenu_free(app->submenu);
    submenu_free(app->riff_menu);
    variable_item_list_free(app->settings_list);
    text_input_free(app->text_input);
    widget_free(app->status_widget);
    furi_string_free(app->status_text);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t guitar_key_fob_app(void* arg) {
    UNUSED(arg);

    GuitarKeyFobApp* app = guitar_app_alloc();
    guitar_switch_view(app, GuitarViewSubmenu);
    view_dispatcher_run(app->view_dispatcher);
    guitar_app_free(app);

    return 0;
}
