#include "simulator_options.h"
#include "simulator_preferences.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    simulator_screen_t screen;
    sim_page_t page;
    po_settings_screen_t settings_screen;
    bool settings_only;
} screen_entry_t;

static const screen_entry_t screens[] = {
    {"boot", SCREEN_BOOT, SIM_PAGE_NONE, PO_SET_HOME, false},
    {"language", SCREEN_LANGUAGE, SIM_PAGE_NONE, PO_SET_HOME, false},
    {"ready", SCREEN_READY, SIM_M1_READY, PO_SET_HOME, false},
    {"settings", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_HOME, false},
    {"m2-ready", SCREEN_M2_READY, SIM_M2_READY, PO_SET_HOME, false},
    {"m2-brew", SCREEN_M2_BREW, SIM_M2_BREW, PO_SET_HOME, false},
    {"m2-result", SCREEN_M2_RESULT, SIM_M2_RESULT, PO_SET_HOME, false},
    {"container", SCREEN_CONTAINER, SIM_CONTAINER, PO_SET_HOME, false},
    {"brew-settings", SCREEN_BREW_SETTINGS, SIM_BREW_SETTINGS, PO_SET_HOME, false},
    {"result", SCREEN_RESULT, SIM_M1_RESULT, PO_SET_HOME, false},
    {"home", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_HOME, true},
    {"history", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_HISTORY, true},
    {"history-delete", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_HISTORY_DELETE, true},
    {"history-clear", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_HISTORY_CLEAR, true},
    {"prefs", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_PREFS, true},
    {"volume", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_VOLUME, true},
    {"brightness", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_BRIGHTNESS, true},
    {"unit", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_UNIT, true},
    {"language", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_LANGUAGE, true},
    {"other", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_OTHER, true},
    {"wake", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_WAKE, true},
    {"auto-off", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_AUTO_OFF, true},
    {"brew", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_BREW, true},
    {"container", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_CONTAINER, true},
    {"container-edit", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_CONTAINER_EDIT, true},
    {"calibrate", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_CALIBRATE, true},
    {"about", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_ABOUT, true},
    {"reset", SCREEN_SETTINGS, SIM_SETTINGS, PO_SET_RESET, true}
};

static const screen_entry_t *find_screen(const char *name, bool settings_only)
{
    for(unsigned i = 0; i < sizeof screens / sizeof screens[0]; ++i)
        if(screens[i].settings_only == settings_only && strcmp(name, screens[i].name) == 0)
            return &screens[i];
    return NULL;
}

void simulator_options_usage(const char *program)
{
    fprintf(stderr, "Usage: %s [--screen ", program);
    for(unsigned i = 0, count = 0; i < sizeof screens / sizeof screens[0]; ++i)
        if(!screens[i].settings_only) fprintf(stderr, "%s%s", count++ ? "|" : "", screens[i].name);
    fputs("] [--boot-animation A|G|H] [--language locale] [--settings-screen name] "
          "[--theme light|dark] [--container-phase phase] "
          "[--m2-surface dose|ratio|mode|pick|bypass|history] [--m2-tab free|recipe|history] "
          "[--m2-editor keypad|wheel] [--resource-pack path.por] [--screenshot path.ppm]\nSettings screens: ", stderr);
    for(unsigned i = 0, count = 0; i < sizeof screens / sizeof screens[0]; ++i)
        if(screens[i].settings_only) fprintf(stderr, "%s%s", count++ ? "|" : "", screens[i].name);
    fputc('\n', stderr);
}

