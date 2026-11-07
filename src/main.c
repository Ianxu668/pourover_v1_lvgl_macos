#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "lvgl/lvgl.h"
#include "hal/hal.h"
#include "hal/frame_stats.h"
#include "pourover_ui.h"
#include "simulator_capture.h"
#include "simulator_options.h"
#include "simulator_preferences.h"
#include "simulator_start.h"
#if defined(PO_UI_RESOURCE_PROTOTYPE) && !defined(SIMULATOR_READY_DEBUG_TEST) && \
    !defined(SIMULATOR_HOST_STARTUP_TEST) && !defined(SIMULATOR_PRODUCT_TEST)
#include "po_resource_pack.h"
#endif

#define SIMULATOR_WIDTH 960
#define SIMULATOR_HEIGHT 376

static char *get_preferences_file_path(void)
{
    char *directory = SDL_GetPrefPath("Mistro", "PourOverScale");
    if(directory == NULL) {
        fprintf(stderr, "Could not locate simulator preferences: %s\n", SDL_GetError());
        return NULL;
    }
    size_t directory_length = strlen(directory);
    bool has_separator = directory_length > 0 &&
                         (directory[directory_length - 1] == '/' ||
                          directory[directory_length - 1] == '\\');
    size_t separator_length = has_separator ? 0u : 1u;
    if(directory_length > SIZE_MAX - separator_length - sizeof("language")) {
        SDL_free(directory);
        fprintf(stderr, "Simulator preference path is too long.\n");
        return NULL;
    }
    size_t path_length = directory_length + separator_length + sizeof("language");
    char *path = malloc(path_length);
    if(path != NULL) snprintf(path, path_length, "%s%slanguage", directory, has_separator ? "" : "/");
    SDL_free(directory);
    if(path == NULL) fprintf(stderr, "Could not allocate simulator preference path.\n");
    return path;
}

static po_ready_view_t ready_view;
static bool ready_curve = true;

static int clamp_ready(int value, int low, int high)
{
    if(value < low) return low;
    if(value > high) return high;
    return value;
}

static void ready_note(po_ready_note_t note, uint32_t *until)
{
    ready_view.note = note;
    *until = SDL_GetTicks() + 1600u;
}

static const po_ready_record_t ready_records[] = {
    {11, 3, 3, 149, 2520, 1},
    {11, 3, 2, 185, 3012, 2},
    {11, 3, 1, 165, 2800, 1},
    {11, 2, 2, 140, 2600, 2},
    {11, 2, 1, 180, 3000, 1}
};

static void ready_params_changed(const po_ready_params_t *params, void *context)
{
    (void)context;
    ready_view.show_preset = params->timer_mode == PO_READY_TIMER_DOWN;
    ready_view.preset_seconds = params->duration_seconds;
    ready_view.show_target = params->target_enabled;
    if(params->target_grams > 0) ready_view.target_grams = params->target_grams;
    ready_curve = params->curve_enabled;
}

static void ready_restore_params(void)
{
    const po_ready_params_t params = {
        .timer_mode = ready_view.show_preset ? PO_READY_TIMER_DOWN : PO_READY_TIMER_UP,
        .duration_seconds = ready_view.preset_seconds,
        .target_enabled = ready_view.show_target,
        .target_grams = ready_view.target_grams,
        .curve_enabled = ready_curve
    };
    (void)po_ui_ready_set_params(&params);
}

static void ready_tared(void *context)
{
    uint32_t *note_until = context;
    ready_view.net_decigrams = 0;
    ready_view.container = PO_READY_CONTAINER_NONE;
    ready_note(PO_READY_NOTE_TARED, note_until);
    po_ui_ready_set(&ready_view);
}

static bool ready_edge(const uint8_t *now, const uint8_t *was, int count, SDL_Scancode code)
{
    return code < count && now[code] && !was[code];
}

