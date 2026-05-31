#include "guitar_ui.h"

#include <stdio.h>

/* Flipper display is 128x64. Logo is 17x32 — keep it in a left column. */
#define GUITAR_ICON_X 4
#define GUITAR_ICON_Y_SIDE 16
#define GUITAR_ICON_Y_TOP 2
#define GUITAR_TEXT_X 26
#define GUITAR_TEXT_W 98
#define GUITAR_MARGIN 4
#define GUITAR_DIVIDER_Y 36
#define GUITAR_BODY_Y 38
#define GUITAR_BODY_H 26

static void guitar_ui_divider(Widget* widget) {
    widget_add_line_element(widget, GUITAR_MARGIN, GUITAR_DIVIDER_Y, 128 - GUITAR_MARGIN, GUITAR_DIVIDER_Y);
}

static void guitar_ui_icon_side(Widget* widget, const Icon* icon) {
    if(icon) {
        widget_add_icon_element(widget, GUITAR_ICON_X, GUITAR_ICON_Y_SIDE, icon);
    }
}

static void guitar_ui_icon_top(Widget* widget, const Icon* icon) {
    if(icon) {
        widget_add_icon_element(widget, GUITAR_ICON_X, GUITAR_ICON_Y_TOP, icon);
    }
}

void guitar_ui_show_status(
    Widget* widget,
    const Icon* icon,
    bool success,
    const char* title,
    const char* body) {
    widget_reset(widget);

    guitar_ui_icon_top(widget, icon);

    char title_buf[64];
    if(success) {
        snprintf(title_buf, sizeof(title_buf), "\e#%s\e#", title);
    } else {
        snprintf(title_buf, sizeof(title_buf), "\e!%s\e!", title);
    }
    widget_add_text_box_element(
        widget, GUITAR_TEXT_X, 8, GUITAR_TEXT_W, 14, AlignLeft, AlignTop, title_buf, false);

    guitar_ui_divider(widget);
    widget_add_text_scroll_element(widget, GUITAR_MARGIN, GUITAR_BODY_Y, 120, GUITAR_BODY_H, body);
}

void guitar_ui_show_banner(
    Widget* widget,
    const Icon* icon,
    const char* title,
    const char* subtitle) {
    widget_reset(widget);

    guitar_ui_icon_side(widget, icon);

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "\e#%s\e#\n%s", title, subtitle);
    widget_add_text_box_element(
        widget, GUITAR_TEXT_X, 12, GUITAR_TEXT_W, 40, AlignLeft, AlignTop, buffer, false);
}
