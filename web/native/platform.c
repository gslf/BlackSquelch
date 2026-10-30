#include "platform.h"
#include <emscripten/emscripten.h>
#include <stdio.h>
#include <time.h>
double bs_time(void) { return emscripten_get_now() / 1000.0; }
void bs_timestamp(char *buffer, size_t capacity) {
    time_t now = time(NULL);
    struct tm *value = localtime(&now);
    if (value) strftime(buffer, capacity, "%H:%M:%S", value);
    else snprintf(buffer, capacity, "--:--:--");
}
bool bs_config_path(char *buffer, size_t capacity) {
    return snprintf(buffer, capacity, "/data/config.txt") < (int)capacity;
}
