#define _DEFAULT_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include "lvgl/lvgl.h"
#include "../src/main.c"

static lv_display_t *display;
static lv_group_t *group;
static const po_ui_locale_t *locale;
static uint32_t note_until;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

static lv_obj_t *named(const char *name)
{
    return lv_obj_find_by_name(lv_screen_active(), name);
}

/* A hidden parent layer hides its subtree; the object's own flag does not change. */
static bool visible(const char *name)
{
    lv_obj_t *object = named(name);
    while(object != NULL) {
        if(lv_obj_is_hidden(object)) return false;
        object = lv_obj_get_parent(object);
    }
    return true;
}

static void click(const char *name)
{
    lv_obj_t *object = named(name);
    assert(object);
    lv_obj_send_event(object, LV_EVENT_CLICKED, NULL);
}

/* Full host key path: page handler, session transition, presentation, tare side effects. */
static void press(sim_key_t key)
{
    product_handle_key(key, locale, &note_until);
}

/* Pending dirty/pending navigation work that callbacks must not do inline. */
static void service(void)
{
    assert(product_service_pending(locale, &note_until));
}

static void start_mode1_ready(void)
{
    ready_view = po_ui_ready_baseline;
    sim_session_init(&product_session);
    assert(po_ui_show_ready(&ready_view, locale));
    po_ui_ready_set_handlers(ready_params_changed, ready_tared, &note_until);
    ready_restore_params();
    assert(po_ui_ready_set_records(ready_records, 5));
    product_session.net_decigrams = ready_view.net_decigrams;
}

/* M2 Ready: params surface, edited dose/ratio, and ounces survive a weight refresh;
 * the TARE callback zeroes the reading through the serviced refresh. */
static void test_m2_ready_state_survives_refresh(void)
{
    po_m2_ready_view_t view;
    start_mode1_ready();
    ready_view.unit = PO_READY_UNIT_OZ;
    press(SIM_SWIPE_NEXT);
    assert(product_session.page == SIM_M2_READY);
    assert(named("m2-ready-root"));

    click("m2-ready-open-params");
    assert(visible("m2-ready-params"));

    on_m2_ready_event(&(po_m2_ready_event_t){.action = PO_M2_READY_ACT_RATIO,
                                            .surface = PO_M2_READY_PARAMS, .value = 200}, NULL);
    on_m2_ready_event(&(po_m2_ready_event_t){.action = PO_M2_READY_ACT_DOSE,
                                            .surface = PO_M2_READY_PARAMS, .value = 250}, NULL);
    assert(m2_ready_mirror.ratio_tenths == 200);
    assert(m2_ready_mirror.dose_decigrams == 250);

    sim_session_add_weight(&product_session, 10);
    service();
    assert(visible("m2-ready-params"));
    assert(m2_ready_mirror.ratio_tenths == 200);
    assert(m2_ready_mirror.dose_decigrams == 250);

    fill_m2_ready(&view);
    assert(view.ounces == (ready_view.unit == PO_READY_UNIT_OZ));

    /* A full-screen editor defers the dynamic set so the draft and Back path survive. */
    click("m2-ready-row-dose");
    assert(product_key_consumed(SIM_KEY_LEFT));
    sim_session_add_weight(&product_session, 10);
    service();
    assert(product_key_consumed(SIM_KEY_LEFT));
    assert(po_ui_m2_ready_key_left());
    service();
    assert(visible("m2-ready-params"));
    assert(m2_ready_mirror.dose_decigrams == 250);
    assert(m2_ready_mirror.ratio_tenths == 200);
    fill_m2_ready(&view); /* leave MAIN for the next test */
    view.surface = PO_M2_READY_MAIN;
    m2_ready_mirror.surface = PO_M2_READY_MAIN;
    assert(po_ui_m2_ready_set(&view));

    /* TARE reports through the callback; the serviced refresh shows zero. */
    product_session.net_decigrams = 1234;
    on_m2_ready_event(&(po_m2_ready_event_t){.action = PO_M2_READY_TARE,
                                            .surface = PO_M2_READY_MAIN, .value = 0}, NULL);
    service();
    assert(product_session.net_decigrams == 0);
    fill_m2_ready(&view);
    assert(view.net_decigrams == 0);
    po_ui_destroy();
}