static bool ready_apply(const uint8_t *now, int count, uint32_t *note_until)
{
    static uint8_t was[SDL_NUM_SCANCODES];
    bool changed = false;
    if(ready_edge(now, was, count, SDL_SCANCODE_C)) {
        ready_view.connected = !ready_view.connected;
        ready_note(ready_view.connected ? PO_READY_NOTE_LINKED : PO_READY_NOTE_UNLINKED, note_until);
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_H)) {
        ready_view.charging = !ready_view.charging;
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_LEFTBRACKET)) {
        ready_view.battery_percent = clamp_ready(ready_view.battery_percent - 10, 0, 100);
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_RIGHTBRACKET)) {
        ready_view.battery_percent = clamp_ready(ready_view.battery_percent + 10, 0, 100);
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_T)) {
        ready_view.net_decigrams = 0;
        ready_view.container = PO_READY_CONTAINER_NONE;
        ready_note(PO_READY_NOTE_TARED, note_until);
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_A)) {
        ready_view.container = PO_READY_CONTAINER_MATCHED;
        ready_view.container_id = 1;
        ready_view.tare_decigrams = 2004;
        ready_view.net_decigrams = 0;
        ready_view.note = PO_READY_NOTE_NONE;
        *note_until = 0;
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_N)) {
        ready_view.container = PO_READY_CONTAINER_UNMATCHED;
        ready_view.tare_decigrams = 2004;
        ready_view.net_decigrams = 0;
        ready_view.note = PO_READY_NOTE_NONE;
        *note_until = 0;
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_O)) {
        ready_view.over_range = !ready_view.over_range;
        ready_view.note = PO_READY_NOTE_NONE;
        *note_until = 0;
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_R)) {
        ready_view.over_range = true;
        ready_note(PO_READY_NOTE_REDUCE, note_until);
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_U)) {
        /* Relabel only. 180 tenths draws as 18.0 g or 1.80 oz; the host converts. */
        ready_view.unit = ready_view.unit == PO_READY_UNIT_OZ ? PO_READY_UNIT_GRAM : PO_READY_UNIT_OZ;
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_UP)) {
        ready_view.net_decigrams = clamp_ready(ready_view.net_decigrams + 10, -99999, 99999);
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_DOWN)) {
        ready_view.net_decigrams = clamp_ready(ready_view.net_decigrams - 10, -99999, 99999);
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_RIGHT)) {
        if(!po_ui_ready_key_right()) {
            ready_view.tare_decigrams = clamp_ready(ready_view.tare_decigrams + 10, 0, 99999);
            changed = true;
        }
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_LEFT)) {
        if(!po_ui_ready_key_left()) {
            ready_view.tare_decigrams = clamp_ready(ready_view.tare_decigrams - 10, 0, 99999);
            changed = true;
        }
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_1)) {
        ready_view.show_target = ready_view.show_preset = true;
        ready_view.container = PO_READY_CONTAINER_MATCHED;
        ready_view.container_id = 1;
        ready_view.over_range = false;
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_2)) {
        ready_view.show_target = ready_view.show_preset = false;
        ready_view.container = PO_READY_CONTAINER_NONE;
        ready_view.over_range = false;
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_3)) {
        ready_view.show_target = ready_view.show_preset = true;
        ready_view.container = PO_READY_CONTAINER_NONE;
        ready_view.net_decigrams = -2000;
        ready_view.over_range = false;
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_4)) {
        ready_view.show_target = ready_view.show_preset = false;
        ready_view.container = PO_READY_CONTAINER_MATCHED;
        ready_view.container_id = 1;
        ready_view.net_decigrams = 0;
        ready_view.over_range = false;
        changed = true;
    }
    if(ready_view.note != PO_READY_NOTE_NONE && *note_until != 0 &&
       (int32_t)(SDL_GetTicks() - *note_until) >= 0) {
        ready_view.note = PO_READY_NOTE_NONE;
        *note_until = 0;
        changed = true;
    }
    if(count > 0) memcpy(was, now, (size_t)count);
    return changed;
}

static bool ready_debug(uint32_t *note_until)
{
    int count = 0;
    const uint8_t *now = SDL_GetKeyboardState(&count);
    if(count > SDL_NUM_SCANCODES) count = SDL_NUM_SCANCODES;
    return ready_apply(now, count, note_until);
}

static bool save_language(const po_ui_locale_t *locale, void *context)
{
    const char *path = context;
    if(!simulator_preferences_write(path, locale)) {
        fprintf(stderr, "Could not save language preference%s%s.\n",
                locale == NULL ? "" : " for ", locale == NULL ? "" : locale->tag);
        return false;
    }
    return true;
}

static const po_ui_locale_t *select_initial_language(const simulator_options_t *options,
                                              const char *preferences_path,
                                              bool *saved_language_valid)
{
    const po_ui_locale_t *language = po_ui_locale_find("en");
    *saved_language_valid = preferences_path != NULL &&
                            simulator_preferences_read(preferences_path, &language);
    const char *origin = "English fallback";
    if(*saved_language_valid) origin = "saved preference";
    else language = po_ui_locale_find("en");
    if(options->language_set) {
        language = options->language;
        origin = "--language override";
    }
    printf("Initial language: %s (origin: %s).\n", language->tag, origin);
    return language;
}

static bool startup_language_pending(const simulator_options_t *options, bool saved_language_valid)
{
    /* Interactive launches always choose a language; automatic screenshots with
       a saved locale hold the boot logo instead. */
    return options->screen == SCREEN_AUTO &&
           (options->screenshot_path == NULL || !saved_language_valid);
}

static bool startup_dispatch(bool *language_pending, bool *showing_language,
                             bool *showing_ready, const po_ui_locale_t *initial,
                             char *preferences_path)
{
    if(*language_pending && po_ui_boot_is_finished()) {
        *language_pending = false;
        *showing_language = true;
        if(!po_ui_show_language(initial, save_language, preferences_path)) {
            fprintf(stderr, "Could not create the language page.\n");
            return false;
        }
    }
    if(*showing_language && po_ui_is_confirmed()) {
        const po_ui_locale_t *selected_language = po_ui_get_language();
        *showing_language = false;
        *showing_ready = true;
        if(!po_ui_show_ready(&ready_view, selected_language)) {
            fprintf(stderr, "Could not create mode 1 ready.\n");
            return false;
        }
        ready_restore_params();
    }
    return true;
}

#ifndef SIMULATOR_READY_DEBUG_TEST
#ifndef SIMULATOR_HOST_STARTUP_TEST
#include "simulator_session.h"

static sim_session_t product_session;
static uint8_t product_was[SDL_NUM_SCANCODES];
static uint32_t product_brew_tick;
static bool product_started;

