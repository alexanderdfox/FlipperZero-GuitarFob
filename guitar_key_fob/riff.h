#pragma once

#include "guitar_types.h"

/** Apply factory-default riff strings for all actions. */
void guitar_riff_defaults(GuitarSettings* settings);

/**
 * Parse riff string: "196", "330+8", or "196,247,330,196".
 * Optional +BEND suffix per note (Hz). Returns false if empty/invalid.
 */
bool guitar_riff_parse(GuitarRiff* out, const char* str);

/** Get stored riff string for an action. */
const char* guitar_riff_string_for_action(const GuitarSettings* settings, GuitarAction action);

/** Writable riff string buffer for an action. */
char* guitar_riff_string_mut_for_action(GuitarSettings* settings, GuitarAction action);

/** Parse the riff configured for a given action. */
bool guitar_riff_for_action(GuitarRiff* out, GuitarAction action, const GuitarSettings* settings);

/** Human-readable action name for UI labels. */
const char* guitar_action_name(GuitarAction action);

/** Short preview of riff string for menus, e.g. "196,247,...". */
void guitar_riff_preview(const GuitarSettings* settings, GuitarAction action, char* buf, size_t buf_len);