/* M2 Brew: surface/dose survive the elapsed refresh; a fresh present resets the
 * transient surface while keeping dose; ounces is propagated. */
static void test_m2_brew_state(void)
{
    po_m2_brew_view_t view;
    start_mode1_ready();
    ready_view.unit = PO_READY_UNIT_OZ;
    press(SIM_SWIPE_NEXT);
    press(SIM_KEY_LEFT);
    assert(product_session.page == SIM_M2_BREW);
    assert(named("m2-brew-root"));
    assert(m2_brew_mirror.surface == PO_M2_BREW_MAIN);

    on_m2_brew_event(&(po_m2_brew_event_t){.action = PO_M2_BREW_SURFACE,
                                          .surface = PO_M2_BREW_CURVE, .value = 0}, NULL);
    sim_session_tick_brew(&product_session);
    assert(refresh_session()); /* the per-second brew tick */
    assert(visible("m2-brew-curve-back"));

    on_m2_brew_event(&(po_m2_brew_event_t){.action = PO_M2_BREW_ACT_DOSE,
                                          .surface = PO_M2_BREW_MAIN, .value = 220}, NULL);
    sim_session_tick_brew(&product_session);
    service();
    assert(m2_brew_mirror.dose_decigrams == 220);
    assert(m2_brew_mirror.surface == PO_M2_BREW_MAIN);

    fill_m2_brew(&view);
    assert(view.ounces == true);
    assert(view.dose_decigrams == 220);
    assert(view.elapsed_seconds == product_session.elapsed_seconds);
    po_ui_destroy();
}

/* M2 Result: no prompt on entry; SAVE_RECIPE closes the menu and stays; the
 * long-brew save choice closes the prompt and stays; bypass recomputes the
 * total; CONTINUE returns to M2 brew; a fresh present resets transient state. */
