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
    !defined(SIMULATOR_HOST_STARTUP_TEST)
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
    }
    return true;
}

#ifndef SIMULATOR_READY_DEBUG_TEST
#ifndef SIMULATOR_HOST_STARTUP_TEST
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
            puts("Ready keys: C link, H charge, [ ] battery, T manual tare, A matched container, N unmatched tare, O overload, R reject, U relabel, up/down net, left/right sheet or tare, 1-4 layout. Pull the top for parameters and the bottom for the last five cups.");
            po_ui_ready_set_handlers(ready_params_changed, ready_tared, &note_until);
            po_ui_ready_set_records(ready_records, 5);
            ready_help = true;
        }
        if(showing_ready && ready_debug(&note_until) && !po_ui_ready_set(&ready_view))
            fputs("Could not update mode 1 ready.\n", stderr);
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
