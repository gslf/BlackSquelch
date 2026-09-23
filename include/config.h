#ifndef BS_CONFIG_H
#define BS_CONFIG_H
#include "audio.h"
#include "modem.h"
typedef struct {
    char call[BS_CALL_CHARS + 1], input[BS_DEVICE_ID], output[BS_DEVICE_ID];
    double amplitude;
    unsigned bit_rate;
} BsConfig;
bool bs_config_load(const char *path, BsConfig *config);
bool bs_config_save(const char *path, const BsConfig *config);
#endif
