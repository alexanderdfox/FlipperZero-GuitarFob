#pragma once

#include <gui/modules/widget.h>
#include <gui/icon.h>
#include <stdbool.h>

void rx_ui_show_status(
    Widget* widget,
    const Icon* icon,
    bool matched,
    const char* title,
    const char* body);

void rx_ui_show_listen(
    Widget* widget,
    const Icon* icon,
    uint32_t carrier_hz,
    uint8_t dot_phase);

void rx_ui_show_banner(Widget* widget, const Icon* icon, const char* title, const char* subtitle);
