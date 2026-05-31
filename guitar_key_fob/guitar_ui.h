#pragma once

#include <gui/modules/widget.h>
#include <gui/icon.h>
#include <stdbool.h>

/** Draw branded TX status screen with icon, divider, and formatted text. */
void guitar_ui_show_status(
    Widget* widget,
    const Icon* icon,
    bool success,
    const char* title,
    const char* body);

/** Draw idle banner with icon in the left column. */
void guitar_ui_show_banner(
    Widget* widget,
    const Icon* icon,
    const char* title,
    const char* subtitle);
