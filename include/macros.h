#ifndef BS_MACROS_H
#define BS_MACROS_H
#include "protocol.h"
#define BS_MACRO_COUNT 9
typedef struct {
    char text[BS_MACRO_COUNT][BS_MAX_TEXT + 1];
} BsMacros;
bool bs_macros_path(const char *config_path, char *path, size_t capacity);
bool bs_macros_load(const char *path, BsMacros *macros);
bool bs_macros_save(const char *path, const BsMacros *macros);
#endif
