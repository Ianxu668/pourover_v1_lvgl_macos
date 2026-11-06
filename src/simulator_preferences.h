#ifndef SIMULATOR_PREFERENCES_H
#define SIMULATOR_PREFERENCES_H

#include <stdbool.h>
#include "pourover_ui.h"

bool simulator_preferences_parse_locale(const char *tag, const po_ui_locale_t **locale);
bool simulator_preferences_read(const char *path, const po_ui_locale_t **locale);
bool simulator_preferences_write(const char *path, const po_ui_locale_t *locale);

#endif
