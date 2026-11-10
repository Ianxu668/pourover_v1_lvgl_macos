#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "simulator_product_internal.h"

sim_session_t product_session;
static uint8_t product_was[SDL_NUM_SCANCODES];
static uint32_t product_brew_tick;
static bool product_started;

static bool refresh_session(void);
static bool present_session(const po_ui_locale_t *locale, uint32_t *note_until);
static void product_handle_key(sim_key_t key, const po_ui_locale_t *locale, uint32_t *note_until);

/* Page-local state lives in host mirrors; dynamic session data is filled per call.
 * Callbacks only mutate the mirrors and set pending flags; the host loop services
 * them outside LVGL event context. */

bool product_view_dirty;
static bool product_inject_valid;
static sim_key_t product_inject_key;
bool product_pending_page_valid;
sim_page_t product_pending_page;

void product_inject(sim_key_t key)
{
    product_inject_key = key;
    product_inject_valid = true;
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
    if(!settings_service()) fputs("Could not switch the settings screen.\n", stderr);
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
    switch(product_session.page) {
    case SIM_M1_READY: return ready_present(locale, note_until);
    case SIM_M1_BREW: return m1_brew_present();
    case SIM_M1_RESULT: return m1_result_present();
    case SIM_M2_READY: return m2_ready_present();
    case SIM_M2_BREW: return m2_brew_present();
    case SIM_M2_RESULT: return m2_result_present();
    case SIM_SETTINGS: return settings_present(locale);
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
    if(product_session.page == SIM_SETTINGS) settings_start(options, locale);
    if(product_session.page == SIM_M2_READY) m2_start(options);
    if(product_session.page == SIM_M1_READY) {
        if(!ready_start(note_until)) return false;
    }
    else if(!present_session(locale, note_until)) return false;
    product_started = true;
    return true;
}

static bool refresh_session(void)
{
    switch(product_session.page) {
    case SIM_M1_READY: return ready_refresh();
    case SIM_M1_BREW: return m1_brew_refresh();
    case SIM_M1_RESULT: return m1_result_refresh();
    case SIM_M2_READY: return m2_ready_refresh();
    case SIM_M2_BREW: return m2_brew_refresh();
    case SIM_M2_RESULT: return m2_result_refresh();
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
        if(product_session.page == SIM_SETTINGS) settings_open_home();
        if(!present_session(locale, note_until))
            fputs("Could not open the product page.\n", stderr);
    }
    else if(result.data_changed) weight_changed = true;
    if(weight_changed && (product_session.page == SIM_M1_BREW ||
                          product_session.page == SIM_M2_BREW))
        host_brew_follow();
    if(weight_changed && product_session.page == SIM_M1_READY)
        ready_weight_changed(note_until);
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
    ready_reset();
    brew_reset();
    m2_reset();
    settings_reset();
    sim_session_init(&product_session);
    memset(product_was, 0, sizeof product_was);
    product_brew_tick = 0;
    product_started = false;
    product_view_dirty = false;
    product_inject_valid = false;
    product_pending_page_valid = false;
}

bool simulator_product_start(const simulator_options_t *options, const po_ui_locale_t *locale,
                             uint32_t *note_until)
{
    po_ui_set_page_motion(true);
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
