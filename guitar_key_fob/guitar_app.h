#pragma once

#include "guitar_types.h"
#include "rf_tx.h"
#include "rx_types.h"

#include <furi.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/modules/text_input.h>
#include <notification/notification.h>

typedef enum {
    GuitarViewMain,
    GuitarViewTxSubmenu,
    GuitarViewTxSettings,
    GuitarViewTxRiffMenu,
    GuitarViewTxTextInput,
    GuitarViewTxStatus,
    GuitarViewRxMenu,
    GuitarViewRxListen,
    GuitarViewRxSync,
    GuitarViewRxSettings,
    GuitarViewResetConfirm,
    GuitarViewCount,
} GuitarView;

typedef enum {
    GuitarMainMenuTransmit,
    GuitarMainMenuReceive,
    GuitarMainMenuReset,
} GuitarMainMenuItem;

typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* main_menu;
    Submenu* tx_submenu;
    Submenu* tx_riff_menu;
    VariableItemList* tx_settings_list;
    TextInput* tx_text_input;
    Widget* tx_status_widget;
    Submenu* rx_menu;
    Submenu* rx_sync_menu;
    VariableItemList* rx_settings_list;
    Widget* rx_widget;
    Widget* reset_widget;
    NotificationApp* notifications;
    GuitarSettings tx_settings;
    GuitarRfEncoder tx_encoder;
    GuitarRxSettings rx_settings;
    FuriString* tx_status_text;
    FuriString* rx_status_text;
    char tx_riff_edit_buffer[GUITAR_RIFF_STR_LEN];
    char rx_last_riff_str[GUITAR_RX_RIFF_STR_LEN];
    GuitarAction tx_editing_riff_action;
    uint16_t rx_last_code;
    GuitarRxAction rx_last_match;
    bool rx_has_frame;
    bool rx_listening;
    uint8_t rx_listen_dots;
    uint32_t active_view;
} GuitarApp;

void guitar_app_switch_view(GuitarApp* app, uint32_t view);

void guitar_tx_init(GuitarApp* app);
void guitar_tx_add_views(GuitarApp* app);
void guitar_tx_remove_views(GuitarApp* app);
void guitar_tx_free(GuitarApp* app);
void guitar_tx_refresh_settings(GuitarApp* app);
bool guitar_tx_handle_navigation(GuitarApp* app);

void guitar_rx_init(GuitarApp* app);
void guitar_rx_add_views(GuitarApp* app);
void guitar_rx_remove_views(GuitarApp* app);
void guitar_rx_free(GuitarApp* app);
void guitar_rx_refresh_settings(GuitarApp* app);
void guitar_rx_stop_radio(GuitarApp* app);
bool guitar_rx_handle_navigation(GuitarApp* app);
bool guitar_rx_handle_custom_event(GuitarApp* app, uint32_t event);
void guitar_rx_handle_tick(GuitarApp* app);
