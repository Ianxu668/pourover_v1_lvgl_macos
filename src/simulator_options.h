#ifndef SIMULATOR_OPTIONS_H
#define SIMULATOR_OPTIONS_H

#include "pourover_ui.h"

typedef enum { SCREEN_AUTO, SCREEN_BOOT, SCREEN_LANGUAGE, SCREEN_READY } simulator_screen_t;

typedef struct {
    simulator_screen_t screen;
    po_boot_variant_t boot_animation;
    bool language_set;
    const po_ui_locale_t *language;
    const char *screenshot_path;
    const char *resource_pack_path;
} simulator_options_t;

bool simulator_options_parse(int argc, char **argv, simulator_options_t *options);
void simulator_options_usage(const char *program);

#endif