static bool refresh_session(void);
static bool present_session(const po_ui_locale_t *locale, uint32_t *note_until);
static void product_handle_key(sim_key_t key, const po_ui_locale_t *locale, uint32_t *note_until);

#define HOST_BREW_CAP 48
static brew_sample_t host_brew[HOST_BREW_CAP];
static unsigned host_brew_count;
static int host_brew_origin;
static int host_brew_last;

static void host_brew_begin(void)
{
    host_brew_count = 0;
    host_brew_origin = product_session.net_decigrams;
    host_brew_last = host_brew_origin;
}

static void host_brew_sample(void)
{
    int weight = product_session.net_decigrams;
    int flow = weight - host_brew_last;
    if(flow < 0) flow = 0;
    if(host_brew_count == 0) {
        host_brew[0].weight_decigrams = host_brew_origin;
        host_brew[0].flow_decigrams = 0;
        host_brew_count = 1;
    }
    host_brew_last = weight;
    if(host_brew_count >= HOST_BREW_CAP) {
        if(host_brew_count > 1)
            memmove(&host_brew[1], &host_brew[2], (host_brew_count - 2u) * sizeof(host_brew[0]));
        if(host_brew_count > 1) host_brew_count--;
    }
    if(host_brew_count >= HOST_BREW_CAP) return;
    host_brew[host_brew_count].weight_decigrams = weight;
    host_brew[host_brew_count].flow_decigrams = flow;
    host_brew_count++;
}

static void host_brew_follow(void)
{
    int weight = product_session.net_decigrams;
    int previous;
    int flow;
    if(host_brew_count == 0) return;
    previous = host_brew_count > 1 ? host_brew[host_brew_count - 2u].weight_decigrams : weight;
    flow = weight - previous;
    if(flow < 0) flow = 0;
    host_brew[host_brew_count - 1u].weight_decigrams = weight;
    host_brew[host_brew_count - 1u].flow_decigrams = flow;
    host_brew_last = weight;
}

/* Page-local state lives in host mirrors; dynamic session data is filled per call.
 * Callbacks only mutate the mirrors and set pending flags; the host loop services
 * them outside LVGL event context. */

static bool m1_brew_target_sheet;
static bool m1_result_save_open;
static po_m2_ready_view_t m2_ready_mirror;
static bool m2_ready_mirror_valid;
static po_m2_brew_view_t m2_brew_mirror;
static bool m2_brew_mirror_valid;
static po_m2_result_view_t m2_result_mirror;
static bool m2_result_mirror_valid;
static po_settings_view_t settings_mirror;
static bool settings_mirror_valid;
static bool product_view_dirty;
static bool product_inject_valid;
static sim_key_t product_inject_key;
static bool product_pending_page_valid;
static sim_page_t product_pending_page;
static bool settings_screen_pending;
static int settings_pending_screen;

static void fill_m1_brew(brew_view_t *view)
{
    memset(view, 0, sizeof(*view));
    view->elapsed_seconds = product_session.elapsed_seconds;
    view->net_decigrams = product_session.net_decigrams;
    view->flow_decigrams = host_brew_count ? host_brew[host_brew_count - 1u].flow_decigrams : 0;
    view->show_curve = ready_curve;
    view->unlocked = true;
    view->target_enabled = ready_view.show_target;
    view->target_grams = ready_view.target_grams;
    view->target_sheet = m1_brew_target_sheet;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    view->sample_count = host_brew_count;
    view->samples = host_brew_count ? host_brew : NULL;
}

static void fill_m1_result(result_view_t *view)
{
    memset(view, 0, sizeof(*view));
    view->elapsed_seconds = (int)product_session.elapsed_seconds;
    view->total_water_decigrams = product_session.net_decigrams;
    view->target_grams = ready_view.show_target ? ready_view.target_grams : 0;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    view->save_open = m1_result_save_open;
    view->save_seconds_left = m1_result_save_open ? 30 : -1;
}

static void fill_m2_ready(po_m2_ready_view_t *view)
{
    *view = m2_ready_mirror;
    view->net_decigrams = product_session.net_decigrams;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
}

static void fill_m2_brew(po_m2_brew_view_t *view)
{
    *view = m2_brew_mirror;
    view->elapsed_seconds = product_session.elapsed_seconds;
    view->net_decigrams = product_session.net_decigrams;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    view->sample_count = 0;
    view->samples = NULL;
}

static void fill_m2_result(po_m2_result_view_t *view)
{
    *view = m2_result_mirror;
    view->elapsed_seconds = (int)product_session.elapsed_seconds;
    view->water_decigrams = product_session.net_decigrams;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    view->sample_count = 0;
    view->samples = NULL;
}

static void product_inject(sim_key_t key)
{
    product_inject_key = key;
    product_inject_valid = true;
}

static void on_m1_brew_target(void *context)
{
    (void)context;
    m1_brew_target_sheet = true;
    product_view_dirty = true;
}

static void on_m1_brew_target_toggle(void *context)
{
    (void)context;
    ready_view.show_target = !ready_view.show_target;
    product_view_dirty = true;
}

static void on_m1_result_save(void *context)
{
    (void)context;
    /* Only close a host-opened prompt; ordinary results never show one and the
       result stays on screen. */
    m1_result_save_open = false;
    product_view_dirty = true;
}

