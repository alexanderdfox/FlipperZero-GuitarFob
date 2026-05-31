#include "rx_ui.h"

#include <stddef.h>
#include <stdio.h>

/* Flipper display is 128x64. Logo is 17x32 — keep it in a left column. */
#define RX_ICON_X 4
#define RX_ICON_Y_SIDE 16
#define RX_ICON_Y_TOP 2
#define RX_TEXT_X 26
#define RX_TEXT_W 98
#define RX_MARGIN 4
#define RX_DIVIDER_Y 36
#define RX_BODY_Y 38
#define RX_BODY_H 26

static void rx_ui_divider(Widget* widget) {
    widget_add_line_element(widget, RX_MARGIN, RX_DIVIDER_Y, 128 - RX_MARGIN, RX_DIVIDER_Y);
}

static void rx_ui_icon_side(Widget* widget, const Icon* icon) {
    if(icon) {
        widget_add_icon_element(widget, RX_ICON_X, RX_ICON_Y_SIDE, icon);
    }
}

static void rx_ui_icon_top(Widget* widget, const Icon* icon) {
    if(icon) {
        widget_add_icon_element(widget, RX_ICON_X, RX_ICON_Y_TOP, icon);
    }
}

static void rx_ui_format_mhz(char* buf, size_t len, uint32_t carrier_hz) {
    snprintf(
        buf,
        len,
        "%lu.%02lu MHz",
        (unsigned long)(carrier_hz / 1000000UL),
        (unsigned long)((carrier_hz / 10000UL) % 100UL));
}

void rx_ui_show_status(
    Widget* widget,
    const Icon* icon,
    bool matched,
    const char* title,
    const char* body) {
    widget_reset(widget);

    rx_ui_icon_top(widget, icon);

    char title_buf[64];
    if(matched) {
        snprintf(title_buf, sizeof(title_buf), "\e#%s\e#", title);
    } else {
        snprintf(title_buf, sizeof(title_buf), "\e!%s\e!", title);
    }
    widget_add_text_box_element(
        widget, RX_TEXT_X, 8, RX_TEXT_W, 14, AlignLeft, AlignTop, title_buf, false);

    rx_ui_divider(widget);
    widget_add_text_scroll_element(widget, RX_MARGIN, RX_BODY_Y, 120, RX_BODY_H, body);
}

void rx_ui_show_listen(
    Widget* widget,
    const Icon* icon,
    uint32_t carrier_hz,
    uint8_t dot_phase) {
    widget_reset(widget);

    rx_ui_icon_side(widget, icon);

    static const char* dots[] = {"", ".", "..", "..."};
    const char* anim = dots[dot_phase % 4];

    char mhz[16];
    rx_ui_format_mhz(mhz, sizeof(mhz), carrier_hz);

    char buffer[96];
    snprintf(
        buffer,
        sizeof(buffer),
        "\e#Listening%s\e#\n%s\n\nWaiting for pluck",
        anim,
        mhz);

    widget_add_text_box_element(
        widget, RX_TEXT_X, 8, RX_TEXT_W, 48, AlignLeft, AlignTop, buffer, false);
}

void rx_ui_show_banner(
    Widget* widget,
    const Icon* icon,
    const char* title,
    const char* subtitle) {
    widget_reset(widget);

    rx_ui_icon_side(widget, icon);

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "\e#%s\e#\n%s", title, subtitle);
    widget_add_text_box_element(
        widget, RX_TEXT_X, 12, RX_TEXT_W, 40, AlignLeft, AlignTop, buffer, false);
}
