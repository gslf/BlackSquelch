#include "app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char output[131072];
static void snapshot(BsApp *a, const char *path, unsigned columns, unsigned rows) {
    size_t count = bs_tui_format(a, columns, rows, true, output, sizeof output);
    assert(count && output[count] == 0);
    assert(!strstr(output, "\033[2K"));
    assert(!strstr(output, "Manual PTT") && !strstr(output, "confirmation"));
    const char *prefix = strstr(output, "C-x");
    assert(prefix && !strstr(prefix + 3, "C-x"));
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(output, 1, count, file) == count);
    fclose(file);
}
static void test_receive_line(BsApp *a) {
    BsFrame frame;
    assert(bs_frame_build(&frame, "IU2XYZ", "HELLO"));
    bs_parser_init(&a->parser, bs_log_event, &a->log);
    a->log.current[0] = 0;
    snapshot(a, "tui-waiting.ansi", 100, 24);
    assert(strstr(output, "Listening for incoming messages."));
    for (size_t i = 0; i < frame.count; ++i) {
        bs_parser_bit(&a->parser, frame.bits[i], 1);
        if (i + 1 < frame.count) {
            /* Complete fields must not blank the line between consecutive bit events. */
            assert(*a->log.current);
        }
    }
    assert(!*a->log.current && !bs_parser_active(&a->parser));
}
int main(void) {
    BsApp *a = calloc(1, sizeof *a);
    assert(a);
    a->audio = (BsAudio *)a; /* Rendering only tests presence, never dereferences the handle. */
    strcpy(a->config.call, "IU2ABC");
    a->config.tones = 8;
    strcpy(a->compose, "HELLO FROM THE HILL. SIGNAL IS CLEAR.");
    strcpy(a->instruction, "Listening. Compose a short message, then press Enter.");
    strcpy(a->log.current, "TEXT [3/6] 010... | receiving character");
    strcpy(a->log.call, "IU2XYZ");
    strcpy(a->log.text, "GOOD MORNING");
    bs_log_add(&a->log, "RX MFSK8 LOCKED: FEC");
    bs_log_add(&a->log, "SYNC 00011010110011111111110000011101 OK");
    bs_log_message(&a->log, "RX VERIFIED", "IU2XYZ", "GOOD MORNING. HOW IS THE SIGNAL?");
    a->show_log = true;
    snapshot(a, "tui-compose.ansi", 100, 24);
    assert(strstr(output, "\\--- BlackSquelch") && strstr(output, ":#/ GSLF"));
    assert(strstr(output, "TECHNICAL LOG") && strstr(output, "010..."));
    assert(strstr(output, "COMPOSE") && strstr(output, "HELLO FROM THE HILL"));
    a->show_log = false;
    snapshot(a, "tui-conversation.ansi", 100, 24);
    assert(!strstr(output, "TECHNICAL LOG") && strstr(output, "CONVERSATION"));
    a->state = BS_FILE_SELECT;
    strcpy(a->file_path, "/tmp/My Image.png");
    snapshot(a, "tui-file.ansi", 100, 24);
    assert(strstr(output, "FILE PATH") && strstr(output, "/tmp/My Image.png_"));
    a->state = BS_COMPOSE;
    a->command_menu = true;
    snapshot(a, "tui-menu.ansi", 100, 24);
    assert(strstr(output, " MENU ") && strstr(output, "Send a file"));
    assert(strstr(output, "Quit") && !strstr(output, "Abort local reception"));
    a->state = BS_PLAYING;
    strcpy(a->instruction, "KEEP PTT PRESSED | 50.0% | 8.0 seconds remaining | PLAYING");
    snapshot(a, "tui-transmitting.ansi", 80, 24);
    assert(strstr(output, "KEEP PTT PRESSED") && strstr(output, "Cancel current operation"));
    assert(!strstr(output, "Send a file"));
    a->state = BS_COMPOSE;
    a->command_menu = false;
    test_receive_line(a);
    a->state = BS_MACROS;
    strcpy(a->instruction, "Choose a shortcut. Enter edits its message; Ctrl+S saves all changes.");
    strcpy(a->editing_macros.text[0], "CQ CQ CQ DE IU2ABC");
    a->macro_slot = 0;
    snapshot(a, "tui-macros.ansi", 100, 24);
    assert(strstr(output, "MACROS") && strstr(output, "Ctrl+X 1") && strstr(output, "CQ CQ CQ"));
    a->macro_editing = true;
    strcpy(a->macro_text, "SHORTCUT MESSAGE");
    snapshot(a, "tui-macro-edit.ansi", 80, 24);
    assert(strstr(output, "EDIT MESSAGE") && strstr(output, "SHORTCUT MESSAGE_"));
    a->state = BS_SETTINGS;
    a->editing = a->config;
    a->editing.amplitude = 0.5;
    a->command_menu = false;
    strcpy(a->instruction, "Set callsign and audio devices, then press Enter.");
    snapshot(a, "tui-settings.ansi", 100, 24);
    assert(strstr(output, "Exactly 6 ASCII letters/digits"));
    for (unsigned rows = 1; rows <= 30; ++rows) {
        for (unsigned columns = 1; columns <= 120; columns += 7) {
            const BsState states[] = {BS_COMPOSE, BS_SETTINGS, BS_FILE_SELECT, BS_MACROS,
                                      BS_PLAYING};
            a->state = states[(rows + columns) % 5];
            a->macro_slot = (rows + columns) % BS_MACRO_COUNT;
            a->command_menu = !a->command_menu;
            a->command_scroll = 999;
            assert(bs_tui_format(a, columns, rows, true, output, sizeof output));
        }
    }
    a->command_menu = false;
    a->state = BS_SETTINGS;
    strcpy(a->editing.call, "\033[2J");
    snapshot(a, "tui-small.ansi", 40, 8);
    assert(!strstr(output, "\033[2J") && strstr(output, "?[2J"));
    assert(!bs_tui_format(a, 100, 24, false, output, 16));
    free(a);
    puts("PASS TUI: panels, command availability, visible PTT, resize bounds, escape sanitization");
}