static void on_m1_result_discard(void *context)
{
    (void)context;
    m1_result_save_open = false;
    product_view_dirty = true;
}

static bool m2_ready_editor_open(const po_m2_ready_view_t *view)
{
    /* Full-screen editors keep their draft in the page; a set would reset the
       parent surface and close them, so dynamic updates wait. */
    return view->surface != PO_M2_READY_MAIN && view->surface != PO_M2_READY_PARAMS;
}

static void on_m2_ready_event(const po_m2_ready_event_t *event, void *context)
{
    (void)context;
    m2_ready_mirror.surface = event->surface;
    switch(event->action) {
    case PO_M2_READY_AUTOSTART:
        m2_ready_mirror.autostart = event->value != 0;
        break;
    case PO_M2_READY_ACT_DOSE:
        m2_ready_mirror.dose_decigrams = event->value;
        m2_ready_mirror.dose_origin = PO_M2_DOSE_MANUAL;
        m2_ready_mirror.detect_popup = false;
        break;
    case PO_M2_READY_ACT_RATIO:
        m2_ready_mirror.ratio_tenths = event->value;
        break;
    case PO_M2_READY_ACT_BYPASS:
        m2_ready_mirror.bypass_decigrams = event->value;
        break;
    case PO_M2_READY_SOURCE:
        m2_ready_mirror.source = (po_m2_source_t)event->value;
        break;
    case PO_M2_READY_RECIPE:
        /* The page owns the copied recipe name; the host keeps the mirror's. */
        break;
    case PO_M2_READY_TARE:
        product_session.net_decigrams = 0;
        product_view_dirty = true;
        break;
    default:
        break;
    }
}

static void on_m2_brew_event(const po_m2_brew_event_t *event, void *context)
{
    (void)context;
    m2_brew_mirror.surface = event->surface;
    switch(event->action) {
    case PO_M2_BREW_ACT_DOSE:
        m2_brew_mirror.dose_decigrams = event->value;
        break;
    case PO_M2_BREW_CONTINUE:
        m2_brew_mirror.duration_left = -1;
        product_view_dirty = true;
        break;
    case PO_M2_BREW_FINISH:
        product_inject(SIM_KEY_LEFT);
        break;
    default:
        break;
    }
}

static void on_m2_result_event(const po_m2_result_event_t *event, void *context)
{
    (void)context;
    m2_result_mirror.surface = event->surface;
    switch(event->action) {
    case PO_M2_RESULT_SAVE_RECIPE:
        /* Save as new recipe: close the menu and remain on the result. */
        m2_result_mirror.surface = PO_M2_RESULT_MAIN;
        m2_result_mirror.save_open = false;
        product_view_dirty = true;
        break;
    case PO_M2_RESULT_SAVE_YES:
    case PO_M2_RESULT_SAVE_NO:
        /* Long-brew history choice: close the prompt and stay on the result. */
        m2_result_mirror.save_open = false;
        m2_result_mirror.save_seconds_left = -1;
        m2_result_mirror.surface = PO_M2_RESULT_MAIN;
        product_view_dirty = true;
        break;
    case PO_M2_RESULT_CONTINUE:
        /* 返回继续 resumes the running brew. */
        m2_result_mirror.save_open = false;
        m2_result_mirror.surface = PO_M2_RESULT_MAIN;
        product_pending_page = SIM_M2_BREW;
        product_pending_page_valid = true;
        break;
    case PO_M2_RESULT_BYPASS_COMMIT:
        m2_result_mirror.bypass_post_decigrams = event->value;
        m2_result_mirror.bypass_live_decigrams = 0;
        m2_result_mirror.total_decigrams = product_session.net_decigrams +
                                           m2_result_mirror.bypass_pre_decigrams +
                                           m2_result_mirror.bypass_post_decigrams;
        m2_result_mirror.surface = PO_M2_RESULT_MAIN;
        product_view_dirty = true;
        break;
    case PO_M2_RESULT_BYPASS_CANCEL:
        m2_result_mirror.bypass_live_decigrams = 0;
        break;
    default:
        break;
    }
}

static po_settings_screen_t settings_parent_screen(po_settings_screen_t screen)
{
    switch(screen) {
    case PO_SET_HISTORY_DETAIL:
    case PO_SET_HISTORY_DELETE:
    case PO_SET_HISTORY_CLEAR: return PO_SET_HISTORY;
    case PO_SET_VOLUME:
    case PO_SET_BRIGHTNESS:
    case PO_SET_UNIT:
    case PO_SET_LANGUAGE: return PO_SET_PREFS;
    case PO_SET_WAKE:
    case PO_SET_AUTO_OFF:
    case PO_SET_BREW:
    case PO_SET_CONTAINER:
    case PO_SET_CALIBRATE:
    case PO_SET_ABOUT:
    case PO_SET_RESET: return PO_SET_OTHER;
    case PO_SET_CONTAINER_EDIT: return PO_SET_CONTAINER;
    default: return PO_SET_HOME;
    }
}

