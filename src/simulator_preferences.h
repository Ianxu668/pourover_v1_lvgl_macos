#ifndef SIMULATOR_PREFERENCES_H
#define SIMULATOR_PREFERENCES_H

#include <stdbool.h>
#include "pourover_ui.h"

const char *simulator_preferences_locale(po_language_t language);
bool simulator_preferences_parse_locale(const char *locale, po_language_t *language);
bool simulator_preferences_read(const char *path, po_language_t *language);
bool simulator_preferences_write(const char *path, po_language_t language);

#endif
