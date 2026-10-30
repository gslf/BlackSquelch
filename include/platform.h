#ifndef BS_PLATFORM_H
#define BS_PLATFORM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum {
    BS_KEY_UP = 256,
    BS_KEY_DOWN,
    BS_KEY_PAGE_UP,
    BS_KEY_PAGE_DOWN,
    BS_KEY_END,
    BS_KEY_DELETE,
    BS_KEY_BACK_TAB
};
double bs_time(void);
void bs_sleep(unsigned milliseconds);
bool bs_terminal_open(void);
void bs_terminal_close(void);
int bs_key(void);
void bs_terminal_size(unsigned *columns, unsigned *rows);
bool bs_quit_requested(void);
void bs_timestamp(char *buffer, size_t capacity);
bool bs_config_path(char *buffer, size_t capacity);
#endif
