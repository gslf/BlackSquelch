#include "config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
static bool valid_device(const char *id, size_t capacity) {
    size_t length = 0;
    while (length < capacity && id[length]) {
        unsigned char byte = (unsigned char)id[length++];
        if (byte < 33 || byte > 126 || byte == '=') {
            return false;
        }
    }
    return length > 0 && length < capacity;
}
static bool valid_config(BsConfig *config) {
    config->tones = bs_tones_effective(config->tones);
    return config->call[BS_CALL_CHARS] == 0 && bs_normalize(config->call, true, NULL) &&
           valid_device(config->input, sizeof config->input) &&
           valid_device(config->output, sizeof config->output) && isfinite(config->amplitude) &&
           config->amplitude >= 0.01 && config->amplitude <= 1 && bs_tones_valid(config->tones);
}
bool bs_config_load(const char *path, BsConfig *config) {
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }
    BsConfig candidate = {0};
    char line[512];
    const char *keys[] = {"schema", "callsign", "input", "output", "amplitude", "tones"};
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
        for (field = 0; field < 6 && strcmp(line, keys[field]); ++field) {
        }
        if (field == 6 || (seen & (1u << field))) {
            valid = false;
            break;
        }
        seen |= 1u << field;
        if (field == 0) {
            valid = !strcmp(value, "2");
        } else if (field <= 3) {
            char *target = field == 1   ? candidate.call
                           : field == 2 ? candidate.input
                                        : candidate.output;
            size_t capacity = field == 1 ? sizeof candidate.call : BS_DEVICE_ID;
            if (strlen(value) >= capacity) {
                valid = false;
            } else {
                strcpy(target, value);
            }
        } else if (field == 4) {
            char *end;
            candidate.amplitude = strtod(value, &end);
            valid = end != value && *end == 0;
        } else {
            valid = !strcmp(value, "8") || !strcmp(value, "16");
            if (valid) {
                candidate.tones = (unsigned)strtoul(value, NULL, 10);
            }
        }
    }
    valid = valid && !ferror(file) && seen == 0x3f && valid_config(&candidate);
    fclose(file);
    if (valid) {
        *config = candidate;
    }
    return valid;
}
bool bs_config_save(const char *path, const BsConfig *config) {
    BsConfig normalized = *config;
    if (!valid_config(&normalized)) {
        return false;
    }
    char temporary[1200];
    int n = snprintf(temporary, sizeof temporary, "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof temporary) {
        return false;
    }
    FILE *file = fopen(temporary, "w");
    if (!file) {
        return false;
    }
    bool ok =
        fprintf(file, "schema=2\ncallsign=%s\ninput=%s\noutput=%s\namplitude=%.6f\ntones=%u\n",
                normalized.call, normalized.input, normalized.output, normalized.amplitude,
                normalized.tones) > 0;
    if (fclose(file)) {
        ok = false;
    }
    /* Replace only a fully written file, so a failed save preserves the last preferences. */
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
