#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

#include "lvgl.h"
#include "simulator_fonts.h"

static unsigned flushed;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    ++flushed;
    lv_display_flush_ready(display);
}

static lv_obj_t *find_label(lv_obj_t *parent, const char *text)
{
    if(lv_obj_check_type(parent, &lv_label_class) && strcmp(lv_label_get_text(parent), text) == 0)
        return parent;
    for(uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        lv_obj_t *label = find_label(lv_obj_get_child(parent, i), text);
        if(label) return label;
    }
    return NULL;
}

static void check_glyph(const char *text, uint32_t codepoint)
{
    lv_obj_t *label = find_label(lv_screen_active(), text);
    assert(label);
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_font_glyph_dsc_t glyph;
    assert(lv_font_get_glyph_dsc(font, &glyph, codepoint, 0));
    assert(!glyph.is_placeholder && glyph.resolved_font == font);
    assert(glyph.format == LV_FONT_GLYPH_FORMAT_A8);
    lv_font_glyph_release_draw_data(&glyph);
}

int main(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(960, 376);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    static uint8_t pixels[960 * 16 * 2];
    lv_display_set_buffers(display, pixels, NULL, sizeof pixels, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);

    assert(simulator_fonts_init());

    assert(simulator_show_language(po_ui_locale_find("en"), NULL, NULL));
    assert(lv_obj_find_by_name(lv_screen_active(), "language-continue"));
    lv_refr_now(display);
    assert(flushed);

    assert(simulator_show_ready(&po_ui_ready_baseline, po_ui_locale_find("zh-Hans")));
    check_glyph("g", 'g');
    check_glyph("8", '8');
    check_glyph("容器一", 0x5bb9);
    flushed = 0;
    lv_refr_now(display);
    assert(flushed);

    assert(po_ui_show_settings(NULL, po_ui_locale_find("zh-Hans")));
    check_glyph("设置", 0x8bbe);
    flushed = 0;
    lv_refr_now(display);
    assert(flushed);

    po_ui_destroy();
    lv_draw_wait_for_finish();
    simulator_fonts_close();
    lv_deinit();
    return 0;
}