bool simulator_options_parse(int argc, char **argv, simulator_options_t *options)
{
    *options = (simulator_options_t){.screen = SCREEN_AUTO, .page = SIM_PAGE_NONE,
                                     .boot_animation = PO_BOOT_H, .settings_screen = PO_SET_HOME,
                                     .language = po_ui_locale_find("en")};
    bool boot_animation_set = false;
    bool settings_screen_set = false;
    const char *screen_name = "auto";
    for(int i = 1; i < argc; ++i) {
        if(i + 1 == argc) return false;
        const char *option = argv[i++], *value = argv[i];
        if(strcmp(option, "--screen") == 0) {
            const screen_entry_t *entry = find_screen(value, false);
            if(!entry) return false;
            options->screen = entry->screen;
            options->page = entry->page;
            screen_name = entry->name;
        }
        else if(strcmp(option, "--boot-animation") == 0) {
            if(strcmp(value, "A") == 0) options->boot_animation = PO_BOOT_A;
            else if(strcmp(value, "G") == 0) options->boot_animation = PO_BOOT_G;
            else if(strcmp(value, "H") == 0) options->boot_animation = PO_BOOT_H;
            else return false;
            boot_animation_set = true;
        }
        else if(strcmp(option, "--language") == 0) {
            if(!simulator_preferences_parse_locale(value, &options->language)) return false;
            options->language_set = true;
        }
        else if(strcmp(option, "--settings-screen") == 0) {
            const screen_entry_t *entry = find_screen(value, true);
            if(!entry) return false;
            options->settings_screen = entry->settings_screen;
            settings_screen_set = true;
        }
        else if(strcmp(option, "--container-phase") == 0) {
            static const struct {
                const char *name;
                po_settings_cont_phase_t phase;
            } phases[] = {
                {"wait", PO_SET_CONT_WAIT}, {"ready", PO_SET_CONT_READY},
                {"conflict", PO_SET_CONT_CONFLICT}, {"full", PO_SET_CONT_FULL},
                {"replace", PO_SET_CONT_REPLACE}
            };
            unsigned i;
            for(i = 0; i < sizeof phases / sizeof phases[0]; ++i)
                if(strcmp(value, phases[i].name) == 0) break;
            if(i == sizeof phases / sizeof phases[0]) return false;
            options->container_phase = phases[i].phase;
            options->container_phase_set = true;
        }
        else if(strcmp(option, "--m2-surface") == 0) {
            static const struct {
                const char *name;
                po_m2_ready_surface_t surface;
            } surfaces[] = {
                {"dose", PO_M2_READY_DOSE}, {"ratio", PO_M2_READY_RATIO},
                {"mode", PO_M2_READY_MODE}, {"pick", PO_M2_READY_PICK},
                {"bypass", PO_M2_READY_BYPASS}, {"history", PO_M2_READY_HISTORY}
            };
            unsigned i;
            for(i = 0; i < sizeof surfaces / sizeof surfaces[0]; ++i)
                if(strcmp(value, surfaces[i].name) == 0) break;
            if(i == sizeof surfaces / sizeof surfaces[0]) return false;
            options->m2_surface = surfaces[i].surface;
            options->m2_surface_set = true;
        }
        else if(strcmp(option, "--m2-tab") == 0) {
            static const struct {
                const char *name;
                po_m2_pick_tab_t tab;
            } tabs[] = {
                {"free", PO_M2_PICK_FREE}, {"recipe", PO_M2_PICK_RECIPE},
                {"history", PO_M2_PICK_HISTORY}
            };
            unsigned i;
            for(i = 0; i < sizeof tabs / sizeof tabs[0]; ++i)
                if(strcmp(value, tabs[i].name) == 0) break;
            if(i == sizeof tabs / sizeof tabs[0]) return false;
            options->m2_tab = tabs[i].tab;
            options->m2_tab_set = true;
        }
        else if(strcmp(option, "--m2-editor") == 0) {
            static const struct {
                const char *name;
                po_m2_editor_t editor;
            } editors[] = {
                {"keypad", PO_M2_EDITOR_KEYPAD}, {"wheel", PO_M2_EDITOR_WHEEL}
            };
            unsigned i;
            for(i = 0; i < sizeof editors / sizeof editors[0]; ++i)
                if(strcmp(value, editors[i].name) == 0) break;
            if(i == sizeof editors / sizeof editors[0]) return false;
            options->m2_editor = editors[i].editor;
            options->m2_editor_set = true;
        }
        else if(strcmp(option, "--theme") == 0) {
            if(strcmp(value, "light") == 0) options->theme_light = true;
            else if(strcmp(value, "dark") != 0) return false;
        }
        else if(strcmp(option, "--screenshot") == 0) {
            if(!value[0]) return false;
            options->screenshot_path = value;
        }
        else if(strcmp(option, "--resource-pack") == 0) {
            if(!value[0]) return false;
            options->resource_pack_path = value;
        }
        else return false;
    }
    if(settings_screen_set && options->screen != SCREEN_SETTINGS) return false;
    if(options->container_phase_set && options->screen != SCREEN_SETTINGS) return false;
    if((options->m2_surface_set || options->m2_tab_set || options->m2_editor_set) &&
       options->screen != SCREEN_M2_READY) return false;
    if(options->resource_pack_path && options->screen != SCREEN_READY) return false;
    /* The language page takes precedence; the boot variant would silently never run. */
    if((options->screen == SCREEN_LANGUAGE || options->page != SIM_PAGE_NONE) && boot_animation_set)
        fprintf(stderr, "Note: --boot-animation is ignored with --screen %s; "
                        "the page preview takes precedence.\n",
                screen_name);
    return true;
}

#ifdef SIMULATOR_OPTIONS_TEST
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

