#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "simulator_preferences.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#define simulator_getpid _getpid
#else
#include <unistd.h>
#define simulator_getpid getpid
#endif

bool simulator_preferences_parse_locale(const char *tag, const po_ui_locale_t **locale)
{
    if(tag == NULL || locale == NULL) return false;

    const po_ui_locale_t *found = po_ui_locale_find(tag);
    if(found == NULL) return false;
    *locale = found;
    return true;
}

bool simulator_preferences_read(const char *path, const po_ui_locale_t **locale)
{
    if(path == NULL || locale == NULL) return false;

    FILE *file = fopen(path, "rb");
    if(file == NULL) return false;

    char contents[16];
    size_t length = fread(contents, 1, sizeof(contents), file);
    bool ok = !ferror(file) && length < sizeof(contents);
    if(fclose(file) != 0) ok = false;
    if(!ok || memchr(contents, '\0', length) != NULL) return false;

    if(length > 0 && contents[length - 1] == '\n') length--;
    contents[length] = '\0';
    return simulator_preferences_parse_locale(contents, locale);
}

static bool replace_file(const char *temporary_path, const char *path)
{
#ifdef _WIN32
    return MoveFileExA(temporary_path, path,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary_path, path) == 0;
#endif
}

bool simulator_preferences_write(const char *path, const po_ui_locale_t *locale)
{
    if(path == NULL || locale == NULL || locale->tag == NULL) return false;

    const po_ui_locale_t *canonical_locale = po_ui_locale_find(locale->tag);
    if(canonical_locale == NULL) return false;

    size_t path_length = strlen(path);
    if(path_length > SIZE_MAX - 32) return false;
    size_t temporary_size = path_length + 32;
    char *temporary_path = malloc(temporary_size);
    if(temporary_path == NULL) return false;

    int written = snprintf(temporary_path, temporary_size, "%s.tmp.%lu", path,
                           (unsigned long)simulator_getpid());
    if(written < 0 || (size_t)written >= temporary_size) {
        free(temporary_path);
        return false;
    }

    FILE *file = fopen(temporary_path, "wb");
    if(file == NULL) {
        free(temporary_path);
        return false;
    }

    size_t locale_length = strlen(canonical_locale->tag);
    bool ok = fwrite(canonical_locale->tag, 1, locale_length, file) == locale_length &&
              fputc('\n', file) != EOF && fflush(file) == 0;
    if(fclose(file) != 0) ok = false;
    if(ok) ok = replace_file(temporary_path, path);
    if(!ok) remove(temporary_path);

    free(temporary_path);
    return ok;
}

#ifdef SIMULATOR_PREFERENCES_TEST
#include <sys/stat.h>

static int write_fixture(const char *path, const char *contents)
{
    FILE *file = fopen(path, "wb");
    if(file == NULL) return 0;
    size_t length = strlen(contents);
    int ok = fwrite(contents, 1, length, file) == length;
    if(fclose(file) != 0) ok = 0;
    return ok;
}

#define CHECK(expression) do { \
    if(!(expression)) { \
        fprintf(stderr, "preference self-test failed at line %d: %s\n", __LINE__, #expression); \
        return EXIT_FAILURE; \
    } \
} while(0)

static int fixture_equals(const char *path, const char *contents)
{
    FILE *file = fopen(path, "rb");
    if(file == NULL) return 0;
    size_t length = strlen(contents);
    char actual[32];
    int ok = length < sizeof(actual) && fread(actual, 1, length, file) == length &&
             memcmp(actual, contents, length) == 0 && fgetc(file) == EOF;
    if(fclose(file) != 0) ok = 0;
    return ok;
}