static void on_settings_event(const po_settings_event_t *event, void *context)
{
    (void)context;
    switch(event->action) {
    case PO_SET_ACT_OPEN:
        settings_pending_screen = event->value;
        settings_screen_pending = true;
        break;
    case PO_SET_ACT_BACK:
        settings_pending_screen = settings_parent_screen(event->screen);
        settings_screen_pending = true;
        break;
    case PO_SET_ACT_UNIT:
        settings_mirror.ounces = event->value != 0;
        ready_view.unit = settings_mirror.ounces ? PO_READY_UNIT_OZ : PO_READY_UNIT_GRAM;
        break;
    case PO_SET_ACT_LANGUAGE: {
        const po_ui_locale_t *chosen = po_ui_locale_at((unsigned)event->value);
        if(chosen != NULL) settings_mirror.language = chosen;
        break;
    }
    case PO_SET_ACT_VOLUME: settings_mirror.volume_percent = event->value; break;
    case PO_SET_ACT_BRIGHTNESS: settings_mirror.brightness_percent = event->value; break;
    case PO_SET_ACT_AUTO_OFF: settings_mirror.auto_off_minutes = event->value; break;
    case PO_SET_ACT_DURATION:
        settings_mirror.duration_seconds = event->value;
        settings_mirror.countdown = true;
        break;
    case PO_SET_ACT_BREW_TAB: settings_mirror.brew_tab = event->value; break;
    case PO_SET_ACT_COUNTDOWN: settings_mirror.countdown = event->value != 0; break;
    case PO_SET_ACT_CLEAR_ON_START: settings_mirror.clear_on_start = event->value != 0; break;
    case PO_SET_ACT_WAKE: settings_mirror.cup_wake = event->value != 0; break;
    case PO_SET_ACT_CURVE: settings_mirror.curve_enabled = event->value != 0; break;
    case PO_SET_ACT_M1_TARE: settings_mirror.m1_auto_tare = event->value != 0; break;
    case PO_SET_ACT_AUTOSTART: settings_mirror.autostart = event->value != 0; break;
    case PO_SET_ACT_DOSE_DETECT: settings_mirror.dose_detect = event->value != 0; break;
    case PO_SET_ACT_FILTER: settings_mirror.filter_detect = event->value != 0; break;
    case PO_SET_ACT_M2_TARE: settings_mirror.m2_auto_tare = event->value != 0; break;
    default:
        /* History, container, calibration and reset have no simulator backend. */
        break;
    }
}

static bool settings_service(void)
{
    if(!settings_screen_pending) return true;
    settings_screen_pending = false;
    settings_mirror.screen = (po_settings_screen_t)settings_pending_screen;
    return po_ui_settings_set(&settings_mirror);
}

static void product_navigate(sim_key_t key, const po_ui_locale_t *locale, uint32_t *note_until)
{
    /* Host-initiated navigation after a page action (save, discard, finish,
     * continue): the page already handled its own state, so the key is applied
     * to the session without offering it to the page again. */
    sim_key_result_t result = sim_session_key(&product_session, key, false);
    if(result.page_changed && !present_session(locale, note_until))
        fputs("Could not open the product page.\n", stderr);
}

static bool product_service_pending(const po_ui_locale_t *locale, uint32_t *note_until)
{
    if(product_inject_valid) {
        product_inject_valid = false;
        product_navigate(product_inject_key, locale, note_until);
    }
    if(product_pending_page_valid) {
        product_pending_page_valid = false;
        product_session.page = product_pending_page;
        if(!present_session(locale, note_until))
            fputs("Could not open the product page.\n", stderr);
    }
    if(settings_screen_pending && !settings_service())
        fputs("Could not switch the settings screen.\n", stderr);
    if(product_view_dirty) {
        product_view_dirty = false;
        if(!refresh_session())
            fputs("Could not refresh the product page.\n", stderr);
    }
    return true;
}

