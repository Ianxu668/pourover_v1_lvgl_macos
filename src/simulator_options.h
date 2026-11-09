#ifndef SIMULATOR_OPTIONS_H
#define SIMULATOR_OPTIONS_H

#include "pourover_ui.h"
#include "simulator_session.h"

typedef enum {
    SCREEN_AUTO, SCREEN_BOOT, SCREEN_LANGUAGE, SCREEN_READY, SCREEN_SETTINGS,
    SCREEN_M2_READY, SCREEN_M2_BREW, SCREEN_M2_RESULT, SCREEN_CONTAINER,
    SCREEN_BREW_SETTINGS, SCREEN_RESULT
} simulator_screen_t;

typedef struct {
    simulator_screen_t screen;
    sim_page_t page;
    po_boot_variant_t boot_animation;
    bool language_set;
    const po_ui_locale_t *language;
    const char *screenshot_path;
    const char *resource_pack_path;
    po_settings_screen_t settings_screen;
    bool container_phase_set;
    po_settings_cont_phase_t container_phase;
    bool m2_surface_set;
    po_m2_ready_surface_t m2_surface;
    bool m2_tab_set;
    po_m2_pick_tab_t m2_tab;
    bool m2_editor_set;
    po_m2_editor_t m2_editor;
    bool theme_light;
} simulator_options_t;

bool simulator_options_parse(int argc, char **argv, simulator_options_t *options);
void simulator_options_usage(const char *program);

#endif
