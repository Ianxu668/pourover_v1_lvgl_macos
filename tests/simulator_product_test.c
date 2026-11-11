#define _DEFAULT_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include "lvgl/lvgl.h"
#include "pourover_ui.h"

static bool reject_recipes, reject_cups, reject_language;

static bool set_recipes(const po_m2_recipe_t *recipes, unsigned count)
{
    return !reject_recipes && po_ui_m2_ready_set_recipes(recipes, count);
}

static bool set_cups(const po_m2_cup_t *cups, unsigned count)
{
    return !reject_cups && po_ui_m2_ready_set_cups(cups, count);
}

static bool set_language(const po_ui_locale_t *language)
{
    return !reject_language && po_ui_set_language(language);
}

#define po_ui_m2_ready_set_recipes set_recipes
#define po_ui_m2_ready_set_cups set_cups
#define po_ui_set_language set_language
#include "../src/simulator_product_ready.c"
#include "../src/simulator_product_brew.c"
#include "../src/simulator_product_m2.c"
#include "../src/simulator_product_settings.c"
#include "../src/simulator_product.c"
#undef po_ui_m2_ready_set_recipes
#undef po_ui_m2_ready_set_cups
#undef po_ui_set_language

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

static bool has_visible_label(lv_obj_t *object, const char *text)
{
    if(lv_obj_is_hidden(object)) return false;
    if(lv_obj_check_type(object, &lv_label_class) && strcmp(lv_label_get_text(object), text) == 0)
        return true;
    for(unsigned i = 0; i < lv_obj_get_child_count(object); ++i)
        if(has_visible_label(lv_obj_get_child(object, i), text)) return true;
    return false;
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

/* M1 Brew: a negative reading cuts the weight series at the first negative
 * sample and a later positive sample starts a second drawn segment. */
static void test_m1_brew_curve_break_follows_weight(void)
{
    lv_obj_t *line;
    start_mode1_ready();
    press(SIM_KEY_LEFT);
    assert(product_session.page == SIM_M1_BREW);

    sim_session_add_weight(&product_session, -1420);
    host_brew_sample();
    product_view_dirty = true;
    service();
    line = named("brew-weight-line");
    assert(line && !lv_obj_is_hidden(line));
    /* The run stops before the negative sample: one segment, origin only. */
    assert(*(unsigned *)lv_obj_get_user_data(named("brew-plot")) == 1);

    sim_session_add_weight(&product_session, 2440);
    host_brew_sample();
    product_view_dirty = true;
    service();
    /* The recovered reading re-enters the drawn set as a second segment. */
    assert(*(unsigned *)lv_obj_get_user_data(named("brew-plot")) == 2);
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

static void test_cli_product_previews(void)
{
    static const struct { const char *screen; const char *root; } cases[] = {
        {"ready", "ready-surface"}, {"settings", "settings-root"},
        {"m2-ready", "m2-ready-root"}, {"m2-brew", "m2-brew-root"},
        {"m2-result", "m2-result-root"}, {"result", "result-root"},
        {"container", "container-root"}, {"brew-settings", "brew-settings-root"}
    };
    for(unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        simulator_options_t options;
        char *args[] = {"simulator", "--screen", (char *)cases[i].screen};
        assert(simulator_options_parse(3, args, &options));
        ready_view = po_ui_ready_baseline;
        if(options.page == SIM_M1_READY) assert(po_ui_show_ready(&ready_view, locale));
        assert(start_product(&options, locale, &note_until));
        assert(product_started && product_session.page == options.page);
        assert(named(cases[i].root));
        po_ui_destroy();
    }
}

static void test_cli_m2_selection_survives_refresh(void)
{
    simulator_options_t options;
    char *args[] = {"simulator", "--screen", "m2-ready", "--m2-surface", "pick",
                     "--m2-tab", "history", "--m2-editor", "keypad"};
    simulator_product_init();
    assert(simulator_options_parse(9, args, &options));
    assert(simulator_product_start(&options, locale, &note_until));
    assert(m2_ready_mirror.surface == PO_M2_READY_PICK);
    assert(m2_ready_mirror.pick_tab == PO_M2_PICK_HISTORY);
    assert(m2_ready_mirror.editor == PO_M2_EDITOR_KEYPAD);
    assert(named("m2-ready-cup-use"));
    click("m2-ready-cup-use");
    service();
    assert(m2_ready_mirror.source == PO_M2_SOURCE_HISTORY);
    assert(m2_ready_mirror.dose_decigrams == m2_cups[0].dose_decigrams);
    assert(m2_ready_mirror.target_grams == m2_cups[0].weight_decigrams / 10);

    m2_ready_mirror.surface = PO_M2_READY_PICK;
    assert(po_ui_m2_ready_set(&m2_ready_mirror));
    click("m2-ready-pick-recipe");
    assert(has_visible_label(lv_screen_active(), m2_recipes[0].name));
    click("m2-ready-pick-use");
    service();
    assert(m2_ready_mirror.source == PO_M2_SOURCE_RECIPE);
    assert(strcmp(m2_ready_mirror.recipe_name, m2_recipes[0].name) == 0);
    assert(m2_ready_mirror.recipe_segments == m2_recipes[0].segments);
    assert(m2_ready_mirror.target_grams == m2_recipes[0].total_grams);
    sim_session_add_weight(&product_session, 10);
    service();
    assert(refresh_session());
    assert(has_visible_label(lv_screen_active(), m2_recipes[0].name));
    assert(strcmp(m2_ready_mirror.recipe_name, m2_recipes[0].name) == 0);
    /* The selected name is painted by the next host snapshot, not retained by the page. */
    po_m2_ready_view_t view;
    fill_m2_ready(&view);
    assert(strcmp(view.recipe_name, m2_recipes[0].name) == 0);
    po_ui_destroy();

    char *wheel[] = {"simulator", "--screen", "m2-ready", "--m2-surface", "dose",
                      "--m2-editor", "wheel"};
    assert(simulator_options_parse(7, wheel, &options));
    assert(simulator_product_start(&options, locale, &note_until));
    assert(m2_ready_mirror.editor == PO_M2_EDITOR_WHEEL);
    assert(named("m2-ready-wheel-0"));
    po_ui_destroy();
}

static void test_product_start_propagates_setup_failures(void)
{
    simulator_options_t options;
    char *args[] = {"simulator", "--screen", "m2-ready"};
    assert(simulator_options_parse(3, args, &options));
    reject_language = true;
    assert(!simulator_product_start(&options, locale, &note_until));
    reject_language = false;
    reject_recipes = true;
    assert(!simulator_product_start(&options, locale, &note_until));
    reject_recipes = false;
    reject_cups = true;
    assert(!simulator_product_start(&options, locale, &note_until));
    reject_cups = false;
    assert(!simulator_product_started());
    assert(simulator_product_start(&options, locale, &note_until));
    po_ui_destroy();
}

static void test_cli_settings_shares_session_host(void)
{
    simulator_options_t options;
    char *args[] = {"simulator", "--screen", "settings", "--settings-screen", "volume"};
    assert(simulator_options_parse(5, args, &options));
    settings_mirror_valid = false;
    settings_mirror.volume_percent = 60;
    assert(start_product(&options, locale, &note_until));
    assert(settings_mirror_valid && product_session.page == SIM_SETTINGS);
    assert(settings_mirror.volume_percent == po_ui_settings_baseline.volume_percent);
    assert(settings_mirror.brightness_percent == po_ui_settings_baseline.brightness_percent);
    assert(settings_mirror.auto_off_minutes == po_ui_settings_baseline.auto_off_minutes);
    assert(strcmp(settings_mirror.device_name, po_ui_settings_baseline.device_name) == 0);
    assert(settings_mirror.screen == PO_SET_VOLUME);
    click("settings-volume-plus1");
    service();
    assert(settings_mirror.volume_percent == 61);
    press(SIM_KEY_LEFT);
    service();
    assert(settings_mirror.screen == PO_SET_PREFS);
    press(SIM_KEY_LEFT);
    service();
    assert(settings_mirror.screen == PO_SET_HOME);
    press(SIM_SWIPE_NEXT);
    assert(product_session.page == SIM_M1_READY && named("ready-surface"));
    press(SIM_SWIPE_NEXT);
    press(SIM_SWIPE_NEXT);
    assert(product_session.page == SIM_SETTINGS && settings_mirror.volume_percent == 61);
    po_ui_destroy();
}

static void confirm_history(void)
{
    lv_obj_t *ok = named("settings-confirm-delete");
    assert(ok);
    lv_obj_send_event(ok, LV_EVENT_CLICKED, NULL);
}

static void test_history_confirmation_returns_to_list(void)
{
    start_mode1_ready();
    press(SIM_SWIPE_NEXT);
    press(SIM_SWIPE_NEXT);
    settings_mirror.history_count = 3;
    for(int i = 0; i < 3; ++i) settings_mirror.history[i].cup_index = i + 1;
    on_settings_event(&(po_settings_event_t){PO_SET_ACT_OPEN, PO_SET_HOME, PO_SET_HISTORY}, NULL);
    service();
    on_settings_event(&(po_settings_event_t){PO_SET_ACT_HISTORY_DELETE_ASK, PO_SET_HISTORY, 1}, NULL);
    service();
    assert(named("settings-history-ask"));
    confirm_history();
    assert(settings_mirror.history_count == 2);
    assert(settings_mirror.history[0].cup_index == 1);
    assert(settings_mirror.history[1].cup_index == 3);
    assert(settings_screen_pending && settings_view_dirty);
    assert(named("settings-history-ask")); /* Navigation waits for GUI dispatch. */
    service();
    lv_tick_inc(200); /* Let the modal close animation delete the ask card. */
    lv_timer_handler();
    assert(settings_mirror.screen == PO_SET_HISTORY);
    assert(!named("settings-history-ask"));
    on_settings_event(&(po_settings_event_t){PO_SET_ACT_HISTORY_CLEAR_ASK, PO_SET_HISTORY, 0}, NULL);
    service();
    confirm_history();
    assert(settings_screen_pending && settings_view_dirty);
    service();
    lv_tick_inc(200);
    lv_timer_handler();
    assert(settings_mirror.history_count == 0 && settings_mirror.screen == PO_SET_HISTORY);
    assert(!named("settings-history-ask") && named("settings-history-empty"));
    lv_tick_inc(10001);
    lv_timer_handler();
    service();
    assert(settings_mirror.screen == PO_SET_HISTORY);
    po_ui_destroy();
}

/* Toast expiry after leaving settings updates the mirror without a failed push. */
static void test_settings_toast_expires_off_page(void)
{
    start_mode1_ready();
    press(SIM_SWIPE_NEXT);
    press(SIM_SWIPE_NEXT);
    assert(product_session.page == SIM_SETTINGS);
    settings_mirror.history_toast = true;
    settings_toast_until = 0;
    press(SIM_SWIPE_NEXT);
    assert(product_session.page == SIM_M1_READY);
    assert(settings_service());
    assert(!settings_mirror.history_toast);
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
    simulator_product_init();
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
    test_m1_brew_curve_break_follows_weight();
    test_settings_back_parent_and_return();
    test_cli_product_previews();
    test_cli_m2_selection_survives_refresh();
    test_product_start_propagates_setup_failures();
    test_cli_settings_shares_session_host();
    test_history_confirmation_returns_to_list();
    test_settings_toast_expires_off_page();
    test_ready_tare_clears_container();

    lv_group_delete(group);
    lv_display_delete(display);
    lv_deinit();
    puts("simulator product host: PASS (mirrors, handlers, settings, tare)");
    return 0;
}
