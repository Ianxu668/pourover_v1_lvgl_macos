#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "lvgl/lvgl.h"
#include "pourover_ui.h"
#include "simulator_product.h"
#include "simulator_session.h"

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


static sim_session_t product_session;
static uint8_t product_was[SDL_NUM_SCANCODES];
static uint32_t product_brew_tick;
static bool product_started;

static bool refresh_session(void);
static bool present_session(const po_ui_locale_t *locale, uint32_t *note_until);
static void product_handle_key(sim_key_t key, const po_ui_locale_t *locale, uint32_t *note_until);

/* One sample per second from t=0 through 30:00. Matches PO_M2_SAMPLE_MAX. */
#define HOST_BREW_CAP 1801
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
/* Simulator fake history so the settings history states are reachable. */
static po_settings_view_t settings_mirror;

static bool settings_mirror_valid;
static bool settings_view_dirty;
static uint32_t settings_toast_until;
static bool product_view_dirty;
static bool product_inject_valid;
static sim_key_t product_inject_key;
static bool product_pending_page_valid;
static sim_page_t product_pending_page;
static bool settings_screen_pending;
static int settings_pending_screen;

static void settings_mirror_initialize(const po_ui_locale_t *locale)
{
    settings_mirror = po_ui_settings_baseline;
    settings_mirror.connected = true;
    settings_mirror.language = locale;
    snprintf(settings_mirror.serial, sizeof settings_mirror.serial, "MS24001128");
    settings_mirror.history[0] = (po_settings_history_t){
        .month = 11, .day = 3, .cup_index = 2, .mode = 1, .seconds = 168,
        .water_decigrams = 2486, .dose_decigrams = 0, .avg_flow_decigrams = 14,
        .target_grams = 250
    };
    settings_mirror.history[1] = (po_settings_history_t){
        .month = 11, .day = 3, .cup_index = 1, .mode = 1, .seconds = 192,
        .water_decigrams = 2554, .dose_decigrams = 0, .avg_flow_decigrams = 13,
        .target_grams = 250
    };
    settings_mirror.history[2] = (po_settings_history_t){
        .month = 11, .day = 2, .cup_index = 4, .mode = 2, .seconds = 175,
        .water_decigrams = 2400, .dose_decigrams = 150, .avg_flow_decigrams = 12,
        .target_grams = 250
    };
    settings_mirror.history[3] = (po_settings_history_t){
        .month = 11, .day = 2, .cup_index = 3, .mode = 2, .seconds = 130,
        .water_decigrams = 2204, .dose_decigrams = 120, .avg_flow_decigrams = 11,
        .target_grams = 250
    };
    settings_mirror.history[4] = (po_settings_history_t){
        .month = 11, .day = 1, .cup_index = 5, .mode = 2, .seconds = 181,
        .water_decigrams = 2350, .dose_decigrams = 130, .avg_flow_decigrams = 12,
        .target_grams = 250
    };
    settings_mirror.history_count = 5;
    settings_mirror.history_index = 0;
    settings_mirror.container_weight_decigrams[0] = 1824;
    settings_mirror.container_weight_decigrams[1] = 3100;
    settings_mirror.container_weight_decigrams[2] = -1;
    settings_mirror.container_slot = 2;
    settings_mirror_valid = true;
}

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

/* Simulator demo catalog: the pick page and history sheet frames are drawn
 * with these three recipes and cups. */
static const po_m2_recipe_t m2_recipes[] = {
    {.name = "配方 A", .segments = 5, .total_grams = 288, .seconds = 185},
    {.name = "配方 B", .segments = 4, .total_grams = 251, .seconds = 154},
    {.name = "预置 · 浅烘", .segments = 4, .total_grams = 300, .seconds = 200, .preset = true},
};

static const po_m2_cup_t m2_cups[] = {
    {.year = 26, .month = 10, .day = 28, .cup_index = 2, .seconds = 185,
     .weight_decigrams = 2880, .dose_decigrams = 180},
    {.year = 26, .month = 10, .day = 27, .cup_index = 1, .seconds = 192,
     .weight_decigrams = 2860, .dose_decigrams = 180},
    {.year = 26, .month = 10, .day = 26, .cup_index = 4, .seconds = 185,
     .weight_decigrams = 2400, .dose_decigrams = 150},
};

