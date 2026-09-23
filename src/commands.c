#include "app.h"
const BsCommand bs_commands[] = {
    {'s', "Prepare message"},
    {'r', "Repeat last message"},
    {'b', "Cycle TX rate: 10 / 25 / 50"},
    {'o', "Open settings"},
    {'a', "Abort local reception"},
    {'e', "Export session log"},
    {'u', "Clear composition"},
    {'p', "Scroll log back"},
    {'n', "Scroll log forward"},
    {'f', "Follow live log"},
    {'d', "Refresh audio devices"},
    {'c', "Cancel current operation"},
    {'q', "Quit"},
};
const size_t bs_command_count = sizeof bs_commands / sizeof bs_commands[0];
bool bs_command_available(const BsApp *a, char key) {
    bool idle = a->state == BS_COMPOSE;
    bool receiving = bs_parser_active(&a->parser);
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
        return idle && a->audio && *a->last && !receiving;
    case 'b':
        return idle;
    case 'o':
        return idle && !receiving;
    case 'a':
        return (idle || a->state == BS_WAIT) && receiving;
    case 'e':
        return idle;
    case 'u':
        return idle && *a->compose;
    case 'p':
        return a->state != BS_SETTINGS && a->log.count > a->log.scroll;
    case 'n':
    case 'f':
        return a->state != BS_SETTINGS && a->log.scroll > 0;
    default:
        return false;
    }
}
