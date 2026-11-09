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
#include "simulator_product.h"
#if defined(PO_UI_RESOURCE_PROTOTYPE) && \
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
        if(!po_ui_show_ready(simulator_product_ready_view(), selected_language)) {
            fprintf(stderr, "Could not create mode 1 ready.\n");
            return false;
        }
        simulator_product_ready_restore_params();
    }
    return true;
}

#ifndef SIMULATOR_HOST_STARTUP_TEST
int main(int argc, char **argv)
{
    simulator_product_init();
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
    po_ui_set_light(options.theme_light);
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
    /* Pages whose show entry takes no locale (mode 2) read this selection. */
    if(!po_ui_set_language(initial_language)) {
        fprintf(stderr, "Could not select the initial language.\n");
        free(preferences_path);
        return EXIT_FAILURE;
    }
#ifdef PO_UI_RESOURCE_PROTOTYPE
    po_resource_pack_t *ready_resource_pack = NULL;
#endif
    bool showing_language = options.screen == SCREEN_LANGUAGE;
    bool showing_ready = options.page != SIM_PAGE_NONE;
    bool language_pending = startup_language_pending(&options, saved_language_valid);
    if(showing_language) {
        if(!po_ui_show_language(initial_language, save_language, preferences_path)) {
            fprintf(stderr, "Could not create the language page.\n");
            free(preferences_path);
            return EXIT_FAILURE;
        }
    }
    else if(options.screen == SCREEN_READY) {
#ifdef PO_UI_RESOURCE_PROTOTYPE
        if(options.resource_pack_path != NULL) {
            po_ready_view_t fallback_view = *simulator_product_ready_view();
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
                    if(po_ui_show_ready_with_resources(simulator_product_ready_view(), resources))
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
                *simulator_product_ready_view() = fallback_view;
                fprintf(stderr, "Falling back to built-in Ready preview.\n");
                if(!po_ui_show_ready(simulator_product_ready_view(), initial_language)) {
                    fprintf(stderr, "Could not create built-in mode 1 ready after resource preview fallback.\n");
                    free(preferences_path);
                    return EXIT_FAILURE;
                }
            }
        }
        else if(!po_ui_show_ready(simulator_product_ready_view(), initial_language)) {
            fprintf(stderr, "Could not create mode 1 ready.\n");
            free(preferences_path);
            return EXIT_FAILURE;
        }
#else
        if(!po_ui_show_ready(simulator_product_ready_view(), initial_language)) {
            fprintf(stderr, "Could not create mode 1 ready.\n");
            free(preferences_path);
            return EXIT_FAILURE;
        }
#endif
    }
    else if(!showing_ready) {
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
    uint32_t note_until = 0;
    for(;;) {
        uint32_t sleep_time = lv_timer_handler();
        po_boot_state_t state = po_ui_boot_get_state();
        if(!measured && (state == PO_BOOT_HOLD || state == PO_BOOT_FINISHED)) {
            frame_stats_report();
            measured = true;
        }
        if(showing_language && po_ui_is_confirmed()) initial_language = po_ui_get_language();
        if(!startup_dispatch(&language_pending, &showing_language, &showing_ready,
                             initial_language, preferences_path)) {
            free(preferences_path);
            return EXIT_FAILURE;
        }
        if(showing_ready && !simulator_product_started()) {
            if(options.page == SIM_PAGE_NONE) options.page = SIM_M1_READY;
            puts("Hardware keys: Z left (start, stop, or back), X right (tare, or zero and return from a result). Tab stands in for the mode swipe: mode 1 ready, mode 2 ready, settings, then mode 1. Up/down changes weight. Mode 1 ready keeps C H [ ] T A N O R U and 1-4.");
            if(!simulator_product_start(&options, initial_language, &note_until)) {
                fputs("Could not open the requested page.\n", stderr);
                free(preferences_path);
                return EXIT_FAILURE;
            }
        }
        if(showing_ready && simulator_product_page() == SIM_M1_READY &&
           simulator_product_ready_debug(&note_until) && !po_ui_ready_set(simulator_product_ready_view()))
            fputs("Could not update mode 1 ready.\n", stderr);
        if(showing_ready && simulator_product_started())
            simulator_product_poll(initial_language, &note_until);
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