static bool present_session(const po_ui_locale_t *locale, uint32_t *note_until)
{
    brew_view_t brew;
    result_view_t result;
    po_m2_ready_view_t mode2_ready;
    po_m2_brew_view_t mode2_brew;
    po_m2_result_view_t mode2_result;
    switch(product_session.page) {
    case SIM_M1_READY:
        ready_view.net_decigrams = product_session.net_decigrams;
        if(!po_ui_show_ready(&ready_view, locale)) return false;
        po_ui_ready_set_handlers(ready_params_changed, ready_tared, note_until);
        ready_restore_params();
        return po_ui_ready_set_records(ready_records, 5);
    case SIM_M1_BREW:
        host_brew_begin();
        m1_brew_target_sheet = false;
        fill_m1_brew(&brew);
        if(!po_ui_show_brew(&brew)) return false;
        po_ui_brew_set_handlers(on_m1_brew_target, on_m1_brew_target_toggle, NULL);
        return true;
    case SIM_M1_RESULT:
        m1_result_save_open = false;
        fill_m1_result(&result);
        if(!po_ui_show_result(&result)) return false;
        po_ui_result_set_handlers(on_m1_result_save, on_m1_result_discard, NULL);
        return true;
    case SIM_M2_READY:
        if(!m2_ready_mirror_valid) {
            m2_ready_mirror = po_ui_m2_ready_baseline;
            m2_ready_mirror_valid = true;
        }
        fill_m2_ready(&mode2_ready);
        if(!po_ui_show_m2_ready(&mode2_ready)) return false;
        po_ui_m2_ready_set_handlers(on_m2_ready_event, NULL);
        return true;
    case SIM_M2_BREW:
        if(!m2_brew_mirror_valid) {
            m2_brew_mirror = po_ui_m2_brew_baseline;
            m2_brew_mirror_valid = true;
        }
        /* Fresh page instance: transient surface/dialog state resets, dose and
           recipe preferences stay. */
        m2_brew_mirror.surface = PO_M2_BREW_MAIN;
        m2_brew_mirror.duration_left = -1;
        m2_brew_mirror.note = PO_M2_NOTE_NONE;
        m2_brew_mirror.editor = PO_M2_EDITOR_WHEEL;
        fill_m2_brew(&mode2_brew);
        if(!po_ui_show_m2_brew(&mode2_brew)) return false;
        po_ui_m2_brew_set_handlers(on_m2_brew_event, NULL);
        return true;
    case SIM_M2_RESULT:
        if(!m2_result_mirror_valid) {
            m2_result_mirror = po_ui_m2_result_baseline;
            m2_result_mirror_valid = true;
        }
        m2_result_mirror.surface = PO_M2_RESULT_MAIN;
        m2_result_mirror.save_open = false;
        m2_result_mirror.save_seconds_left = -1;
        m2_result_mirror.return_seconds_left = -1;
        m2_result_mirror.bypass_live_decigrams = 0;
        m2_result_mirror.editor = PO_M2_EDITOR_WHEEL;
        fill_m2_result(&mode2_result);
        if(!po_ui_show_m2_result(&mode2_result)) return false;
        po_ui_m2_result_set_handlers(on_m2_result_event, NULL);
        return true;
    case SIM_SETTINGS:
        if(!settings_mirror_valid) {
            settings_mirror = po_ui_settings_baseline;
            settings_mirror.language = locale;
            settings_mirror_valid = true;
        }
        settings_mirror.screen = PO_SET_HOME;
        settings_mirror.ounces = ready_view.unit == PO_READY_UNIT_OZ;
        if(!po_ui_show_settings(&settings_mirror)) return false;
        po_ui_settings_set_handlers(on_settings_event, NULL);
        return true;
    }
    return false;
}

static bool refresh_session(void)
{
    brew_view_t brew;
    result_view_t result;
    po_m2_ready_view_t mode2_ready;
    po_m2_brew_view_t mode2_brew;
    po_m2_result_view_t mode2_result;
    switch(product_session.page) {
    case SIM_M1_READY:
        ready_view.net_decigrams = product_session.net_decigrams;
        return po_ui_ready_set(&ready_view);
    case SIM_M1_BREW:
        fill_m1_brew(&brew);
        return po_ui_brew_set(&brew);
    case SIM_M1_RESULT:
        fill_m1_result(&result);
        return po_ui_result_set(&result);
    case SIM_M2_READY:
        fill_m2_ready(&mode2_ready);
        if(m2_ready_editor_open(&mode2_ready)) return true;
        return po_ui_m2_ready_set(&mode2_ready);
    case SIM_M2_BREW:
        fill_m2_brew(&mode2_brew);
        return po_ui_m2_brew_set(&mode2_brew);
    case SIM_M2_RESULT:
        fill_m2_result(&mode2_result);
        return po_ui_m2_result_set(&mode2_result);
    case SIM_SETTINGS:
        return true;
    }
    return false;
}

static bool product_key_consumed(sim_key_t key)
{
    if(key == SIM_KEY_LEFT) {
        switch(product_session.page) {
        case SIM_M1_READY: return po_ui_ready_key_left();
        case SIM_M2_READY: return po_ui_m2_ready_key_left();
        case SIM_M2_BREW: return po_ui_m2_brew_key_left();
        case SIM_M2_RESULT: return po_ui_m2_result_key_left();
        case SIM_SETTINGS: return po_ui_settings_key_left();
        default: return false;
        }
    }
    if(key != SIM_KEY_RIGHT) return false;
    switch(product_session.page) {
    case SIM_M1_READY: return po_ui_ready_key_right();
    case SIM_M2_READY: return po_ui_m2_ready_key_right();
    case SIM_M2_BREW: return po_ui_m2_brew_key_right();
    case SIM_M2_RESULT: return po_ui_m2_result_key_right();
    default: return false;
    }
}

static void product_handle_key(sim_key_t key, const po_ui_locale_t *locale, uint32_t *note_until)
{
    bool consumed = product_key_consumed(key);
    sim_key_result_t result = sim_session_key(&product_session, key, consumed);
    bool weight_changed = false;
    if(result.page_changed) {
        if(!present_session(locale, note_until))
            fputs("Could not open the product page.\n", stderr);
    }
    else if(result.data_changed) weight_changed = true;
    if(weight_changed && product_session.page == SIM_M1_BREW) host_brew_follow();
    if(weight_changed && product_session.page == SIM_M1_READY)
        ready_tared(note_until); /* X tare clears the container like the T key */
    if(weight_changed && !refresh_session())
        fputs("Could not refresh the product page.\n", stderr);
}


static bool product_edge(const uint8_t *now, int count, SDL_Scancode code)
{
    return code < count && now[code] && !product_was[code];
}