static void test_m2_result_flows(void)
{
    po_m2_result_view_t view;
    start_mode1_ready();
    ready_view.unit = PO_READY_UNIT_OZ;
    press(SIM_SWIPE_NEXT);
    press(SIM_KEY_LEFT);
    press(SIM_KEY_LEFT);
    assert(product_session.page == SIM_M2_RESULT);
    assert(named("m2-result-root"));

    /* Ordinary result: no save prompt, no countdown. */
    fill_m2_result(&view);
    assert(!view.save_open);
    assert(view.save_seconds_left == -1);
    assert(!visible("m2-result-save"));

    click("m2-result-open-menu");
    service();
    assert(visible("m2-result-save-recipe"));

    /* Save as recipe closes the menu and remains on the result. */
    on_m2_result_event(&(po_m2_result_event_t){.action = PO_M2_RESULT_SAVE_RECIPE,
                                              .surface = PO_M2_RESULT_MENU, .value = 0}, NULL);
    service();
    assert(product_session.page == SIM_M2_RESULT);
    assert(m2_result_mirror.surface == PO_M2_RESULT_MAIN);
    assert(!m2_result_mirror.save_open);
    assert(!visible("m2-result-save-recipe"));

    /* Manually opened host prompt: SAVE_YES closes it and stays. */
    m2_result_mirror.save_open = true;
    m2_result_mirror.surface = PO_M2_RESULT_SAVE;
    product_view_dirty = true;
    service();
    assert(visible("m2-result-save"));
    on_m2_result_event(&(po_m2_result_event_t){.action = PO_M2_RESULT_SAVE_YES,
                                              .surface = PO_M2_RESULT_SAVE, .value = 0}, NULL);
    service();
    assert(product_session.page == SIM_M2_RESULT);
    assert(!m2_result_mirror.save_open);
    assert(!visible("m2-result-save"));

    /* Bypass commit recomputes the total and clears the live reading. */
    product_session.net_decigrams = 2000;
    on_m2_result_event(&(po_m2_result_event_t){.action = PO_M2_RESULT_BYPASS_COMMIT,
                                              .surface = PO_M2_RESULT_BYPASS, .value = 300}, NULL);
    service();
    assert(m2_result_mirror.bypass_live_decigrams == 0);
    assert(m2_result_mirror.bypass_post_decigrams == 300);
    assert(m2_result_mirror.total_decigrams == 2300); /* water 2000 + pre 0 + post 300 */
    fill_m2_result(&view);
    assert(view.total_decigrams == 2300);
    assert(m2_result_mirror.surface == PO_M2_RESULT_MAIN);

    /* Cancel clears the live reading without changing the confirmed total. */
    m2_result_mirror.bypass_live_decigrams = 500;
    on_m2_result_event(&(po_m2_result_event_t){.action = PO_M2_RESULT_BYPASS_CANCEL,
                                              .surface = PO_M2_RESULT_MAIN, .value = 0}, NULL);
    service();
    assert(m2_result_mirror.bypass_live_decigrams == 0);
    assert(m2_result_mirror.bypass_post_decigrams == 300);
    assert(m2_result_mirror.total_decigrams == 2300);

    /* 返回继续 goes back to the running brew, not Ready. */
    on_m2_result_event(&(po_m2_result_event_t){.action = PO_M2_RESULT_CONTINUE,
                                              .surface = PO_M2_RESULT_MAIN, .value = 0}, NULL);
    service();
    assert(product_session.page == SIM_M2_BREW);
    assert(named("m2-brew-root"));
    assert(m2_brew_mirror.surface == PO_M2_BREW_MAIN); /* fresh present reset */
    assert(m2_brew_mirror.dose_decigrams == 220);      /* dose preference retained */

    /* Re-entering the result after the earlier dialog: transient state reset. */
    press(SIM_KEY_LEFT);
    assert(product_session.page == SIM_M2_RESULT);
    assert(!m2_result_mirror.save_open);
    assert(m2_result_mirror.surface == PO_M2_RESULT_MAIN);
    assert(m2_result_mirror.bypass_live_decigrams == 0);
    po_ui_destroy();
}

/* M1 Result: no automatic save prompt or auto-return; host callbacks close an
 * already-open prompt and remain on the result. */
static void test_m1_result_save_and_discard(void)
{
    result_view_t rview;
    start_mode1_ready();
    press(SIM_KEY_LEFT);
    press(SIM_KEY_LEFT);
    assert(product_session.page == SIM_M1_RESULT);
    assert(named("result-root"));

    fill_m1_result(&rview);
    assert(!rview.save_open);
    assert(rview.save_seconds_left == -1);
    assert(!visible("result-save"));

    /* Servicing must not navigate away on its own. */
    service();
    service();
    assert(product_session.page == SIM_M1_RESULT);

    m1_result_save_open = true; /* host-opened prompt */
    product_view_dirty = true;
    service();
    assert(visible("result-save"));
    fill_m1_result(&rview);
    assert(rview.save_seconds_left == 30);

    on_m1_result_save(NULL);
    service();
    assert(product_session.page == SIM_M1_RESULT);
    fill_m1_result(&rview);
    assert(!rview.save_open);
    assert(!visible("result-save"));

    m1_result_save_open = true;
    on_m1_result_discard(NULL);
    service();
    assert(product_session.page == SIM_M1_RESULT);
    assert(!m1_result_save_open);
    assert(!visible("result-save"));
    po_ui_destroy();
}

/* M1 Brew: target sheet and target toggle survive the per-second refresh. */
static void test_m1_brew_target_sheet_survives(void)
{
    start_mode1_ready();
    press(SIM_KEY_LEFT);
    assert(product_session.page == SIM_M1_BREW);
    assert(named("brew-root"));
    assert(!visible("brew-target-sheet"));

    on_m1_brew_target(NULL);
    service();
    assert(visible("brew-target-sheet"));

    on_m1_brew_target_toggle(NULL);
    assert(!ready_view.show_target);

    sim_session_tick_brew(&product_session);
    service();
    assert(visible("brew-target-sheet"));
    assert(!ready_view.show_target);
    on_m1_brew_target_toggle(NULL);
    assert(ready_view.show_target);
    po_ui_destroy();
}

