#include "macros.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
bool bs_macros_path(const char *config_path, char *path, size_t capacity) {
    const char *base = config_path;
    for (const char *p = config_path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    if (!*config_path) {
        return false;
    }
    int n = snprintf(path, capacity, "%.*smacros.txt", (int)(base - config_path), config_path);
    return n > 0 && (size_t)n < capacity;
}
static bool valid_macros(BsMacros *macros) {
    for (unsigned i = 0; i < BS_MACRO_COUNT; ++i) {
        if (macros->text[i][BS_MAX_TEXT] ||
            (*macros->text[i] && !bs_normalize(macros->text[i], false, NULL))) {
            return false;
        }
    }
    return true;
}
bool bs_macros_load(const char *path, BsMacros *macros) {
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }
    BsMacros candidate = {0};
    char line[BS_MAX_TEXT + 32];
    unsigned seen = 0;
    bool valid = true;
    while (valid && fgets(line, sizeof line, file)) {
        size_t length = strlen(line);
        if (length && line[length - 1] != '\n' && !feof(file)) {
            valid = false;
            break;
        }
        line[strcspn(line, "\r\n")] = 0;
        char *value = strchr(line, '=');
        if (!value) {
            valid = false;
            break;
        }
        *value++ = 0;
        unsigned field;
        if (!strcmp(line, "schema")) {
            field = BS_MACRO_COUNT;
            valid = !strcmp(value, "1");
        } else if (strlen(line) == 6 && !strncmp(line, "macro", 5) && line[5] >= '1' &&
                   line[5] <= '9') {
            field = (unsigned)(line[5] - '1');
            valid = strlen(value) <= BS_MAX_TEXT;
            if (valid) {
                strcpy(candidate.text[field], value);
            }
        } else {
            valid = false;
            break;
        }
        if (seen & (1u << field)) {
            valid = false;
        }
        seen |= 1u << field;
    }
    valid = valid && !ferror(file) && seen == (1u << (BS_MACRO_COUNT + 1)) - 1 &&
            valid_macros(&candidate);
    fclose(file);
    if (valid) {
        *macros = candidate;
    }
    return valid;
}
bool bs_macros_save(const char *path, const BsMacros *macros) {
    BsMacros normalized = *macros;
    if (!valid_macros(&normalized)) {
        return false;
    }
    char temporary[1200];
    int n = snprintf(temporary, sizeof temporary, "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof temporary || !*path) {
        return false;
    }
    FILE *file = fopen(temporary, "w");
    if (!file) {
        return false;
    }
    bool ok = fprintf(file, "schema=1\n") > 0;
    for (unsigned i = 0; i < BS_MACRO_COUNT; ++i) {
        if (fprintf(file, "macro%u=%s\n", i + 1, normalized.text[i]) < 0) {
            ok = false;
        }
    }
    if (fclose(file)) {
        ok = false;
    }
#ifdef _WIN32
    if (ok) {
        ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }
#else
    if (ok) {
        ok = rename(temporary, path) == 0;
    }
#endif
    if (!ok) {
        remove(temporary);
    }
    return ok;
}