int main(void)
{
    char directory[] = "/tmp/pourover-preferences-test-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);

    char path[sizeof(directory) + 16];
    int path_length = snprintf(path, sizeof(path), "%s/language", directory);
    CHECK(path_length > 0 && (size_t)path_length < sizeof(path));

    const po_ui_locale_t *locale = po_ui_locale_find("en");
    CHECK(locale != NULL);
    CHECK(!simulator_preferences_read(path, &locale));
    CHECK(locale == po_ui_locale_find("en"));

    CHECK(po_ui_locale_count() == 2);
    for(unsigned i = 0; i < po_ui_locale_count(); i++) {
        const po_ui_locale_t *expected = po_ui_locale_at(i);
        const po_ui_locale_t *parsed = NULL;
        CHECK(expected != NULL);
        CHECK(simulator_preferences_parse_locale(expected->tag, &parsed));
        CHECK(parsed == expected);
        CHECK(simulator_preferences_write(path, expected));
        char stored[32];
        int stored_length = snprintf(stored, sizeof(stored), "%s\n", expected->tag);
        CHECK(stored_length > 0 && (size_t)stored_length < sizeof(stored));
        CHECK(fixture_equals(path, stored));
        parsed = NULL;
        CHECK(simulator_preferences_read(path, &parsed));
        CHECK(parsed == expected);
    }
    CHECK(!simulator_preferences_parse_locale("EN", &locale));
    CHECK(!simulator_preferences_parse_locale("zh-Hans\n", &locale));

    CHECK(write_fixture(path, "ja"));
    CHECK(!simulator_preferences_read(path, &locale));
    CHECK(fixture_equals(path, "ja"));
    CHECK(write_fixture(path, "zh-Hant"));
    CHECK(!simulator_preferences_read(path, &locale));
    CHECK(fixture_equals(path, "zh-Hant"));
    CHECK(write_fixture(path, "en\n\n"));
    CHECK(!simulator_preferences_read(path, &locale));
    CHECK(write_fixture(path, "en\r\n"));
    CHECK(!simulator_preferences_read(path, &locale));
    CHECK(write_fixture(path, "not-a-locale"));
    CHECK(!simulator_preferences_read(path, &locale));
    CHECK(write_fixture(path, "0123456789abcdef"));
    CHECK(!simulator_preferences_read(path, &locale));
    FILE *binary = fopen(path, "wb");
    CHECK(binary != NULL);
    const char invalid[] = {'e', 'n', '\0', 'x'};
    CHECK(fwrite(invalid, 1, sizeof(invalid), binary) == sizeof(invalid));
    CHECK(fclose(binary) == 0);
    CHECK(!simulator_preferences_read(path, &locale));

    const po_ui_locale_t *simplified_chinese = po_ui_locale_find("zh-Hans");
    CHECK(simplified_chinese != NULL);
    CHECK(simulator_preferences_write(path, simplified_chinese));
    const po_ui_locale_t unsupported = {"ja", "Japanese"};
    CHECK(!simulator_preferences_write(path, &unsupported));
    CHECK(!simulator_preferences_write(path, NULL));
    CHECK(simulator_preferences_read(path, &locale));
    CHECK(locale == simplified_chinese);

    char blocked_temporary[sizeof(path) + 32];
    int temporary_length = snprintf(blocked_temporary, sizeof(blocked_temporary),
                                    "%s.tmp.%lu", path, (unsigned long)getpid());
    CHECK(temporary_length > 0 && (size_t)temporary_length < sizeof(blocked_temporary));
    CHECK(mkdir(blocked_temporary, 0700) == 0);
    CHECK(!simulator_preferences_write(path, po_ui_locale_find("en")));
    CHECK(simulator_preferences_read(path, &locale));
    CHECK(locale == simplified_chinese);
    CHECK(rmdir(blocked_temporary) == 0);

    char missing_parent[sizeof(directory) + 24];
    path_length = snprintf(missing_parent, sizeof(missing_parent), "%s/missing/language", directory);
    CHECK(path_length > 0 && (size_t)path_length < sizeof(missing_parent));
    CHECK(!simulator_preferences_write(missing_parent, po_ui_locale_find("en")));
    CHECK(simulator_preferences_read(path, &locale));
    CHECK(locale == simplified_chinese);

    CHECK(remove(path) == 0);
    CHECK(rmdir(directory) == 0);
    puts("preference parser/read/write self-test passed");
    return EXIT_SUCCESS;
}
#endif
