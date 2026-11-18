#ifndef SIMULATOR_FONTS_H
#define SIMULATOR_FONTS_H

#include "pourover_ui.h"

#ifdef SIMULATOR_SHARED_FREETYPE
bool simulator_show_language(const po_ui_locale_t *locale, po_ui_save_cb save, void *context);
bool simulator_show_ready(const po_ready_view_t *view, const po_ui_locale_t *locale);
/* Destroy the UI and finish rendering before releasing borrowed fonts. */
void simulator_fonts_close(void);
#else
static inline bool simulator_show_language(const po_ui_locale_t *locale, po_ui_save_cb save,
                                           void *context)
{
    return po_ui_show_language(locale, save, context);
}

static inline bool simulator_show_ready(const po_ready_view_t *view, const po_ui_locale_t *locale)
{
    return po_ui_show_ready(view, locale);
}

static inline void simulator_fonts_close(void) {}
#endif

#endif
