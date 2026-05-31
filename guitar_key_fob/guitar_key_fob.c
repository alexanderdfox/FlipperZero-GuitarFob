/**
 * @file guitar_key_fob.c
 * @brief Guitar Key Fob — unified TX/RX Sub-GHz guitar-pluck app.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026, Alexander Fox
 */

#include "guitar_app.h"
#include "settings.h"
#include "rx_settings.h"

#include <gui/gui.h>
#include <notification/notification_messages.h>

static void guitar_app_reset_defaults(GuitarApp* app);
static void guitar_show_reset_confirm(GuitarApp* app);

static void guitar_reset_confirm_callback(GuiButtonType button, InputType type, void* context) {
    if(type != InputTypeShort) return;

    GuitarApp* app = context;
    furi_assert(app);

    if(button == GuiButtonTypeRight) {
        guitar_app_reset_defaults(app);
        notification_message(app->notifications, &sequence_success);
    }

    guitar_app_switch_view(app, GuitarViewMain);
}

static void guitar_show_reset_confirm(GuitarApp* app) {
    widget_reset(app->reset_widget);
    widget_add_text_box_element(
        app->reset_widget,
        4,
        4,
        120,
        14,
        AlignCenter,
        AlignTop,
        "\e#Reset Defaults\e#",
        false);
    widget_add_text_box_element(
        app->reset_widget,
        4,
        22,
        120,
        28,
        AlignCenter,
        AlignTop,
        "Reset TX and RX\nsettings to factory\ndefaults?",
        false);
    widget_add_button_element(
        app->reset_widget, GuiButtonTypeLeft, "Cancel", guitar_reset_confirm_callback, app);
    widget_add_button_element(
        app->reset_widget, GuiButtonTypeRight, "Reset", guitar_reset_confirm_callback, app);
    guitar_app_switch_view(app, GuitarViewResetConfirm);
}

static void guitar_app_reset_defaults(GuitarApp* app) {
    guitar_rx_stop_radio(app);
    guitar_settings_reset(&app->tx_settings);
    guitar_rx_settings_reset(&app->rx_settings);
    app->rx_has_frame = false;
    app->rx_last_match = GuitarRxActionCount;
    app->rx_last_code = 0;
    app->rx_last_riff_str[0] = '\0';
    guitar_tx_refresh_settings(app);
    guitar_rx_refresh_settings(app);
}

static void guitar_main_menu_callback(void* context, uint32_t index) {
    GuitarApp* app = context;
    furi_assert(app);

    switch(index) {
    case GuitarMainMenuTransmit:
        guitar_app_switch_view(app, GuitarViewTxSubmenu);
        break;
    case GuitarMainMenuReceive:
        guitar_app_switch_view(app, GuitarViewRxMenu);
        break;
    case GuitarMainMenuReset:
        guitar_show_reset_confirm(app);
        break;
    default:
        break;
    }
}

void guitar_app_switch_view(GuitarApp* app, uint32_t view) {
    app->active_view = view;
    view_dispatcher_switch_to_view(app->view_dispatcher, view);
}

static bool guitar_app_navigation(void* context) {
    GuitarApp* app = context;
    furi_assert(app);

    if(app->active_view == GuitarViewResetConfirm) {
        guitar_app_switch_view(app, GuitarViewMain);
        return true;
    }

    if(app->active_view == GuitarViewMain) {
        guitar_rx_stop_radio(app);
        guitar_settings_save(&app->tx_settings);
        guitar_rx_settings_save(&app->rx_settings);
        view_dispatcher_stop(app->view_dispatcher);
        return true;
    }

    if(guitar_tx_handle_navigation(app)) return true;
    if(guitar_rx_handle_navigation(app)) return true;

    return false;
}

static bool guitar_app_custom_event(void* context, uint32_t event) {
    GuitarApp* app = context;
    return guitar_rx_handle_custom_event(app, event);
}

static void guitar_app_tick(void* context) {
    GuitarApp* app = context;
    guitar_rx_handle_tick(app);
}

static GuitarApp* guitar_app_alloc(void) {
    GuitarApp* app = malloc(sizeof(GuitarApp));
    memset(app, 0, sizeof(GuitarApp));

    app->view_dispatcher = view_dispatcher_alloc();
    app->main_menu = submenu_alloc();
    app->reset_widget = widget_alloc();
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    submenu_set_header(app->main_menu, "Guitar Key Fob");
    submenu_add_item(
        app->main_menu, "Transmit", GuitarMainMenuTransmit, guitar_main_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Receive", GuitarMainMenuReceive, guitar_main_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Reset Defaults", GuitarMainMenuReset, guitar_main_menu_callback, app);

    guitar_tx_init(app);
    guitar_rx_init(app);

    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, guitar_app_navigation);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, guitar_app_custom_event);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, guitar_app_tick, 500);

    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewMain, submenu_get_view(app->main_menu));
    view_dispatcher_add_view(
        app->view_dispatcher, GuitarViewResetConfirm, widget_get_view(app->reset_widget));
    guitar_tx_add_views(app);
    guitar_rx_add_views(app);

    return app;
}

static void guitar_app_free(GuitarApp* app) {
    guitar_rx_stop_radio(app);
    guitar_settings_save(&app->tx_settings);
    guitar_rx_settings_save(&app->rx_settings);

    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewMain);
    view_dispatcher_remove_view(app->view_dispatcher, GuitarViewResetConfirm);
    guitar_tx_remove_views(app);
    guitar_rx_remove_views(app);

    view_dispatcher_free(app->view_dispatcher);
    submenu_free(app->main_menu);
    widget_free(app->reset_widget);
    guitar_tx_free(app);
    guitar_rx_free(app);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t guitar_key_fob_app(void* arg) {
    UNUSED(arg);

    GuitarApp* app = guitar_app_alloc();
    guitar_app_switch_view(app, GuitarViewMain);
    view_dispatcher_run(app->view_dispatcher);
    guitar_app_free(app);
    return 0;
}
