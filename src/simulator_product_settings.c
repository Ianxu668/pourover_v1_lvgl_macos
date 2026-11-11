#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "simulator_product_internal.h"

/* Simulator fake history so the settings history states are reachable. */
static po_settings_view_t settings_mirror;
static bool settings_mirror_valid;
static bool settings_view_dirty;
static uint32_t settings_toast_until;
static bool settings_screen_pending;
static int settings_pending_screen;

static void settings_mirror_initialize(const po_ui_locale_t *locale)
{
    settings_mirror = po_ui_settings_baseline;
    settings_mirror.connected = true;
    settings_mirror.language = locale;
    snprintf(settings_mirror.serial, sizeof settings_mirror.serial, "MS24001128");
    snprintf(settings_mirror.sync_time, sizeof settings_mirror.sync_time, "11-03 09:12");
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
    case PO_SET_ACT_AUTOSTART:
        settings_mirror.autostart = m2_autostart = event->value != 0;
        break;
    case PO_SET_ACT_REMAIN:
        settings_mirror.show_remain = m2_show_remain = event->value != 0;
        break;
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

bool settings_service(void)
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
    /* The settings page is gone. Keep the mirror current and stay quiet. */
    if(product_session.page != SIM_SETTINGS) return true;
    return po_ui_settings_set(&settings_mirror);
}

bool settings_present(const po_ui_locale_t *locale)
{
    if(!settings_mirror_valid) settings_mirror_initialize(locale);
    settings_mirror.ounces = ready_view.unit == PO_READY_UNIT_OZ;
    settings_mirror.autostart = m2_autostart;
    settings_mirror.show_remain = m2_show_remain;
    if(!po_ui_show_settings(&settings_mirror,
                            settings_mirror.language ? settings_mirror.language : locale))
        return false;
    po_ui_settings_set_handlers(on_settings_event, NULL);
    return true;
}

void settings_start(const simulator_options_t *options, const po_ui_locale_t *locale)
{
    if(!settings_mirror_valid) settings_mirror_initialize(locale);
    settings_mirror.screen = options->settings_screen;
    if(options->container_phase_set) settings_mirror.container_phase = options->container_phase;
}

void settings_open_home(void)
{
    settings_mirror.screen = PO_SET_HOME;
}

void settings_reset(void)
{
    memset(&settings_mirror, 0, sizeof settings_mirror);
    settings_mirror_valid = false;
    settings_view_dirty = false;
    settings_toast_until = 0;
    settings_screen_pending = false;
    settings_pending_screen = PO_SET_HOME;
}
