#include <string.h>
#include <SDL.h>
#include "simulator_product_internal.h"

po_ready_view_t ready_view;
bool ready_curve = true;

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

bool ready_present(const po_ui_locale_t *locale, uint32_t *note_until)
{
    ready_view.net_decigrams = product_session.net_decigrams;
    if(!po_ui_show_ready(&ready_view, locale)) return false;
    po_ui_ready_set_handlers(ready_params_changed, ready_tared, note_until);
    ready_restore_params();
    return po_ui_ready_set_records(ready_records, sizeof ready_records / sizeof ready_records[0]);
}

bool ready_start(uint32_t *note_until)
{
    po_ui_ready_set_handlers(ready_params_changed, ready_tared, note_until);
    ready_restore_params();
    return po_ui_ready_set_records(ready_records, sizeof ready_records / sizeof ready_records[0]);
}

bool ready_refresh(void)
{
    ready_view.net_decigrams = product_session.net_decigrams;
    return po_ui_ready_set(&ready_view);
}

void ready_weight_changed(uint32_t *note_until)
{
    ready_tared(note_until); /* X tare clears the container like the T key */
}

void ready_reset(void)
{
    ready_view = po_ui_ready_baseline;
    ready_curve = true;
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
