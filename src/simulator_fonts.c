#include <stdio.h>

#include "lvgl.h"
#include "simulator_fonts.h"

static po_ui_language_fonts_t language_fonts;
static po_ui_ready_fonts_t ready_fonts;

static const struct {
    const lv_font_t **slot;
    const char *path;
    uint16_t size, weight, line_height, base_line;
} roles[] = {
#define PO_UI_FREETYPE_ROLE(group, field, family, size, weight, height, baseline) \
    {&group##_fonts.field, SIMULATOR_FREETYPE_ASSETS "/fonts/" #family ".ttf", \
     size, weight, height, baseline},
#include "freetype_roles.inc"
#undef PO_UI_FREETYPE_ROLE
};

void simulator_fonts_close(void)
{
    for(size_t i = 0; i < sizeof roles / sizeof roles[0]; ++i) {
        lv_freetype_font_delete((lv_font_t *)*roles[i].slot);
        *roles[i].slot = NULL;
    }
}

static bool simulator_fonts_open(void)
{
    if(language_fonts.title) return true;

    for(size_t i = 0; i < sizeof roles / sizeof roles[0]; ++i) {
        lv_font_info_t info;
        lv_freetype_init_font_info(&info);
        info.name = roles[i].path;
        info.size = roles[i].size;
        info.weight = roles[i].weight;
        lv_font_t *font = lv_freetype_font_create_with_info(&info);
        if(!font) {
            fprintf(stderr, "Could not load %s at %u px, weight %u.\n", info.name,
                    (unsigned)info.size, (unsigned)info.weight);
            simulator_fonts_close();
            return false;
        }
        font->line_height = roles[i].line_height;
        font->base_line = roles[i].base_line;
        *roles[i].slot = font;
    }
    return true;
}

bool simulator_show_language(const po_ui_locale_t *locale, po_ui_save_cb save, void *context)
{
    return simulator_fonts_open() &&
           po_ui_show_language_with_fonts(locale, save, context, &language_fonts);
}

bool simulator_show_ready(const po_ready_view_t *view, const po_ui_locale_t *locale)
{
    return simulator_fonts_open() && po_ui_show_ready_with_fonts(view, locale, &ready_fonts);
}
