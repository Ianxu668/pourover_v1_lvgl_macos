#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl/lvgl.h"
#include <SDL.h>

#include "hal/hal.h"
#include "pourover_ui.h"
#include "simulator_preferences.h"

#define SIMULATOR_WIDTH 960
#define SIMULATOR_HEIGHT 376
#define LANGUAGE_PREVIEW_DELAY_MS 1500

typedef enum {
    SCREEN_AUTO,
    SCREEN_BOOT,
    SCREEN_LANGUAGE
} simulator_screen_t;

typedef struct {
    simulator_screen_t screen;
    bool language_set;
    po_language_t language;
    const char *screenshot_path;
} simulator_options_t;

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--screen boot|language] [--language locale] [--screenshot path.ppm]\n",
            program);
}

static bool parse_arguments(int argc, char **argv, simulator_options_t *options)
{
    options->screen = SCREEN_AUTO;
    options->language_set = false;
    options->language = PO_LANG_EN;
    options->screenshot_path = NULL;

    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
            i++;
            if(strcmp(argv[i], "boot") == 0) options->screen = SCREEN_BOOT;
            else if(strcmp(argv[i], "language") == 0) options->screen = SCREEN_LANGUAGE;
            else return false;
        }
        else if(strcmp(argv[i], "--language") == 0 && i + 1 < argc) {
            if(!simulator_preferences_parse_locale(argv[++i], &options->language)) return false;
            options->language_set = true;
        }
        else if(strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            options->screenshot_path = argv[++i];
            if(options->screenshot_path[0] == '\0') return false;
        }
        else {
            return false;
        }
    }
    return true;
}

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
    if(path != NULL) {
        snprintf(path, path_length, "%s%slanguage", directory, has_separator ? "" : "/");
    }
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

static bool write_screenshot(const char *path)
{
    lv_draw_buf_t *snapshot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if(snapshot == NULL) {
        fprintf(stderr, "Could not capture LVGL snapshot.\n");
        return false;
    }

    size_t width = snapshot->header.w;
    size_t height = snapshot->header.h;
    size_t stride = snapshot->header.stride;
    bool valid = snapshot->header.cf == LV_COLOR_FORMAT_RGB888 &&
                 snapshot->data != NULL && width > 0 && height > 0 &&
                 width <= SIZE_MAX / 3 && stride >= width * 3;
    if(!valid) {
        fprintf(stderr, "LVGL snapshot has unsupported format or dimensions.\n");
        lv_draw_buf_destroy(snapshot);
        return false;
    }

    FILE *file = fopen(path, "wb");
    if(file == NULL) {
        fprintf(stderr, "Could not open screenshot '%s': %s\n", path, strerror(errno));
        lv_draw_buf_destroy(snapshot);
        return false;
    }

    bool ok = fprintf(file, "P6\n%zu %zu\n255\n", width, height) > 0;
    const unsigned char *data = snapshot->data;
    for(size_t row = 0; ok && row < height; row++) {
        const unsigned char *pixels = data + row * stride;
        for(size_t x = 0; ok && x < width; x++) {
            const unsigned char rgb[] = {pixels[x * 3 + 2], pixels[x * 3 + 1], pixels[x * 3]};
            ok = fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb);
        }
    }
    if(fclose(file) != 0) ok = false;
    lv_draw_buf_destroy(snapshot);

    if(!ok) fprintf(stderr, "Could not write screenshot '%s'.\n", path);
    return ok;
}

int main(int argc, char **argv)
{
    simulator_options_t options;
    if(!parse_arguments(argc, argv, &options)) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* Without a preference path, preview still works and confirmation reports save failure. */
    char *preferences_path = get_preferences_file_path();
    po_language_t initial_language = PO_LANG_EN;
    bool saved_language_valid = preferences_path != NULL &&
                                simulator_preferences_read(preferences_path, &initial_language);
    if(!saved_language_valid) {
        initial_language = PO_LANG_EN;
        puts("No valid saved language; English will be used for first-run setup.");
    }
    else {
        printf("Loaded saved language: %s\n", simulator_preferences_locale(initial_language));
    }
    if(options.language_set) initial_language = options.language;

    lv_init();
    lv_display_t *display = sdl_hal_init(SIMULATOR_WIDTH, SIMULATOR_HEIGHT);
    if(display == NULL) {
        fprintf(stderr, "Could not initialize the SDL display.\n");
        free(preferences_path);
        return EXIT_FAILURE;
    }

    po_ui_show_boot();
    bool show_language_after_delay = options.screen == SCREEN_AUTO && !saved_language_valid;
    if(options.screen == SCREEN_LANGUAGE) {
        po_ui_show_language(initial_language, save_language, preferences_path);
        show_language_after_delay = false;
    }
    else if(options.screen == SCREEN_BOOT) {
        show_language_after_delay = false;
    }
    else if(saved_language_valid) {
        puts("Keeping the boot screen; the next product screen is not integrated yet.");
    }

    uint32_t boot_started = SDL_GetTicks();
    for(;;) {
        uint32_t sleep_time = lv_timer_handler();
        uint32_t elapsed = SDL_GetTicks() - boot_started;

        if(show_language_after_delay && elapsed >= LANGUAGE_PREVIEW_DELAY_MS) {
            show_language_after_delay = false;
            po_ui_show_language(initial_language, save_language, preferences_path);
        }

        if(options.screenshot_path != NULL && !show_language_after_delay) {
            lv_refr_now(display);
            bool captured = write_screenshot(options.screenshot_path);
            po_ui_destroy();
            free(preferences_path);
            return captured ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        if(sleep_time == LV_NO_TIMER_READY) sleep_time = LV_DEF_REFR_PERIOD;
        if(show_language_after_delay) {
            uint32_t remaining = LANGUAGE_PREVIEW_DELAY_MS - elapsed;
            if(sleep_time > remaining) sleep_time = remaining;
        }
        SDL_Delay(sleep_time);
    }
}
