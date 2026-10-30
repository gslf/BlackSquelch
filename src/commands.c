#include "app.h"
const BsCommand bs_commands[] = {
    {'s', "Prepare message", "MESSAGE"},
    {'t', "Send a file", "MESSAGE"},
    {'r', "Repeat / send requested blocks", "MESSAGE"},
    {'u', "Clear text / file path", "MESSAGE"},
    {'k', "Manage shortcuts...", "MACROS"},
    {'w', "Save this message as a macro...", "MACROS"},
    {'1', "Insert macro 1", "MACROS"},
    {'2', "Insert macro 2", "MACROS"},
    {'3', "Insert macro 3", "MACROS"},
    {'4', "Insert macro 4", "MACROS"},
    {'5', "Insert macro 5", "MACROS"},
    {'6', "Insert macro 6", "MACROS"},
    {'7', "Insert macro 7", "MACROS"},
    {'8', "Insert macro 8", "MACROS"},
    {'9', "Insert macro 9", "MACROS"},
    {'m', "Report receipt / request missing blocks", "RECEPTION"},
    {'v', "Retry saving received file", "RECEPTION"},
    {'a', "Abort local reception", "RECEPTION"},
    {'p', "Older log entries", "LOG"},
    {'n', "Newer log entries", "LOG"},
    {'f', "Follow live log", "LOG"},
    {'o', "Settings...", "APPLICATION"},
    {'l', "Show / hide technical log", "APPLICATION"},
    {'e', "Export session log", "APPLICATION"},
    {'d', "Refresh audio devices", "APPLICATION"},
    {'c', "Cancel current operation", "APPLICATION"},
    {'q', "Quit", "APPLICATION"},
};
const size_t bs_command_count = sizeof bs_commands / sizeof bs_commands[0];
bool bs_command_available(const BsApp *a, char key) {
    bool idle = a->state == BS_COMPOSE;
    bool receiving = bs_rx_active(&a->receiver) || bs_parser_active(&a->parser);
    if (key >= '1' && key <= '9') {
        return idle && *a->macros.text[key - '1'];
    }
    switch (key) {
    case 'q':
        return true;
    case 'c':
        return a->state != BS_COMPOSE && a->state != BS_RELEASE;
    case 'd':
        return a->state == BS_SETTINGS;
    case 's':
        return idle && a->audio && *a->compose && !receiving;
    case 'r':
        return idle && a->audio && (*a->last || a->last_is_file) && !receiving;
    case 't':
        return idle && a->audio && !receiving;
    case 'k':
        return idle || a->state == BS_SETTINGS;
    case 'w':
        return idle && *a->compose;
    case 'm':
        return idle && a->audio && !receiving && a->incoming_file.active &&
               (!a->incoming_file.complete || a->incoming_file.saved);
    case 'v':
        return idle && a->incoming_file.complete && !a->incoming_file.saved;
    case 'o':
        return idle && !receiving;
    case 'a':
        return (idle || a->state == BS_WAIT || a->state == BS_FILE_SELECT) &&
               (receiving || (a->incoming_file.active && !a->incoming_file.saved));
    case 'l':
        return a->state != BS_SETTINGS && a->state != BS_MACROS;
    case 'e':
        return idle;
    case 'u':
        return (idle && *a->compose) || (a->state == BS_FILE_SELECT && *a->file_path);
    case 'p':
        return a->show_log && a->state != BS_SETTINGS && a->state != BS_MACROS &&
               a->log.count > a->log.scroll;
    case 'n':
    case 'f':
        return a->show_log && a->state != BS_SETTINGS && a->state != BS_MACROS && a->log.scroll > 0;
    default:
        return false;
    }
}
