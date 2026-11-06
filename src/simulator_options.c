#include "simulator_options.h"
#include "simulator_preferences.h"
#include <stdio.h>
#include <string.h>

void simulator_options_usage(const char *program)
{
    fprintf(stderr, "Usage: %s [--screen boot|language|ready] [--boot-animation A|G|H] "
                    "[--language locale] [--resource-pack path.por] [--screenshot path.ppm]\n",
            program);
}

bool simulator_options_parse(int argc, char **argv, simulator_options_t *options)
{
    *options = (simulator_options_t){.screen = SCREEN_AUTO, .boot_animation = PO_BOOT_H,
                                     .language = po_ui_locale_find("en")};
    bool boot_animation_set = false;
    for(int i = 1; i < argc; ++i) {
        if(i + 1 == argc) return false;
        const char *option = argv[i++], *value = argv[i];
        if(strcmp(option, "--screen") == 0) {
            if(strcmp(value, "boot") == 0) options->screen = SCREEN_BOOT;
            else if(strcmp(value, "language") == 0) options->screen = SCREEN_LANGUAGE;
            else if(strcmp(value, "ready") == 0) options->screen = SCREEN_READY;
            else return false;
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
    if(options->resource_pack_path && options->screen != SCREEN_READY) return false;
    /* The language page takes precedence; the boot variant would silently never run. */
    if((options->screen == SCREEN_LANGUAGE || options->screen == SCREEN_READY) && boot_animation_set)
        fprintf(stderr, "Note: --boot-animation is ignored with --screen %s; "
                        "the page preview takes precedence.\n",
                options->screen == SCREEN_LANGUAGE ? "language" : "ready");
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
    puts("PASS: simulator defaults, A/G/H selection, catalog locales, capture, conflict warning and invalid arguments");
    return 0;
}
#endif
