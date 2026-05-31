#include "guitar_app.h"
#include "settings.h"
#include "riff.h"
#include "waveform.h"
#include "rf_tx.h"
#include "speaker_pluck.h"
#include "guitar_ui.h"
#include "guitar_key_fob_icons.h"

#include <string.h>
#include <notification/notification_messages.h>

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
} GuitarSettingItem;

typedef enum {
    GuitarRiffMenuUnlock,
    GuitarRiffMenuLock,
    GuitarRiffMenuTrunk,
    GuitarRiffMenuAlarm,
} GuitarRiffMenuItem;

static void guitar_tx_show_status(GuitarApp* app, bool ok, const char* title, const char* body);
static void guitar_tx_settings_populate(GuitarApp* app);
static void guitar_tx_riff_menu_populate(GuitarApp* app);

static bool guitar_tx_transmit_action(GuitarApp* app, GuitarAction action) {
    GuitarRiff riff;
    if(!guitar_riff_for_action(&riff, action, &app->tx_settings)) return false;

    guitar_rf_encoder_clear(&app->tx_encoder);

    for(uint8_t i = 0; i < riff.count; i++) {
        GuitarPluckParams params;
        guitar_pluck_params_from_riff_note(&params, &riff, i, &app->tx_settings);
        if(!guitar_rf_encoder_build_pluck(&app->tx_encoder, &params, &app->tx_settings, action)) {
            return false;
        }
    }

    const bool ok = guitar_rf_transmit(&app->tx_encoder, &app->tx_settings, &riff);
    guitar_settings_save(&app->tx_settings);
    return ok;
}

static void guitar_tx_show_status(GuitarApp* app, bool ok, const char* title, const char* body) {
    guitar_ui_show_status(app->tx_status_widget, &I_guitar_logo_17x32, ok, title, body);
    guitar_app_switch_view(app, GuitarViewTxStatus);
}