/* Settings: BACK pops to the parent screen; home left falls through to the session. */
static void test_settings_back_parent_and_return(void)
{
    start_mode1_ready();
    press(SIM_SWIPE_NEXT);
    press(SIM_SWIPE_NEXT);
    assert(product_session.page == SIM_SETTINGS);
    assert(named("settings-root"));
    assert(settings_parent_screen(PO_SET_BREW) == PO_SET_OTHER);
    assert(settings_parent_screen(PO_SET_CONTAINER) == PO_SET_OTHER);
    assert(settings_parent_screen(PO_SET_CALIBRATE) == PO_SET_OTHER);
    assert(settings_parent_screen(PO_SET_ABOUT) == PO_SET_OTHER);
    assert(settings_parent_screen(PO_SET_RESET) == PO_SET_OTHER);

    click("settings-prefs");
    service();
    assert(visible("settings-back"));
    assert(settings_mirror.screen == PO_SET_PREFS);

    on_settings_event(&(po_settings_event_t){.action = PO_SET_ACT_OPEN,
                                            .screen = PO_SET_PREFS, .value = PO_SET_UNIT}, NULL);
    service();
    click("settings-unit-oz");
    assert(settings_mirror.ounces == true);
    assert(ready_view.unit == PO_READY_UNIT_OZ);
    on_settings_event(&(po_settings_event_t){.action = PO_SET_ACT_LANGUAGE,
                                            .screen = PO_SET_UNIT, .value = 1}, NULL);
    const po_ui_locale_t *chosen = po_ui_locale_at(1);
    assert(settings_mirror.language == chosen);

    /* Back from UNIT pops to PREFS, not HOME. */
    assert(po_ui_settings_key_left());
    service();
    assert(settings_mirror.screen == PO_SET_PREFS);
    assert(visible("settings-back"));

    /* Second back reaches HOME; home left falls through to the session. */
    assert(po_ui_settings_key_left());
    service();
    assert(settings_mirror.screen == PO_SET_HOME);
    assert(visible("settings-prefs"));
    assert(named("settings-back") == NULL);

    assert(!po_ui_settings_key_left()); /* home does not consume */
    press(SIM_KEY_LEFT);
    assert(product_session.page == SIM_M2_READY); /* return_ready from the swipe */
    press(SIM_SWIPE_NEXT);
    assert(product_session.page == SIM_SETTINGS);
    assert(settings_mirror.language == chosen);
    po_ui_destroy();
}

/* Ready X tare zeroes like the T key: container cleared, net zero, TARED note. */
static void test_ready_tare_clears_container(void)
{
    start_mode1_ready();
    ready_view.container = PO_READY_CONTAINER_MATCHED;
    ready_view.container_id = 1;
    ready_view.net_decigrams = 1234;
    product_session.net_decigrams = 1234;
    press(SIM_KEY_RIGHT);
    assert(product_session.net_decigrams == 0);
    assert(ready_view.container == PO_READY_CONTAINER_NONE);
    assert(ready_view.net_decigrams == 0);
    assert(ready_view.note == PO_READY_NOTE_TARED);
    po_ui_destroy();
}

int main(void)
{
    locale = po_ui_locale_find("en");
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

    test_m2_ready_state_survives_refresh();
    test_m2_brew_state();
    test_m2_result_flows();
    test_m1_result_save_and_discard();
    test_m1_brew_target_sheet_survives();
    test_settings_back_parent_and_return();
    test_ready_tare_clears_container();

    lv_group_delete(group);
    lv_display_delete(display);
    lv_deinit();
    puts("simulator product host: PASS (mirrors, handlers, settings, tare)");
    return 0;
}