static void poll_product_keys(const po_ui_locale_t *locale, uint32_t *note_until)
{
    int count = 0;
    const uint8_t *now = SDL_GetKeyboardState(&count);
    sim_key_t keys[3];
    int found = 0;
    int i;
    if(count > SDL_NUM_SCANCODES) count = SDL_NUM_SCANCODES;
    if(product_session.page == SIM_M1_READY)
        product_session.net_decigrams = ready_view.net_decigrams;
    bool weight_changed = false;
    if(product_edge(now, count, SDL_SCANCODE_Z)) keys[found++] = SIM_KEY_LEFT;
    if(product_edge(now, count, SDL_SCANCODE_X)) keys[found++] = SIM_KEY_RIGHT;
    if(product_edge(now, count, SDL_SCANCODE_TAB)) keys[found++] = SIM_SWIPE_NEXT;
    if(product_session.page != SIM_M1_READY && product_session.page != SIM_SETTINGS) {
        if(product_edge(now, count, SDL_SCANCODE_UP)) {
            sim_session_add_weight(&product_session, 10);
            weight_changed = true;
        }
        if(product_edge(now, count, SDL_SCANCODE_DOWN)) {
            sim_session_add_weight(&product_session, -10);
            weight_changed = true;
        }
    }
    for(i = 0; i < found; ++i) product_handle_key(keys[i], locale, note_until);
    product_service_pending(locale, note_until);
    if(weight_changed && !refresh_session())
        fputs("Could not refresh the product page.\n", stderr);
    if(count > 0) memcpy(product_was, now, (size_t)count);
    if(product_session.page == SIM_M1_BREW || product_session.page == SIM_M2_BREW) {
        uint32_t now_ms = SDL_GetTicks();
        if(now_ms - product_brew_tick >= 1000u) {
            product_brew_tick = now_ms;
            sim_session_tick_brew(&product_session);
            if(product_session.page == SIM_M1_BREW) host_brew_sample();
            (void)refresh_session();
        }
    } else product_brew_tick = SDL_GetTicks();
}

#ifndef SIMULATOR_PRODUCT_TEST
int main(int argc, char **argv)
{
    ready_view = po_ui_ready_baseline;
    simulator_options_t options;
    if(!simulator_options_parse(argc, argv, &options)) {
        simulator_options_usage(argv[0]);
        return EXIT_FAILURE;
    }
#ifndef PO_UI_RESOURCE_PROTOTYPE
    if(options.resource_pack_path != NULL) {
        fprintf(stderr, "--resource-pack requires PO_UI_RESOURCE_PROTOTYPE=ON.\n");
        return EXIT_FAILURE;
    }
#endif
    /* Preview still works without storage; confirmation reports save failure. */
    char *preferences_path = get_preferences_file_path();
    bool saved_language_valid = false;
    const po_ui_locale_t *initial_language =
        select_initial_language(&options, preferences_path, &saved_language_valid);

    lv_init();
    lv_display_t *display = sdl_hal_init(SIMULATOR_WIDTH, SIMULATOR_HEIGHT);
    if(display == NULL) {
        fprintf(stderr, "Could not initialize the SDL display.\n");
        free(preferences_path);
        return EXIT_FAILURE;
    }
#ifdef PO_UI_RESOURCE_PROTOTYPE
    po_resource_pack_t *ready_resource_pack = NULL;
#endif
    bool showing_language = options.screen == SCREEN_LANGUAGE;
    bool showing_ready = options.screen == SCREEN_READY;
    bool language_pending = startup_language_pending(&options, saved_language_valid);
    if(showing_language) {
        if(!po_ui_show_language(initial_language, save_language, preferences_path)) {
            fprintf(stderr, "Could not create the language page.\n");
            free(preferences_path);
            return EXIT_FAILURE;
        }
    }
    else if(showing_ready) {
#ifdef PO_UI_RESOURCE_PROTOTYPE
        if(options.resource_pack_path != NULL) {
            po_ready_view_t fallback_view = ready_view;
            po_resource_pack_t *candidate = NULL;
            po_resource_pack_error_t error = {0};
            po_resource_pack_result_t result =
                po_resource_pack_load(options.resource_pack_path, &candidate, &error);
            bool use_builtin = result != PO_RESOURCE_PACK_OK;
            if(use_builtin) {
                fprintf(stderr, "Could not load Ready resource pack '%s': %s.\n",
                        options.resource_pack_path,
                        error.message != NULL ? error.message : "unknown resource pack error");
            }
            else {
                const po_ready_resources_t *resources = po_resource_pack_resources(candidate);
                po_resource_pack_stats_t stats = po_resource_pack_stats(candidate);
                if(resources != NULL) {
                    printf("Loaded Ready resource pack: locale=%s, file=%zu bytes, "
                           "retained host estimate=%zu bytes, reads=%u.\n",
                           resources->locale_tag, stats.file_bytes,
                           stats.retained_host_bytes_estimate, stats.read_calls);
                    if(po_ui_show_ready_with_resources(&ready_view, resources))
                        ready_resource_pack = candidate;
                    else {
                        fprintf(stderr, "Could not create Ready page with the resource pack.\n");
                        use_builtin = true;
                    }
                }
                else {
                    fprintf(stderr, "Ready resource pack has no resource view.\n");
                    use_builtin = true;
                }
            }
            if(use_builtin) {
                po_ui_destroy();
                if(candidate != NULL) po_resource_pack_destroy(candidate);
                ready_view = fallback_view;
                fprintf(stderr, "Falling back to built-in Ready preview.\n");
                if(!po_ui_show_ready(&ready_view, initial_language)) {
                    fprintf(stderr, "Could not create built-in mode 1 ready after resource preview fallback.\n");
                    free(preferences_path);
                    return EXIT_FAILURE;
                }
            }
        }
        else if(!po_ui_show_ready(&ready_view, initial_language)) {
            fprintf(stderr, "Could not create mode 1 ready.\n");
            free(preferences_path);
            return EXIT_FAILURE;
        }
#else
        if(!po_ui_show_ready(&ready_view, initial_language)) {
            fprintf(stderr, "Could not create mode 1 ready.\n");
            free(preferences_path);
            return EXIT_FAILURE;
        }
#endif
    }
    else {
        if(!options.screenshot_path) simulator_wait_for_start();
        po_ui_show_boot_variant(options.boot_animation);
        if(po_ui_boot_get_state() == PO_BOOT_NONE) {
            fprintf(stderr, "Could not create boot animation.\n");
            free(preferences_path);
            return EXIT_FAILURE;
        }
        /* Preferences and the language-page inputs are already ready on this host. */
        if(language_pending) po_ui_boot_request_exit();
        else puts("Boot preview holds the final logo.");
        frame_stats_begin(display);
    }
    bool measured = showing_language || showing_ready;
    bool ready_help = false;
    uint32_t note_until = 0;
    for(;;) {
        uint32_t sleep_time = lv_timer_handler();
        po_boot_state_t state = po_ui_boot_get_state();
        if(!measured && (state == PO_BOOT_HOLD || state == PO_BOOT_FINISHED)) {
            frame_stats_report();
            measured = true;
        }
        if(!startup_dispatch(&language_pending, &showing_language, &showing_ready,
                             initial_language, preferences_path)) {
            free(preferences_path);
            return EXIT_FAILURE;
        }
        if(showing_ready && !ready_help) {
            puts("Hardware keys: Z left (start, stop, or back), X right (tare, or zero and return from a result). Tab stands in for the mode swipe: mode 1 ready, mode 2 ready, settings, then mode 1. Up/down changes weight. Mode 1 ready keeps C H [ ] T A N O R U and 1-4.");
            sim_session_init(&product_session);
            product_session.net_decigrams = ready_view.net_decigrams;
            product_brew_tick = SDL_GetTicks();
            product_started = true;
            po_ui_ready_set_handlers(ready_params_changed, ready_tared, &note_until);
            ready_restore_params();
            po_ui_ready_set_records(ready_records, 5);
            ready_help = true;
        }
        if(showing_ready && product_session.page == SIM_M1_READY &&
           ready_debug(&note_until) && !po_ui_ready_set(&ready_view))
            fputs("Could not update mode 1 ready.\n", stderr);
        if(showing_ready && product_started)
            poll_product_keys(initial_language, &note_until);
        if(options.screenshot_path && !language_pending &&
           (showing_language || showing_ready || state == PO_BOOT_HOLD)) {
            lv_refr_now(display);
            bool captured = simulator_capture(options.screenshot_path);
            po_ui_destroy();
#ifdef PO_UI_RESOURCE_PROTOTYPE
            if(ready_resource_pack != NULL) po_resource_pack_destroy(ready_resource_pack);
#endif
            free(preferences_path);
            return captured ? EXIT_SUCCESS : EXIT_FAILURE;
        }
        if(sleep_time == LV_NO_TIMER_READY) sleep_time = LV_DEF_REFR_PERIOD;
        SDL_Delay(sleep_time);
    }
}
#endif /* SIMULATOR_PRODUCT_TEST */
#endif
#else
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

