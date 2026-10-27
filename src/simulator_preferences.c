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

static const struct {
    const char *locale;
    po_language_t language;
} locales[] = {
    {"en", PO_LANG_EN},
    {"zh-Hans", PO_LANG_ZH_HANS},
    {"zh-Hant", PO_LANG_ZH_HANT},
    {"ja", PO_LANG_JA},
    {"ko", PO_LANG_KO},
    {"fr", PO_LANG_FR},
    {"de", PO_LANG_DE},
    {"es", PO_LANG_ES},
};

const char *simulator_preferences_locale(po_language_t language)
{
    for(size_t i = 0; i < sizeof(locales) / sizeof(locales[0]); i++) {
        if(locales[i].language == language) return locales[i].locale;
    }
    return NULL;
}

bool simulator_preferences_parse_locale(const char *locale, po_language_t *language)
{
    if(locale == NULL || language == NULL) return false;

    for(size_t i = 0; i < sizeof(locales) / sizeof(locales[0]); i++) {
        if(strcmp(locale, locales[i].locale) == 0) {
            *language = locales[i].language;
            return true;
        }
    }
    return false;
}

bool simulator_preferences_read(const char *path, po_language_t *language)
{
    if(path == NULL || language == NULL) return false;

    FILE *file = fopen(path, "rb");
    if(file == NULL) return false;

    char contents[16];
    size_t length = fread(contents, 1, sizeof(contents), file);
    bool ok = !ferror(file) && length < sizeof(contents);
    if(fclose(file) != 0) ok = false;
    if(!ok || memchr(contents, '\0', length) != NULL) return false;

    if(length > 0 && contents[length - 1] == '\n') length--;
    contents[length] = '\0';
    return simulator_preferences_parse_locale(contents, language);
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

bool simulator_preferences_write(const char *path, po_language_t language)
{
    if(path == NULL) return false;

    const char *locale = simulator_preferences_locale(language);
    if(locale == NULL) return false;

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

    size_t locale_length = strlen(locale);
    bool ok = fwrite(locale, 1, locale_length, file) == locale_length &&
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

int main(void)
{
    char directory[] = "/tmp/pourover-preferences-test-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);

    char path[sizeof(directory) + 16];
    int path_length = snprintf(path, sizeof(path), "%s/language", directory);
    CHECK(path_length > 0 && (size_t)path_length < sizeof(path));

    po_language_t language = PO_LANG_ES;
    CHECK(!simulator_preferences_read(path, &language));
    CHECK(language == PO_LANG_ES);

    for(size_t i = 0; i < sizeof(locales) / sizeof(locales[0]); i++) {
        po_language_t parsed = PO_LANG_COUNT;
        CHECK(simulator_preferences_parse_locale(locales[i].locale, &parsed));
        CHECK(parsed == locales[i].language);
        CHECK(strcmp(simulator_preferences_locale(parsed), locales[i].locale) == 0);
    }
    CHECK(!simulator_preferences_parse_locale("EN", &language));
    CHECK(!simulator_preferences_parse_locale("zh-Hans\n", &language));

    CHECK(simulator_preferences_write(path, PO_LANG_ZH_HANT));
    CHECK(simulator_preferences_read(path, &language));
    CHECK(language == PO_LANG_ZH_HANT);
    CHECK(write_fixture(path, "fr"));
    CHECK(simulator_preferences_read(path, &language));
    CHECK(language == PO_LANG_FR);

    CHECK(write_fixture(path, "en\n\n"));
    CHECK(!simulator_preferences_read(path, &language));
    CHECK(write_fixture(path, "en\r\n"));
    CHECK(!simulator_preferences_read(path, &language));
    CHECK(write_fixture(path, "not-a-locale"));
    CHECK(!simulator_preferences_read(path, &language));
    FILE *binary = fopen(path, "wb");
    CHECK(binary != NULL);
    const char invalid[] = {'e', 'n', '\0', 'x'};
    CHECK(fwrite(invalid, 1, sizeof(invalid), binary) == sizeof(invalid));
    CHECK(fclose(binary) == 0);
    CHECK(!simulator_preferences_read(path, &language));

    CHECK(simulator_preferences_write(path, PO_LANG_KO));
    CHECK(!simulator_preferences_write(path, PO_LANG_COUNT));
    CHECK(simulator_preferences_read(path, &language));
    CHECK(language == PO_LANG_KO);

    char blocked_temporary[sizeof(path) + 32];
    int temporary_length = snprintf(blocked_temporary, sizeof(blocked_temporary),
                                    "%s.tmp.%lu", path, (unsigned long)getpid());
    CHECK(temporary_length > 0 && (size_t)temporary_length < sizeof(blocked_temporary));
    CHECK(mkdir(blocked_temporary, 0700) == 0);
    CHECK(!simulator_preferences_write(path, PO_LANG_FR));
    CHECK(simulator_preferences_read(path, &language));
    CHECK(language == PO_LANG_KO);
    CHECK(rmdir(blocked_temporary) == 0);

    char missing_parent[sizeof(directory) + 24];
    path_length = snprintf(missing_parent, sizeof(missing_parent), "%s/missing/language", directory);
    CHECK(path_length > 0 && (size_t)path_length < sizeof(missing_parent));
    CHECK(!simulator_preferences_write(missing_parent, PO_LANG_FR));
    CHECK(simulator_preferences_read(path, &language));
    CHECK(language == PO_LANG_KO);

    CHECK(remove(path) == 0);
    CHECK(rmdir(directory) == 0);
    puts("preference parser/read/write self-test passed");
    return EXIT_SUCCESS;
}
#endif
