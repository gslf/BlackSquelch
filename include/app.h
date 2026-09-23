#ifndef BS_APP_H
#define BS_APP_H
#include "config.h"
#include "modem.h"
#include "log.h"
typedef enum {
    BS_COMPOSE,
    BS_WAIT,
    BS_COUNTDOWN,
    BS_PLAYING,
    BS_DRAINING,
    BS_RELEASE,
    BS_SETTINGS
} BsState;
typedef struct {
    BsConfig config, editing;
    BsAudio *audio;
    BsParser parser;
    BsReceiver receiver;
    BsTransmitter transmitter;
    BsFrame frame;
    BsLog log;
    BsState state;
    char compose[BS_MAX_TEXT + 1], last[BS_MAX_TEXT + 1], frozen[BS_MAX_TEXT + 1];
    char instruction[512], config_path[1024];
    BsDevice inputs[BS_DEVICE_LIMIT], outputs[BS_DEVICE_LIMIT];
    size_t input_count, output_count, input_index, output_index;
    unsigned setting, frozen_bit_rate;
    int16_t pending[BS_BLOCK];
    size_t pending_count, pending_offset;
    uint64_t played;
    double deadline, last_capture, last_progress, last_render;
    bool quit, command_menu;
    size_t command_scroll;
} BsApp;
typedef struct {
    char key;
    const char *description;
} BsCommand;
extern const BsCommand bs_commands[];
extern const size_t bs_command_count;
bool bs_command_available(const BsApp *app, char key);
int bs_app_run(void);
size_t bs_tui_format(const BsApp *app, unsigned columns, unsigned rows, bool color, char *output,
                     size_t capacity);
void bs_tui_render(const BsApp *app);
#endif
