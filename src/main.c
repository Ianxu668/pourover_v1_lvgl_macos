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
    bool language_pending = options.screen == SCREEN_AUTO && !saved_language_valid;
    if(showing_language) {
        if(!po_ui_show_language(initial_language, save_language, preferences_path)) {
            fprintf(stderr, "Could not create the language page.\n");
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
        if(language_pending) po_ui_boot_request_exit();
        else puts("Boot preview holds the final logo; no Home page is implemented.");
        frame_stats_begin(display);
    }
    bool measured = showing_language;
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
        if(options.screenshot_path && !language_pending &&
           (showing_language || state == PO_BOOT_HOLD)) {
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