static void fill_m2_brew(po_m2_brew_view_t *view)
{
    *view = m2_brew_mirror;
    view->elapsed_seconds = product_session.elapsed_seconds;
    view->net_decigrams = product_session.net_decigrams;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    view->sample_count = host_brew_count;
    view->samples = host_brew_count ? host_brew : NULL;
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
        if(event->value < 0 || (unsigned)event->value >= sizeof m2_recipes / sizeof m2_recipes[0])
            break;
        {
            const po_m2_recipe_t *recipe = &m2_recipes[event->value];
            m2_ready_mirror.surface = PO_M2_READY_MAIN;
            m2_ready_mirror.source = PO_M2_SOURCE_RECIPE;
            snprintf(m2_ready_mirror.recipe_name, sizeof m2_ready_mirror.recipe_name, "%s",
                     recipe->name);
            m2_ready_mirror.recipe_segments = recipe->segments;
            m2_ready_mirror.target_grams = recipe->total_grams;
            product_view_dirty = true;
        }
        break;
    case PO_M2_READY_CUP:
        if(event->value < 0 || (unsigned)event->value >= sizeof m2_cups / sizeof m2_cups[0])
            break;
        {
            const po_m2_cup_t *cup = &m2_cups[event->value];
            m2_ready_mirror.surface = PO_M2_READY_MAIN;
            m2_ready_mirror.source = PO_M2_SOURCE_HISTORY;
            m2_ready_mirror.recipe_name[0] = '\0';
            m2_ready_mirror.recipe_segments = 0;
            m2_ready_mirror.dose_origin = PO_M2_DOSE_CARRIED;
            m2_ready_mirror.dose_decigrams = cup->dose_decigrams;
            m2_ready_mirror.ratio_tenths = cup->dose_decigrams > 0
                                               ? cup->weight_decigrams * 10 / cup->dose_decigrams
                                               : 0;
            m2_ready_mirror.target_grams = cup->weight_decigrams / 10;
            product_view_dirty = true;
        }
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
    case PO_SET_ACT_HISTORY_OPEN:
        if(event->value >= 0 && (unsigned)event->value < settings_mirror.history_count) {
            settings_mirror.history_index = event->value;
            settings_view_dirty = true;
        }
        break;
    case PO_SET_ACT_HISTORY_DELETE_ASK:
        if(event->value < 0 || (unsigned)event->value >= settings_mirror.history_count) break;
        settings_mirror.history_index = event->value;
        settings_pending_screen = PO_SET_HISTORY_DELETE;
        settings_screen_pending = true;
        break;
    case PO_SET_ACT_HISTORY_CLEAR_ASK:
        settings_pending_screen = PO_SET_HISTORY_CLEAR;
        settings_screen_pending = true;
        break;
    case PO_SET_ACT_HISTORY_DELETE_CANCEL:
        settings_pending_screen = PO_SET_HISTORY;
        settings_screen_pending = true;
        break;
    case PO_SET_ACT_HISTORY_DELETE_OK:
        if(settings_mirror.history_index >= 0 &&
           (unsigned)settings_mirror.history_index < settings_mirror.history_count) {
            for(unsigned i = (unsigned)settings_mirror.history_index;
                i + 1 < settings_mirror.history_count; ++i)
                settings_mirror.history[i] = settings_mirror.history[i + 1];
            settings_mirror.history_count--;
            settings_mirror.history_index = 0;
        }
        settings_mirror.history_toast = true;
        settings_toast_until = SDL_GetTicks() + 2000;
        settings_pending_screen = PO_SET_HISTORY;
        settings_screen_pending = true;
        settings_view_dirty = true;
        break;
    case PO_SET_ACT_HISTORY_CLEAR_OK:
        settings_mirror.history_count = 0;
        settings_mirror.history_index = 0;
        settings_pending_screen = PO_SET_HISTORY;
        settings_screen_pending = true;
        settings_view_dirty = true;
        break;
    default:
        /* Container, calibration and reset have no simulator backend. */
        break;
    }
}

static bool settings_service(void)
{
    if(settings_mirror.history_toast && SDL_GetTicks() >= settings_toast_until) {
        settings_mirror.history_toast = false;
        settings_view_dirty = true;
    }
    if(!settings_screen_pending && !settings_view_dirty) return true;
    if(settings_screen_pending) {
        settings_screen_pending = false;
        settings_mirror.screen = (po_settings_screen_t)settings_pending_screen;
    }
    settings_view_dirty = false;
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
    if((settings_screen_pending || settings_view_dirty) && !settings_service())
        fputs("Could not switch the settings screen.\n", stderr);
    if(product_view_dirty) {
        product_view_dirty = false;
        if(!refresh_session())
            fputs("Could not refresh the product page.\n", stderr);
    }
    return true;
}

