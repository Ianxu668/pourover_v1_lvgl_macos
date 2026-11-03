#include "simulator_options.h"
#include "simulator_preferences.h"
#include <stdio.h>
#include <string.h>

void simulator_options_usage(const char *program)
{
    fprintf(stderr, "Usage: %s [--screen boot|language] [--boot-animation A|G|H] "
                    "[--language locale] [--screenshot path.ppm]\n", program);
}

bool simulator_options_parse(int argc, char **argv, simulator_options_t *options)
{
    *options = (simulator_options_t){.screen = SCREEN_AUTO, .boot_animation = PO_BOOT_H,
                                     .language = PO_LANG_EN};
    bool boot_animation_set = false;
    for(int i = 1; i < argc; ++i) {
        if(i + 1 == argc) return false;
        const char *option = argv[i++], *value = argv[i];
        if(strcmp(option, "--screen") == 0) {
            if(strcmp(value, "boot") == 0) options->screen = SCREEN_BOOT;
            else if(strcmp(value, "language") == 0) options->screen = SCREEN_LANGUAGE;
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
        else return false;
    }
    /* The language page takes precedence; the boot variant would silently never run. */
    if(options->screen == SCREEN_LANGUAGE && boot_animation_set)
        fprintf(stderr, "Note: --boot-animation is ignored with --screen language; "
                        "the language preview takes precedence.\n");
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
    for(unsigned variant = 0; variant < PO_BOOT_COUNT; ++variant) {
        char *names[] = {"A", "G", "H"};
        char *args[] = {"simulator", "--screen", "boot", "--boot-animation", names[variant],
                        "--language", "ja", "--screenshot", "/tmp/boot.ppm"};
        assert(simulator_options_parse(9, args, &options));
        assert(options.boot_animation == (po_boot_variant_t)variant);
        assert(options.screen == SCREEN_BOOT && options.language_set && options.language == PO_LANG_JA);
        assert(strcmp(options.screenshot_path, "/tmp/boot.ppm") == 0);
    }
    char *bad[] = {"simulator", "--boot-animation", "I"};
    assert(!simulator_options_parse(3, bad, &options));
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
    assert(warnings == 2);
    puts("PASS: simulator defaults, A/G/H selection, locale, capture, conflict warning and invalid arguments");
    return 0;
}
#endif