int main(void)
{
    simulator_options_t options;
    char *defaults[] = {"simulator"};
    assert(simulator_options_parse(1, defaults, &options));
    assert(options.boot_animation == PO_BOOT_H && options.screen == SCREEN_AUTO);
    assert(!options.language_set && options.language == po_ui_locale_find("en"));
    for(unsigned variant = 0; variant < PO_BOOT_COUNT; ++variant) {
        char *names[] = {"A", "G", "H"};
        char *args[] = {"simulator", "--screen", "boot", "--boot-animation", names[variant],
                        "--language", "zh-Hans", "--screenshot", "/tmp/boot.ppm"};
        assert(simulator_options_parse(9, args, &options));
        assert(options.boot_animation == (po_boot_variant_t)variant);
        assert(options.screen == SCREEN_BOOT && options.language_set);
        assert(options.language == po_ui_locale_find("zh-Hans"));
        assert(strcmp(options.screenshot_path, "/tmp/boot.ppm") == 0);
    }
    char *bad[] = {"simulator", "--boot-animation", "I"};
    assert(!simulator_options_parse(3, bad, &options));
    char *unsupported_locale[] = {"simulator", "--language", "ja"};
    assert(!simulator_options_parse(3, unsupported_locale, &options));
    char *supported_locale[] = {"simulator", "--language", "en"};
    assert(simulator_options_parse(3, supported_locale, &options));
    assert(options.language == po_ui_locale_find("en"));
    assert(!simulator_options_parse(2, bad, &options));
    char *empty[] = {"simulator", "--screenshot", ""};
    assert(!simulator_options_parse(3, empty, &options));
    char *unknown[] = {"simulator", "--unknown", "A"};
    assert(!simulator_options_parse(3, unknown, &options));
    char *light[] = {"simulator", "--theme", "light"};
    assert(simulator_options_parse(3, light, &options) && options.theme_light);
    char *dark[] = {"simulator", "--theme", "dark"};
    assert(simulator_options_parse(3, dark, &options) && !options.theme_light);
    char *gray[] = {"simulator", "--theme", "gray"};
    assert(!simulator_options_parse(3, gray, &options));
    char *phase_args[] = {"simulator", "--screen", "settings", "--container-phase", "ready"};
    assert(simulator_options_parse(5, phase_args, &options) && options.container_phase_set &&
           options.container_phase == PO_SET_CONT_READY);
    char *phase_bad[] = {"simulator", "--screen", "settings", "--container-phase", "done"};
    assert(!simulator_options_parse(5, phase_bad, &options));
    char *phase_wrong[] = {"simulator", "--screen", "ready", "--container-phase", "ready"};
    assert(!simulator_options_parse(5, phase_wrong, &options));
    char *m2_args[] = {"simulator", "--screen", "m2-ready", "--m2-surface", "pick",
                       "--m2-tab", "history", "--m2-editor", "keypad"};
    assert(simulator_options_parse(9, m2_args, &options));
    assert(options.m2_surface_set && options.m2_surface == PO_M2_READY_PICK);
    assert(options.m2_tab_set && options.m2_tab == PO_M2_PICK_HISTORY);
    assert(options.m2_editor_set && options.m2_editor == PO_M2_EDITOR_KEYPAD);
    char *m2_wheel[] = {"simulator", "--screen", "m2-ready", "--m2-surface", "dose",
                        "--m2-editor", "wheel"};
    assert(simulator_options_parse(7, m2_wheel, &options) && options.m2_surface_set &&
           options.m2_surface == PO_M2_READY_DOSE && options.m2_editor_set &&
           options.m2_editor == PO_M2_EDITOR_WHEEL);
    char *m2_bad_surface[] = {"simulator", "--screen", "m2-ready", "--m2-surface", "brew"};
    assert(!simulator_options_parse(5, m2_bad_surface, &options));
    char *m2_bad_tab[] = {"simulator", "--screen", "m2-ready", "--m2-tab", "steps"};
    assert(!simulator_options_parse(5, m2_bad_tab, &options));
    char *m2_bad_editor[] = {"simulator", "--screen", "m2-ready", "--m2-editor", "dial"};
    assert(!simulator_options_parse(5, m2_bad_editor, &options));
    char *m2_wrong_screen[] = {"simulator", "--screen", "ready", "--m2-surface", "dose"};
    assert(!simulator_options_parse(5, m2_wrong_screen, &options));
    static const struct { const char *name; simulator_screen_t screen; } previews[] = {
        {"boot", SCREEN_BOOT}, {"language", SCREEN_LANGUAGE}, {"ready", SCREEN_READY},
        {"settings", SCREEN_SETTINGS}, {"m2-ready", SCREEN_M2_READY}, {"m2-brew", SCREEN_M2_BREW},
        {"m2-result", SCREEN_M2_RESULT}, {"container", SCREEN_CONTAINER},
        {"brew-settings", SCREEN_BREW_SETTINGS}, {"result", SCREEN_RESULT}
    };
    for(unsigned i = 0; i < sizeof previews / sizeof previews[0]; ++i) {
        char *args[] = {"simulator", "--screen", (char *)previews[i].name};
        assert(simulator_options_parse(3, args, &options));
        assert(options.screen == previews[i].screen);
    }
    static const struct { const char *name; po_settings_screen_t screen; } settings[] = {
        {"home", PO_SET_HOME}, {"history", PO_SET_HISTORY},
        {"history-delete", PO_SET_HISTORY_DELETE}, {"history-clear", PO_SET_HISTORY_CLEAR},
        {"prefs", PO_SET_PREFS}, {"volume", PO_SET_VOLUME}, {"brightness", PO_SET_BRIGHTNESS},
        {"unit", PO_SET_UNIT}, {"language", PO_SET_LANGUAGE}, {"other", PO_SET_OTHER},
        {"wake", PO_SET_WAKE}, {"auto-off", PO_SET_AUTO_OFF}, {"brew", PO_SET_BREW},
        {"container", PO_SET_CONTAINER}, {"container-edit", PO_SET_CONTAINER_EDIT},
        {"calibrate", PO_SET_CALIBRATE}, {"about", PO_SET_ABOUT}, {"reset", PO_SET_RESET}
    };
    for(unsigned i = 0; i < sizeof settings / sizeof settings[0]; ++i) {
        char *args[] = {"simulator", "--settings-screen", (char *)settings[i].name, "--screen", "settings"};
        assert(simulator_options_parse(5, args, &options));
        assert(options.settings_screen == settings[i].screen);
    }
    char *removed[] = {"simulator", "--screen", "settings", "--settings-screen", "history-detail"};
    assert(!simulator_options_parse(5, removed, &options));
    char *wrong_parent[] = {"simulator", "--screen", "ready", "--settings-screen", "volume"};
    assert(!simulator_options_parse(5, wrong_parent, &options));
    FILE *captured = freopen("/tmp/simulator_options_warning.txt", "w+", stderr);
    assert(captured);
    char *conflict[] = {"simulator", "--screen", "language", "--boot-animation", "A"};
    assert(simulator_options_parse(5, conflict, &options));
    assert(options.screen == SCREEN_LANGUAGE && options.boot_animation == PO_BOOT_A);
    char *conflict_late[] = {"simulator", "--boot-animation", "H", "--screen", "language"};
    assert(simulator_options_parse(5, conflict_late, &options));
    assert(options.screen == SCREEN_LANGUAGE && options.boot_animation == PO_BOOT_H);
    fflush(stderr);
    char line[256];
    rewind(captured);
    unsigned warnings = 0;
    while(fgets(line, sizeof(line), captured))
        if(strstr(line, "--boot-animation") && strstr(line, "--screen language")) warnings++;
    char *ready_args[] = {"simulator", "--screen", "ready"};
    assert(simulator_options_parse(3, ready_args, &options));
    assert(options.screen == SCREEN_READY);
    char *resource_args[] = {"simulator", "--screen", "ready", "--resource-pack", "ready.por"};
    assert(simulator_options_parse(5, resource_args, &options));
    assert(options.resource_pack_path && strcmp(options.resource_pack_path, "ready.por") == 0);
    char *resource_auto[] = {"simulator", "--resource-pack", "ready.por"};
    assert(!simulator_options_parse(3, resource_auto, &options));
    char *resource_boot[] = {"simulator", "--screen", "boot", "--resource-pack", "ready.por"};
    assert(!simulator_options_parse(5, resource_boot, &options));
    char *resource_empty[] = {"simulator", "--screen", "ready", "--resource-pack", ""};
    assert(!simulator_options_parse(5, resource_empty, &options));
    assert(warnings == 2);
    assert(fseek(captured, 0, SEEK_END) == 0);
    long usage_start = ftell(captured);
    simulator_options_usage("simulator");
    fflush(stderr);
    assert(fseek(captured, usage_start, SEEK_SET) == 0);
    char usage[4096] = {0};
    assert(fread(usage, 1, sizeof usage - 1, captured) > 0);
    for(unsigned i = 0; i < sizeof previews / sizeof previews[0]; ++i)
        assert(strstr(usage, previews[i].name));
    for(unsigned i = 0; i < sizeof settings / sizeof settings[0]; ++i)
        assert(strstr(usage, settings[i].name));
    assert(!strstr(usage, "history-detail"));
    puts("PASS: simulator defaults, A/G/H selection, catalog locales, capture, conflict warning and invalid arguments");
    return 0;
}
#endif
