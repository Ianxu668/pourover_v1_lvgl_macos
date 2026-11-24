#ifndef SIMULATOR_FONTS_H
#define SIMULATOR_FONTS_H

#include "pourover_ui.h"

#ifdef PO_UI_FREETYPE_READOUTS
/* Configure the full UI's FreeType font paths before opening product pages.
 * Call again after po_ui_destroy() or a page-creation failure. */
bool simulator_fonts_init(void);
#else
static inline bool simulator_fonts_init(void) { return true; }
#endif

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
