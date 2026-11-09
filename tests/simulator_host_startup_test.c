#define _DEFAULT_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "lvgl/lvgl.h"
#include "po_ready_assets.h"
#include "../src/simulator_product.c"
#include "../src/main.c"

static lv_display_t *display;
static lv_group_t *group;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

static void advance(unsigned ms)
{
    for(unsigned elapsed = 0; elapsed < ms; elapsed += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
}

static void choose_next_language(void)
{
    assert(lv_group_send_data(lv_group_get_default(), LV_KEY_DOWN) == LV_RESULT_OK);
    advance(240);
    assert(po_ui_is_settled());
    assert(po_ui_get_language() == po_ui_locale_find("zh-Hans"));
}

static void confirm_language(void)
{
    lv_obj_t *button = lv_obj_find_by_name(lv_screen_active(), "language-continue");
    assert(button);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
}

static void test_initial_locale_override(char *path)
{
    const po_ui_locale_t *chinese = po_ui_locale_find("zh-Hans");
    assert(simulator_preferences_write(path, chinese));
    char *arguments[] = {"simulator", "--language", "en"};
    simulator_options_t options;
    assert(simulator_options_parse(3, arguments, &options));

    bool saved_valid = false;
    const po_ui_locale_t *selected = select_initial_language(&options, path, &saved_valid);
    assert(saved_valid);
    assert(selected == po_ui_locale_find("en"));
    const po_ui_locale_t *still_saved = NULL;
    assert(simulator_preferences_read(path, &still_saved));
    assert(still_saved == chinese);
}

static void test_screenshot_language_gate(char *path)
{
    char *arguments[] = {"simulator", "--screenshot", "/unused.ppm"};
    simulator_options_t options;
    assert(simulator_options_parse(3, arguments, &options));
    assert(options.screen == SCREEN_AUTO);
    assert(simulator_preferences_write(path, po_ui_locale_find("zh-Hans")));
    bool saved_valid = false;
    assert(select_initial_language(&options, path, &saved_valid) == po_ui_locale_find("zh-Hans"));
    assert(saved_valid && !startup_language_pending(&options, saved_valid));

    FILE *file = fopen(path, "wb");
    assert(file && fputs("ja\n", file) >= 0 && fclose(file) == 0);
    assert(select_initial_language(&options, path, &saved_valid) == po_ui_locale_find("en"));
    assert(!saved_valid && startup_language_pending(&options, saved_valid));
}

static void test_boot_language_gate(char *missing_path)
{
    ready_view = po_ui_ready_baseline;
    po_ui_show_boot_variant(PO_BOOT_H);
    char *arguments[] = {"simulator"};
    simulator_options_t options;
    assert(simulator_options_parse(1, arguments, &options));
    bool language_pending = startup_language_pending(&options, true);
    assert(language_pending);
    bool showing_language = false;
    bool showing_ready = false;
    assert(!po_ui_boot_is_finished());
    assert(startup_dispatch(&language_pending, &showing_language, &showing_ready,
                            po_ui_locale_find("en"), missing_path));
    assert(language_pending && !showing_language && !showing_ready);
    assert(lv_obj_find_by_name(lv_screen_active(), "language-continue") == NULL);

    po_ui_boot_request_exit();
    for(unsigned elapsed = 0; elapsed < 10000 && !po_ui_boot_is_finished(); elapsed += 10)
        advance(10);
    assert(po_ui_boot_is_finished());
    assert(startup_dispatch(&language_pending, &showing_language, &showing_ready,
                            po_ui_locale_find("en"), missing_path));
    assert(!language_pending && showing_language && !showing_ready);
    assert(lv_obj_find_by_name(lv_screen_active(), "language-continue"));
}

static void test_save_failure_stays_on_language(char *missing_path)
{
    assert(po_ui_show_language(po_ui_locale_find("en"), save_language, missing_path));
    bool language_pending = false;
    bool showing_language = true;
    bool showing_ready = false;
    choose_next_language();
    confirm_language();
    assert(!po_ui_is_confirmed());
    assert(startup_dispatch(&language_pending, &showing_language, &showing_ready,
                            po_ui_locale_find("en"), missing_path));
    assert(showing_language && !showing_ready);
    assert(lv_obj_find_by_name(lv_screen_active(), "ready-container-label") == NULL);
}

static void test_selected_locale_reaches_ready(char *path)
{
    assert(simulator_preferences_write(path, po_ui_locale_find("en")));
    ready_view = po_ui_ready_baseline;
    assert(po_ui_show_language(po_ui_locale_find("en"), save_language, path));
    choose_next_language();
    confirm_language();
    assert(po_ui_is_confirmed());

    bool language_pending = false;
    bool showing_language = true;
    bool showing_ready = false;
    assert(startup_dispatch(&language_pending, &showing_language, &showing_ready,
                            po_ui_locale_find("en"), path));
    assert(!showing_language && showing_ready);
    lv_obj_t *container_label = lv_obj_find_by_name(lv_screen_active(), "ready-container-label");
    assert(container_label && lv_obj_check_type(container_label, &lv_image_class));
    assert(lv_image_get_src(container_label) == &po_asset_container_1);
    const po_ui_locale_t *saved = NULL;
    assert(simulator_preferences_read(path, &saved));
    assert(saved == po_ui_locale_find("zh-Hans"));
}

static lv_obj_t *find_image_source(lv_obj_t *object, const void *source)
{
    if(lv_obj_check_type(object, &lv_image_class) && lv_image_get_src(object) == source)
        return object;
    for(uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) {
        lv_obj_t *found = find_image_source(lv_obj_get_child(object, i), source);
        if(found) return found;
    }
    return NULL;
}

static void test_curve_survives_language_handoff(char *path)
{
    ready_view = po_ui_ready_baseline;
    po_ready_params_t params = {
        .timer_mode = PO_READY_TIMER_DOWN,
        .duration_seconds = 180,
        .target_enabled = true,
        .target_grams = 300,
        .curve_enabled = false
    };
    ready_params_changed(&params, NULL);
    assert(po_ui_show_language(po_ui_locale_find("en"), save_language, path));
    confirm_language();
    assert(po_ui_is_confirmed());

    bool language_pending = false;
    bool showing_language = true;
    bool showing_ready = false;
    assert(startup_dispatch(&language_pending, &showing_language, &showing_ready,
                            po_ui_locale_find("en"), path));
    assert(showing_ready);
    assert(find_image_source(lv_screen_active(), &po_asset_param_curve_off));
    assert(!find_image_source(lv_screen_active(), &po_asset_param_curve_on));
}

int main(void)
{
    char directory[] = "/tmp/simulator-host-startup-XXXXXX";
    assert(mkdtemp(directory));
    char path[sizeof(directory) + 16];
    char missing_path[sizeof(directory) + 32];
    assert(snprintf(path, sizeof(path), "%s/language", directory) > 0);
    assert(snprintf(missing_path, sizeof(missing_path), "%s/missing/language", directory) > 0);

    test_initial_locale_override(path);
    test_screenshot_language_gate(path);

    lv_init();
    display = lv_display_create(960, 376);
    assert(display);
    static uint8_t pixels[960 * 40 * 2];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    group = lv_group_create();
    assert(group);
    lv_group_set_default(group);

    test_boot_language_gate(missing_path);
    po_ui_destroy();
    test_save_failure_stays_on_language(missing_path);
    po_ui_destroy();
    test_selected_locale_reaches_ready(path);
    po_ui_destroy();
    test_curve_survives_language_handoff(path);
    po_ui_destroy();

    lv_group_delete(group);
    lv_display_delete(display);
    lv_deinit();
    assert(remove(path) == 0);
    assert(rmdir(directory) == 0);
    puts("simulator host startup: PASS (boot gate, persistence, locale handoff, CLI override)");
    return 0;
}
