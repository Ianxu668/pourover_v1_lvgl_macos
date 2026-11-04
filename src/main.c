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

static bool ready_edge(const uint8_t *now, const uint8_t *was, int count, SDL_Scancode code)
{
    return code < count && now[code] && !was[code];
}

static bool ready_debug(uint32_t *note_until)
{
    int count = 0;
    const uint8_t *now = SDL_GetKeyboardState(&count);
    static uint8_t was[SDL_NUM_SCANCODES];
    if(count > SDL_NUM_SCANCODES) count = SDL_NUM_SCANCODES;
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
        ready_note(PO_READY_NOTE_TARED, note_until);
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_A)) {
        ready_view.net_decigrams = 0;
        ready_note(PO_READY_NOTE_AUTO, note_until);
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_N)) {
        ready_view.net_decigrams = 0;
        ready_note(PO_READY_NOTE_CONTAINER, note_until);
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
        ready_view.gross_decigrams = clamp_ready(ready_view.gross_decigrams + 10, -99999, 99999);
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_LEFT)) {
        ready_view.gross_decigrams = clamp_ready(ready_view.gross_decigrams - 10, -99999, 99999);
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_1)) {
        ready_view.show_target = ready_view.show_preset = ready_view.show_gross = true;
        ready_view.over_range = false;
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_2)) {
        ready_view.show_target = ready_view.show_preset = ready_view.show_gross = false;
        ready_view.over_range = false;
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_3)) {
        ready_view.show_target = ready_view.show_preset = true;
        ready_view.show_gross = false;
        ready_view.net_decigrams = -2000;
        ready_view.over_range = false;
        changed = true;
    }
    if(ready_edge(now, was, count, SDL_SCANCODE_4)) {
        ready_view.show_target = ready_view.show_preset = false;
        ready_view.show_gross = true;
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

static bool save_language(po_language_t language, void *context)
{
    const char *path = context;
    if(!simulator_preferences_write(path, language)) {
        const char *locale = simulator_preferences_locale(language);
        fprintf(stderr, "Could not save language preference%s%s.\n",
                locale == NULL ? "" : " for ", locale == NULL ? "" : locale);
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    ready_view = po_ui_ready_baseline;
    simulator_options_t options;
    if(!simulator_options_parse(argc, argv, &options)) {
        simulator_options_usage(argv[0]);
        return EXIT_FAILURE;
    }
    /* Preview still works without storage; confirmation reports save failure. */
    char *preferences_path = get_preferences_file_path();
    po_language_t initial_language = PO_LANG_EN;
    bool saved_language_valid = preferences_path != NULL &&
                                simulator_preferences_read(preferences_path, &initial_language);
    if(!saved_language_valid) {
        initial_language = PO_LANG_EN;
        puts("No valid saved language; English will be used for first-run setup.");
    }
    else printf("Loaded saved language: %s\n", simulator_preferences_locale(initial_language));
    if(options.language_set) initial_language = options.language;

    lv_init();
    lv_display_t *display = sdl_hal_init(SIMULATOR_WIDTH, SIMULATOR_HEIGHT);
    if(display == NULL) {
        fprintf(stderr, "Could not initialize the SDL display.\n");
        free(preferences_path);
        return EXIT_FAILURE;
    }
    bool showing_language = options.screen == SCREEN_LANGUAGE;
    bool showing_ready = options.screen == SCREEN_READY;
    bool language_pending = options.screen == SCREEN_AUTO && !saved_language_valid;
    bool continue_after_boot = options.screen == SCREEN_AUTO && options.screenshot_path == NULL;
    if(showing_language) {
        if(!po_ui_show_language(initial_language, save_language, preferences_path)) {
            fprintf(stderr, "Could not create the language page.\n");
            free(preferences_path);
            return EXIT_FAILURE;
        }
    }
    else if(showing_ready) {
        if(!po_ui_show_ready(&ready_view)) {
            fprintf(stderr, "Could not create mode 1 ready.\n");
            free(preferences_path);
            return EXIT_FAILURE;
        }
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
        if(language_pending || continue_after_boot) po_ui_boot_request_exit();
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
        if(language_pending && po_ui_boot_is_finished()) {
            language_pending = false;
            showing_language = true;
            if(!po_ui_show_language(initial_language, save_language, preferences_path)) {
                fprintf(stderr, "Could not create the language page.\n");
                free(preferences_path);
                return EXIT_FAILURE;
            }
        }
        else if(continue_after_boot && !showing_language && !showing_ready &&
                po_ui_boot_is_finished()) {
            showing_ready = true;
            if(!po_ui_show_ready(&ready_view)) {
                fprintf(stderr, "Could not create mode 1 ready.\n");
                free(preferences_path);
                return EXIT_FAILURE;
            }
        }
        if(showing_language && po_ui_is_confirmed()) {
            showing_language = false;
            showing_ready = true;
            if(!po_ui_show_ready(&ready_view)) {
                fprintf(stderr, "Could not create mode 1 ready.\n");
                free(preferences_path);
                return EXIT_FAILURE;
            }
        }
        if(showing_ready && !ready_help) {
            puts("Ready keys: C link, H charge, [ ] battery, T/A/N tare, O overload, R reject, U relabel, arrows weight, 1-4 layout.");
            ready_help = true;
        }
        if(showing_ready && ready_debug(&note_until) && !po_ui_ready_set(&ready_view))
            fputs("Could not update mode 1 ready.\n", stderr);
        if(options.screenshot_path && !language_pending &&
           (showing_language || showing_ready || state == PO_BOOT_HOLD)) {
            lv_refr_now(display);
            bool captured = simulator_capture(options.screenshot_path);
            po_ui_destroy();
            free(preferences_path);
            return captured ? EXIT_SUCCESS : EXIT_FAILURE;
        }
        if(sleep_time == LV_NO_TIMER_READY) sleep_time = LV_DEF_REFR_PERIOD;
        SDL_Delay(sleep_time);
    }
}