static void guitar_tx_submenu_callback(void* context, uint32_t index) {
    GuitarApp* app = context;
    furi_assert(app);

    if(index == GuitarSubmenuSettings) {
        guitar_app_switch_view(app, GuitarViewTxSettings);
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

    const bool ok = guitar_tx_transmit_action(app, action);
    char preview[32];
    guitar_riff_preview(&app->tx_settings, action, preview, sizeof(preview));

    if(ok) {
        furi_string_printf(
            app->tx_status_text,
            "Riff: %s\n%lu.%02lu MHz  #%lu",
            preview,
            app->tx_settings.carrier_hz / 1000000,
            (app->tx_settings.carrier_hz / 10000) % 100,
            app->tx_settings.rolling_code);
        guitar_tx_show_status(
            app, true, guitar_action_name(action), furi_string_get_cstr(app->tx_status_text));
        notification_message(app->notifications, &sequence_success);
    } else {
        guitar_tx_show_status(app, false, "TX Failed", "Check region\nor frequency lock");
        notification_message(app->notifications, &sequence_error);
    }

    notification_message(app->notifications, &sequence_blink_stop);
}

static GuitarAction guitar_tx_riff_index_to_action(uint32_t index) {
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

static void guitar_tx_riff_text_input_callback(void* context) {
    GuitarApp* app = context;
    furi_assert(app);

    GuitarRiff parsed;
    char* dest =
        guitar_riff_string_mut_for_action(&app->tx_settings, app->tx_editing_riff_action);

    if(guitar_riff_parse(&parsed, app->tx_riff_edit_buffer)) {
        strncpy(dest, app->tx_riff_edit_buffer, GUITAR_RIFF_STR_LEN - 1);
        dest[GUITAR_RIFF_STR_LEN - 1] = '\0';
        guitar_settings_save(&app->tx_settings);
    }

    guitar_tx_riff_menu_populate(app);
    guitar_app_switch_view(app, GuitarViewTxRiffMenu);
}

static void guitar_tx_riff_menu_callback(void* context, uint32_t index) {
    GuitarApp* app = context;
    furi_assert(app);

    app->tx_editing_riff_action = guitar_tx_riff_index_to_action(index);
    const char* current =
        guitar_riff_string_for_action(&app->tx_settings, app->tx_editing_riff_action);

    strncpy(app->tx_riff_edit_buffer, current, sizeof(app->tx_riff_edit_buffer) - 1);
    app->tx_riff_edit_buffer[sizeof(app->tx_riff_edit_buffer) - 1] = '\0';

    FuriString* header = furi_string_alloc_printf(
        "%s Riff (Hz,+bend)", guitar_action_name(app->tx_editing_riff_action));
    text_input_reset(app->tx_text_input);
    text_input_set_header_text(app->tx_text_input, furi_string_get_cstr(header));
    furi_string_free(header);

    text_input_set_result_callback(
        app->tx_text_input,
        guitar_tx_riff_text_input_callback,
        app,
        app->tx_riff_edit_buffer,
        sizeof(app->tx_riff_edit_buffer),
        true);

    guitar_app_switch_view(app, GuitarViewTxTextInput);
}

static void guitar_tx_riff_menu_populate(GuitarApp* app) {
    submenu_reset(app->tx_riff_menu);
    submenu_set_header(app->tx_riff_menu, "Custom Riffs");

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
        guitar_riff_preview(&app->tx_settings, actions[i], preview, sizeof(preview));
        snprintf(label, sizeof(label), "%s: %s", guitar_action_name(actions[i]), preview);
        submenu_add_item(app->tx_riff_menu, label, indices[i], guitar_tx_riff_menu_callback, app);
    }
}

static void guitar_tx_settings_enter_callback(void* context, uint32_t index) {
    GuitarApp* app = context;
    furi_assert(app);

    if(index == GuitarSettingEditRiffs) {
        guitar_tx_riff_menu_populate(app);
        guitar_app_switch_view(app, GuitarViewTxRiffMenu);
    }
}

static void guitar_tx_setting_change_carrier(VariableItem* item) {
    GuitarApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->tx_settings.carrier_hz =
        guitar_settings_carrier_hz(index == 1 ? GuitarCarrierEU : GuitarCarrierUS);
    variable_item_set_current_value_text(item, index == 1 ? "433.92 MHz" : "315 MHz");
}

static void guitar_tx_setting_change_attack(VariableItem* item) {
    GuitarApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->tx_settings.attack_ms = (uint16_t)(8 + index);
    FuriString* text = furi_string_alloc_printf("%u ms", app->tx_settings.attack_ms);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void guitar_tx_setting_change_decay(VariableItem* item) {
    GuitarApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->tx_settings.decay_ms = (uint16_t)(300 + index * 50);
    FuriString* text = furi_string_alloc_printf("%u ms", app->tx_settings.decay_ms);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void guitar_tx_setting_change_harm2(VariableItem* item) {
    GuitarApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->tx_settings.harmonic2_pct = (uint8_t)(index * 10);
    FuriString* text = furi_string_alloc_printf("%u%%", app->tx_settings.harmonic2_pct);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void guitar_tx_setting_change_harm3(VariableItem* item) {
    GuitarApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->tx_settings.harmonic3_pct = (uint8_t)(index * 5);
    FuriString* text = furi_string_alloc_printf("%u%%", app->tx_settings.harmonic3_pct);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void guitar_tx_setting_change_vibrato(VariableItem* item) {
    GuitarApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->tx_settings.vibrato_depth_pct = (uint8_t)(index * 2);
    FuriString* text = furi_string_alloc_printf("%u%%", app->tx_settings.vibrato_depth_pct);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void guitar_tx_setting_change_speaker(VariableItem* item) {
    GuitarApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->tx_settings.speaker_enabled = index == 1;
    variable_item_set_current_value_text(item, app->tx_settings.speaker_enabled ? "On" : "Off");
}

static void guitar_tx_settings_populate(GuitarApp* app) {
    variable_item_list_reset(app->tx_settings_list);

    VariableItem* item = variable_item_list_add(
        app->tx_settings_list, "Carrier", 2, guitar_tx_setting_change_carrier, app);
    variable_item_set_current_value_index(
        item, app->tx_settings.carrier_hz == GUITAR_CARRIER_EU_HZ ? 1 : 0);
    variable_item_set_current_value_text(
        item, app->tx_settings.carrier_hz == GUITAR_CARRIER_EU_HZ ? "433.92 MHz" : "315 MHz");

    item = variable_item_list_add(
        app->tx_settings_list, "Attack", 8, guitar_tx_setting_change_attack, app);
    variable_item_set_current_value_index(item, (uint8_t)(app->tx_settings.attack_ms - 8));
    FuriString* attack_text = furi_string_alloc_printf("%u ms", app->tx_settings.attack_ms);
    variable_item_set_current_value_text(item, furi_string_get_cstr(attack_text));
    furi_string_free(attack_text);

    item = variable_item_list_add(
        app->tx_settings_list, "Decay", 9, guitar_tx_setting_change_decay, app);
    variable_item_set_current_value_index(
        item, (uint8_t)((app->tx_settings.decay_ms - 300) / 50));
    FuriString* decay_text = furi_string_alloc_printf("%u ms", app->tx_settings.decay_ms);
    variable_item_set_current_value_text(item, furi_string_get_cstr(decay_text));
    furi_string_free(decay_text);

    item = variable_item_list_add(
        app->tx_settings_list, "2nd Harm", 11, guitar_tx_setting_change_harm2, app);
    variable_item_set_current_value_index(item, app->tx_settings.harmonic2_pct / 10);
    FuriString* h2 = furi_string_alloc_printf("%u%%", app->tx_settings.harmonic2_pct);
    variable_item_set_current_value_text(item, furi_string_get_cstr(h2));
    furi_string_free(h2);

    item = variable_item_list_add(
        app->tx_settings_list, "3rd Harm", 21, guitar_tx_setting_change_harm3, app);
    variable_item_set_current_value_index(item, app->tx_settings.harmonic3_pct / 5);
    FuriString* h3 = furi_string_alloc_printf("%u%%", app->tx_settings.harmonic3_pct);
    variable_item_set_current_value_text(item, furi_string_get_cstr(h3));
    furi_string_free(h3);

    item = variable_item_list_add(
        app->tx_settings_list, "Vibrato", 51, guitar_tx_setting_change_vibrato, app);
    variable_item_set_current_value_index(item, app->tx_settings.vibrato_depth_pct / 2);
    FuriString* vib = furi_string_alloc_printf("%u%%", app->tx_settings.vibrato_depth_pct);
    variable_item_set_current_value_text(item, furi_string_get_cstr(vib));
    furi_string_free(vib);

    item = variable_item_list_add(
        app->tx_settings_list, "Speaker", 2, guitar_tx_setting_change_speaker, app);
    variable_item_set_current_value_index(item, app->tx_settings.speaker_enabled ? 1 : 0);
    variable_item_set_current_value_text(item, app->tx_settings.speaker_enabled ? "On" : "Off");

    variable_item_list_add(app->tx_settings_list, "Edit Riffs", 1, NULL, app);

    variable_item_list_set_enter_callback(
        app->tx_settings_list, guitar_tx_settings_enter_callback, app);
}

void guitar_tx_refresh_settings(GuitarApp* app) {
    guitar_tx_settings_populate(app);
    guitar_tx_riff_menu_populate(app);
}

void guitar_tx_init(GuitarApp* app) {
    app->tx_submenu = submenu_alloc();
    app->tx_riff_menu = submenu_alloc();
    app->tx_settings_list = variable_item_list_alloc();
    app->tx_text_input = text_input_alloc();
    app->tx_status_widget = widget_alloc();
    app->tx_status_text = furi_string_alloc();
    app->tx_editing_riff_action = GuitarActionUnlock;

    guitar_settings_load(&app->tx_settings);
    guitar_rf_encoder_clear(&app->tx_encoder);

    submenu_set_header(app->tx_submenu, "Transmit");
    submenu_add_item(
        app->tx_submenu, "Unlock   G3", GuitarSubmenuUnlock, guitar_tx_submenu_callback, app);
    submenu_add_item(
        app->tx_submenu, "Lock     B3", GuitarSubmenuLock, guitar_tx_submenu_callback, app);
    submenu_add_item(
        app->tx_submenu, "Trunk    E4", GuitarSubmenuTrunk, guitar_tx_submenu_callback, app);
    submenu_add_item(
        app->tx_submenu, "Alarm  Riff", GuitarSubmenuAlarm, guitar_tx_submenu_callback, app);
    submenu_add_item(
        app->tx_submenu, "Settings", GuitarSubmenuSettings, guitar_tx_submenu_callback, app);

    guitar_tx_settings_populate(app);
    guitar_tx_riff_menu_populate(app);
}

void guitar_tx_add_views(GuitarApp* app) {
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewTxSubmenu, submenu_get_view(app->tx_submenu));
    view_dispatcher_add_view(
        app->view_dispatcher,
        GuitarViewTxSettings,
        variable_item_list_get_view(app->tx_settings_list));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewTxRiffMenu, submenu_get_view(app->tx_riff_menu));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewTxTextInput, text_input_get_view(app->tx_text_input));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewTxStatus, widget_get_view(app->tx_status_widget));
}

void guitar_tx_remove_views(GuitarApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewTxSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewTxSettings);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewTxRiffMenu);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewTxTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewTxStatus);
}

void guitar_tx_free(GuitarApp* app) {
    guitar_settings_save(&app->tx_settings);
    submenu_free(app->tx_submenu);
    submenu_free(app->tx_riff_menu);
    variable_item_list_free(app->tx_settings_list);
    text_input_free(app->tx_text_input);
    widget_free(app->tx_status_widget);
    furi_string_free(app->tx_status_text);
}

bool guitar_tx_handle_navigation(GuitarApp* app) {
    if(app->active_view == GuitarViewTxTextInput) {
        guitar_app_switch_view(app, GuitarViewTxRiffMenu);
        return true;
    }

    if(app->active_view == GuitarViewTxRiffMenu) {
        guitar_app_switch_view(app, GuitarViewTxSettings);
        return true;
    }

    if(app->active_view == GuitarViewTxSubmenu) {
        guitar_settings_save(&app->tx_settings);
        guitar_app_switch_view(app, GuitarViewMain);
        return true;
    }

    if(app->active_view >= GuitarViewTxSubmenu && app->active_view <= GuitarViewTxStatus) {
        guitar_settings_save(&app->tx_settings);
        guitar_app_switch_view(app, GuitarViewTxSubmenu);
        return true;
    }

    return false;
}