/* Tare keys run on a synthetic keyboard; release is a second call with the key up. */
static bool press_key(uint16_t scancode)
{
    static uint8_t keys[SDL_NUM_SCANCODES];
    memset(keys, 0, sizeof(keys));
    keys[scancode] = 1;
    uint32_t until = 0;
    bool changed = ready_apply(keys, SDL_NUM_SCANCODES, &until);
    memset(keys, 0, sizeof(keys));
    ready_apply(keys, SDL_NUM_SCANCODES, &until);
    return changed;
}

int main(void)
{
    ready_view = po_ui_ready_baseline;
    ready_view.container = PO_READY_CONTAINER_NONE;
    ready_view.net_decigrams = 180;
    assert(press_key(SDL_SCANCODE_A));
    assert(ready_view.container == PO_READY_CONTAINER_MATCHED);
    assert(ready_view.container_id == 1);
    assert(ready_view.tare_decigrams == 2004);
    assert(ready_view.net_decigrams == 0);
    assert(ready_view.note == PO_READY_NOTE_NONE);

    ready_view.container = PO_READY_CONTAINER_NONE;
    ready_view.net_decigrams = 55;
    assert(press_key(SDL_SCANCODE_N));
    assert(ready_view.container == PO_READY_CONTAINER_UNMATCHED);
    assert(ready_view.net_decigrams == 0);
    assert(ready_view.note == PO_READY_NOTE_NONE);

    assert(press_key(SDL_SCANCODE_T));
    assert(ready_view.net_decigrams == 0);
    assert(ready_view.container == PO_READY_CONTAINER_NONE);
    assert(ready_view.note == PO_READY_NOTE_TARED);
    puts("simulator ready debug: PASS (A/N tare zero the net weight)");
    return 0;
}
#endif
