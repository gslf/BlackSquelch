#ifndef BS_APP_H
#define BS_APP_H
#include "config.h"
#include "modem.h"
#include "log.h"
#include "files.h"
#include "macros.h"
#define BS_HISTORY_COUNT 32
typedef enum {
    BS_COMPOSE,
    BS_WAIT,
    BS_COUNTDOWN,
    BS_PLAYING,
    BS_DRAINING,
    BS_RELEASE,
    BS_SETTINGS,
    BS_FILE_SELECT,
    BS_MACROS
} BsState;
typedef struct {
    BsConfig config, editing;
    BsAudio *audio;
    BsParser parser;
    BsReceiver receiver;
    BsTransmitter transmitter;
    BsFrame frame;
    BsLog log;
    BsMacros macros, editing_macros;
    BsState macro_return_state;
    unsigned macro_slot;
    bool macro_editing, show_log;
    char macro_text[BS_MAX_TEXT + 1], macros_path[1024];
    char history[BS_HISTORY_COUNT][BS_MAX_TEXT + 1], history_draft[BS_MAX_TEXT + 1];
    size_t history_count, history_head, history_offset;
    BsFile outgoing_file;
    BsFileReception incoming_file;
    char file_path[1024], file_status[512];
    bool tx_file, last_is_file, tx_report, repair_pending, have_outgoing_file;
    uint8_t requested[BS_FILE_BLOCKS];
    uint16_t plan_indices[BS_FILE_BLOCKS];
    BsRepairRequest reports[BS_FILE_BLOCKS / BS_REPAIR_WINDOW];
    char frozen_call[BS_CALL_CHARS + 1], last_file_call[BS_CALL_CHARS + 1];
    size_t plan_count, plan_position;
    uint64_t total_samples;
    size_t tx_file_index, total_bits;
    BsState state;
    char compose[BS_MAX_TEXT + 1], last[BS_MAX_TEXT + 1], frozen[BS_MAX_TEXT + 1];
    char instruction[512], config_path[1024];
    BsDevice inputs[BS_DEVICE_LIMIT], outputs[BS_DEVICE_LIMIT];
    size_t input_count, output_count, input_index, output_index;
    unsigned setting, frozen_tones;
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
    const char *group;
} BsCommand;
extern const BsCommand bs_commands[];
extern const size_t bs_command_count;
bool bs_command_available(const BsApp *app, char key);
int bs_app_run(void);
size_t bs_tui_format(const BsApp *app, unsigned columns, unsigned rows, bool color, char *output,
                     size_t capacity);
void bs_tui_render(const BsApp *app);
#endif