static void on_preview_back(void *context)
{
    (void)context;
    product_pending_page = product_session.return_ready;
    product_pending_page_valid = true;
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
        return po_ui_ready_set_records(ready_records, sizeof ready_records / sizeof ready_records[0]);
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
        if(!po_ui_m2_ready_set_recipes(m2_recipes,
                                       sizeof m2_recipes / sizeof m2_recipes[0])) return false;
        if(!po_ui_m2_ready_set_cups(m2_cups, sizeof m2_cups / sizeof m2_cups[0])) return false;
        po_ui_m2_ready_set_handlers(on_m2_ready_event, NULL);
        return true;
    case SIM_M2_BREW:
        host_brew_begin();
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
        if(!settings_mirror_valid) settings_mirror_initialize(locale);
        settings_mirror.ounces = ready_view.unit == PO_READY_UNIT_OZ;
        if(!po_ui_show_settings(&settings_mirror,
                                settings_mirror.language ? settings_mirror.language : locale))
            return false;
        po_ui_settings_set_handlers(on_settings_event, NULL);
        return true;
    case SIM_CONTAINER:
        if(!po_ui_show_container(NULL)) return false;
        po_ui_container_set_handlers(on_preview_back, NULL, NULL, NULL, NULL, NULL);
        return true;
    case SIM_BREW_SETTINGS:
        if(!po_ui_show_brew_settings(NULL)) return false;
        po_ui_brew_settings_set_handlers(on_preview_back, NULL, NULL);
        return true;
    case SIM_PAGE_NONE:
        return false;
    }
    return false;
}

static bool start_product(const simulator_options_t *options, const po_ui_locale_t *locale,
                          uint32_t *note_until)
{
    product_started = false;
    if(!po_ui_set_language(locale)) return false;
    sim_session_init(&product_session);
    product_session.page = options->page;
    product_session.net_decigrams = ready_view.net_decigrams;
    product_brew_tick = SDL_GetTicks();
    if(product_session.page == SIM_SETTINGS) {
        if(!settings_mirror_valid) settings_mirror_initialize(locale);
        settings_mirror.screen = options->settings_screen;
        if(options->container_phase_set)
            settings_mirror.container_phase = options->container_phase;
    }
    if(product_session.page == SIM_M2_READY) {
        if(!m2_ready_mirror_valid) {
            m2_ready_mirror = po_ui_m2_ready_baseline;
            m2_ready_mirror_valid = true;
        }
        if(options->m2_surface_set) m2_ready_mirror.surface = options->m2_surface;
        if(options->m2_tab_set) m2_ready_mirror.pick_tab = options->m2_tab;
        if(options->m2_editor_set) m2_ready_mirror.editor = options->m2_editor;
    }
    if(product_session.page == SIM_M1_READY) {
        po_ui_ready_set_handlers(ready_params_changed, ready_tared, note_until);
        ready_restore_params();
        if(!po_ui_ready_set_records(ready_records, sizeof ready_records / sizeof ready_records[0]))
            return false;
    }
    else if(!present_session(locale, note_until)) return false;
    product_started = true;
    return true;
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
    case SIM_CONTAINER:
    case SIM_BREW_SETTINGS:
        return true;
    case SIM_PAGE_NONE:
        return false;
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
        if(product_session.page == SIM_SETTINGS) settings_mirror.screen = PO_SET_HOME;
        if(!present_session(locale, note_until))
            fputs("Could not open the product page.\n", stderr);
    }
    else if(result.data_changed) weight_changed = true;
    if(weight_changed && (product_session.page == SIM_M1_BREW ||
                          product_session.page == SIM_M2_BREW))
        host_brew_follow();
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
            host_brew_sample();
            (void)refresh_session();
        }
    } else product_brew_tick = SDL_GetTicks();
}

void simulator_product_init(void)
{
    ready_view = po_ui_ready_baseline;
    ready_curve = true;
    sim_session_init(&product_session);
    memset(product_was, 0, sizeof product_was);
    host_brew_count = 0;
    host_brew_origin = 0;
    host_brew_last = 0;
    product_brew_tick = 0;
    product_started = false;
    m1_brew_target_sheet = false;
    m1_result_save_open = false;
    memset(&m2_ready_mirror, 0, sizeof m2_ready_mirror);
    m2_ready_mirror_valid = false;
    memset(&m2_brew_mirror, 0, sizeof m2_brew_mirror);
    m2_brew_mirror_valid = false;
    memset(&m2_result_mirror, 0, sizeof m2_result_mirror);
    m2_result_mirror_valid = false;
    memset(&settings_mirror, 0, sizeof settings_mirror);
    settings_mirror_valid = false;
    settings_view_dirty = false;
    settings_toast_until = 0;
    product_view_dirty = false;
    product_inject_valid = false;
    product_pending_page_valid = false;
    settings_screen_pending = false;
    settings_pending_screen = PO_SET_HOME;
}

po_ready_view_t *simulator_product_ready_view(void)
{
    return &ready_view;
}

void simulator_product_ready_restore_params(void)
{
    ready_restore_params();
}

bool simulator_product_ready_debug(uint32_t *note_until)
{
    return ready_debug(note_until);
}

bool simulator_product_start(const simulator_options_t *options, const po_ui_locale_t *locale,
                             uint32_t *note_until)
{
    return start_product(options, locale, note_until);
}

bool simulator_product_started(void)
{
    return product_started;
}

sim_page_t simulator_product_page(void)
{
    return product_session.page;
}

void simulator_product_poll(const po_ui_locale_t *locale, uint32_t *note_until)
{
    poll_product_keys(locale, note_until);
}

