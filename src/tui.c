#include "app.h"
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SCREEN_ROWS 120
#define SCREEN_COLUMNS 240
enum { NORMAL, MUTED, ACCENT, STRONG, NOTICE, MODAL, MODAL_KEY, SELECTED };
typedef struct {
    char text[SCREEN_ROWS][SCREEN_COLUMNS];
    unsigned char style[SCREEN_ROWS][SCREEN_COLUMNS];
    int rows, columns, left, width;
} Screen;
static void put(Screen *s, int row, int column, int width, unsigned char style, const char *format,
                ...) {
    if (row < 0 || row >= s->rows || column >= s->columns || width <= 0) {
        return;
    }
    char text[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    for (int i = 0; text[i] && i < width && column + i < s->columns; ++i) {
        if (column + i >= 0) {
            unsigned char c = (unsigned char)text[i];
            /* Device names, pasted text and logs cannot inject terminal control sequences. */
            s->text[row][column + i] = c >= 32 && c <= 126 ? (char)c : '?';
            s->style[row][column + i] = style;
        }
    }
}
static void section(Screen *s, int row, const char *title, const char *detail) {
    for (int x = s->left; x < s->left + s->width; ++x) {
        put(s, row, x, 1, MUTED, "-");
    }
    put(s, row, s->left, s->width, ACCENT, "%s ", title);
    int length = (int)strlen(detail);
    if (length + (int)strlen(title) + 3 < s->width) {
        put(s, row, s->left + s->width - length - 1, length + 1, MUTED, " %s", detail);
    }
}
static void wrapped(Screen *s, int row, int lines, int column, int width, unsigned char style,
                    const char *text, bool tail) {
    if (width < 1) {
        return;
    }
    size_t length = strlen(text), room = (size_t)width * (size_t)lines;
    if (tail && length > room) {
        text += length - room;
        length = room;
    }
    for (int i = 0; i < lines && (size_t)i * (size_t)width < length; ++i) {
        put(s, row + i, column, width, style, "%s", text + (size_t)i * (size_t)width);
    }
}
static const char *state_name(BsState state) {
    const char *names[] = {"LISTENING",
                           "READY / HOLD PTT",
                           "COUNTDOWN / HOLD PTT",
                           "TRANSMITTING / HOLD PTT",
                           "DRAINING / HOLD PTT",
                           "RELEASE PTT",
                           "SETTINGS",
                           "SELECT FILE",
                           "MACROS"};
    return names[state];
}
static void header(Screen *s, const BsApp *a) {
    put(s, 0, s->left, s->width, STRONG, "\\--- BlackSquelch");
    if (s->width >= 58) {
        put(s, 0, s->left + 21, 10, MUTED, ":#/ GSLF");
        put(s, 0, s->left + s->width - 25, 25, a->state == BS_RELEASE ? NOTICE : ACCENT, "%s",
            a->state == BS_COMPOSE && !a->audio ? "AUDIO OFFLINE" : state_name(a->state));
    }
    if (a->receiver.tones) {
        put(s, 1, s->left, s->width, MUTED,
            "%s  |  TX %u tones  |  RX %u tones / AUTO  |  Audio %s",
            *a->config.call ? a->config.call : "------", bs_tones_effective(a->config.tones),
            a->receiver.tones, a->audio ? "ready" : "offline");
    } else {
        put(s, 1, s->left, s->width, MUTED, "%s  |  TX %u tones  |  RX AUTO 8/16  |  Audio %s",
            *a->config.call ? a->config.call : "------", bs_tones_effective(a->config.tones),
            a->audio ? "ready" : "offline");
    }
}
static void setting_value(const BsApp *a, unsigned field, char *text, size_t capacity) {
    switch (field) {
    case 0:
        snprintf(text, capacity, "%s_", a->editing.call);
        break;
    case 1:
        snprintf(text, capacity, "%s",
                 a->input_count ? a->inputs[a->input_index].name : "No input devices");
        break;
    case 2:
        snprintf(text, capacity, "%s",
                 a->output_count ? a->outputs[a->output_index].name : "No output devices");
        break;
    case 3:
        snprintf(text, capacity, "%.0f %%", a->editing.amplitude * 100);
        break;
    default:
        snprintf(text, capacity, "%u tones", bs_tones_effective(a->editing.tones));
        break;
    }
}
static void settings_screen(Screen *s, const BsApp *a) {
    const char *labels[] = {"Callsign", "Audio input", "Audio output", "Output level", "TX tones"};
    const char *hints[] = {"Exactly 6 ASCII letters/digits", "Up / Down selects the microphone",
                           "Up / Down selects the speaker", "Up / Down adjusts the transmit level",
                           "8: wider tone spacing | 16: more bits per symbol | RX is automatic"};
    if (s->rows >= 20) {
        section(s, 3, "SETTINGS", "Station / audio / transmission");
        for (unsigned i = 0; i < 5; ++i) {
            char value[256];
            setting_value(a, i, value, sizeof value);
            put(s, 5 + (int)i * 2, s->left, s->width, a->setting == i ? STRONG : NORMAL,
                "%c %-13s %s", a->setting == i ? '>' : ' ', labels[i], value);
            if (a->setting == i) {
                put(s, 6 + (int)i * 2, s->left + 2, s->width - 2, ACCENT, "%s", hints[i]);
            }
        }
    } else {
        char value[256];
        setting_value(a, a->setting, value, sizeof value);
        put(s, 3, s->left, s->width, STRONG, "%u/5 %s: %s", a->setting + 1, labels[a->setting],
            value);
        if (s->rows >= 12) {
            wrapped(s, 5, 2, s->left, s->width, MUTED, hints[a->setting], false);
        }
    }
}
static void composer(Screen *s, const BsApp *a, int row, int lines) {
    if (a->state == BS_FILE_SELECT || ((a->tx_file || a->tx_report) && a->state != BS_COMPOSE)) {
        section(s, row,
                a->state == BS_FILE_SELECT ? "FILE PATH"
                : a->tx_report             ? "FILE REPORT"
                                           : "SEND FILE",
                a->state == BS_FILE_SELECT ? "Any format / max 1 MiB" : "Ready to transmit");
        char text[sizeof a->file_path + 2];
        snprintf(text, sizeof text, "%s%s", a->state == BS_FILE_SELECT ? a->file_path : a->frozen,
                 a->state == BS_FILE_SELECT ? "_" : "");
        wrapped(s, row + 1, lines, s->left, s->width, STRONG, text, true);
        return;
    }
    BsFrame estimate;
    bool valid = bs_frame_build(&estimate, a->config.call, a->compose);
    char detail[128];
    if (a->history_offset) {
        snprintf(detail, sizeof detail, "History %zu/%zu / %zu characters", a->history_offset,
                 a->history_count, strlen(a->compose));
    } else if (valid) {
        snprintf(detail, sizeof detail, "%zu/512 / %.2f s + 2 s PTT", strlen(a->compose),
                 bs_frame_duration(&estimate, bs_tones_effective(a->config.tones)));
    } else {
        snprintf(detail, sizeof detail, "%zu/512%s", strlen(a->compose),
                 a->history_count ? " / Up recalls sent messages" : "");
    }
    section(s, row, "COMPOSE", detail);
    char text[BS_MAX_TEXT + 4];
    snprintf(text, sizeof text, "%s_", a->compose);
    put(s, row + 1, s->left, 2, ACCENT, "> ");
    wrapped(s, row + 1, lines, s->left + 2, s->width - 2, STRONG, text, true);
}
static void conversation(Screen *s, const BsApp *a, int start, int end) {
    if (!a->log.messages) {
        put(s, start + 1, s->left, s->width, MUTED, "Received and sent messages will appear here.");
        put(s, start + 2, s->left, s->width, MUTED,
            "Write below. Open the menu for files, macros and settings.");
        return;
    }
    size_t count = a->log.messages < 16 ? a->log.messages : 16, visible = 0;
    int used = 0;
    for (size_t i = 0; i < count; ++i) {
        const char *message = a->log.conversation[(a->log.messages - i - 1) % 16];
        int lines = (int)((strlen(message) + (size_t)s->width - 1) / (size_t)s->width);
        if (lines > 3) {
            lines = 3;
        }
        if (used + lines + (visible ? 1 : 0) > end - start) {
            break;
        }
        used += lines + (visible ? 1 : 0);
        ++visible;
    }
    int row = start;
    for (size_t i = 0; i < visible; ++i) {
        const char *message = a->log.conversation[(a->log.messages - visible + i) % 16];
        int lines = (int)((strlen(message) + (size_t)s->width - 1) / (size_t)s->width);
        if (lines > 3) {
            lines = 3;
        }
        unsigned char style = strstr(message, "UNVERIFIED") ? NOTICE : NORMAL;
        wrapped(s, row, lines, s->left, s->width, style, message, false);
        row += lines + 1;
    }
}
static void main_screen(Screen *s, const BsApp *a) {
    if (s->rows < 12) {
        if (s->rows >= 8) {
            put(s, 2, s->left, s->width, MUTED, "%s", a->log.current);
        }
        const char *input = a->state == BS_FILE_SELECT ? a->file_path : a->compose;
        size_t room = (size_t)(s->width > 3 ? s->width - 3 : 0);
        put(s, s->rows >= 8 ? 3 : 2, s->left, s->width, STRONG, "> %s_",
            input + (strlen(input) > room ? strlen(input) - room : 0));
        return;
    }
    int compose_row = s->rows - 8, activity = compose_row - 2;
    section(s, 3, "CONVERSATION", "Verified messages / local sends");
    if (a->show_log && s->rows >= 22) {
        conversation(s, a, 4, 8);
        section(s, 8, "TECHNICAL LOG", a->log.scroll ? "HISTORY" : "LIVE");
        size_t room = activity > 9 ? (size_t)(activity - 9) : 0;
        size_t end = a->log.count > a->log.scroll ? a->log.count - a->log.scroll : 0;
        size_t begin = end > room ? end - room : 0;
        for (size_t i = begin; i < end; ++i) {
            put(s, 9 + (int)(i - begin), s->left, s->width, MUTED, "%s",
                a->log.rows[(a->log.head + BS_LOG_ROWS - a->log.count + i) % BS_LOG_ROWS]);
        }
    } else {
        conversation(s, a, 4, activity);
    }
    if (activity >= 4) {
        put(s, activity, s->left, s->width, bs_rx_active(&a->receiver) ? ACCENT : MUTED, "%s",
            a->show_log && *a->log.current ? a->log.current
            : bs_rx_active(&a->receiver)   ? "Receiving a protected packet..."
                                           : "Listening for incoming messages.");
        if (*a->file_status) {
            put(s, activity + 1, s->left, s->width, NORMAL, "%s", a->file_status);
        }
    }
    composer(s, a, compose_row, 2);
}
static void macros_screen(Screen *s, const BsApp *a) {
    const char *text = a->macro_editing ? a->macro_text : a->editing_macros.text[a->macro_slot];
    if (s->rows >= 22) {
        bool changed =
            memcmp(&a->macros, &a->editing_macros, sizeof a->macros) != 0 ||
            (a->macro_editing && strcmp(a->macro_text, a->editing_macros.text[a->macro_slot]));
        section(s, 3, "MACROS",
                changed ? "Unsaved changes / Ctrl+S saves" : "Saved message shortcuts");
        for (unsigned i = 0; i < BS_MACRO_COUNT; ++i) {
            const char *preview =
                a->macro_editing && a->macro_slot == i ? a->macro_text : a->editing_macros.text[i];
            if (i == a->macro_slot) {
                put(s, 5 + (int)i, s->left, s->width, SELECTED, "%*s", s->width, "");
            }
            put(s, 5 + (int)i, s->left, s->width, i == a->macro_slot ? SELECTED : NORMAL,
                "%c Ctrl+X %u   %s", i == a->macro_slot ? '>' : ' ', i + 1,
                *preview ? preview : "(Not assigned)");
        }
        char detail[48];
        snprintf(detail, sizeof detail, "Ctrl+X then %u / %zu/512", a->macro_slot + 1,
                 strlen(text));
        section(s, s->rows - 8, a->macro_editing ? "EDIT MESSAGE" : "MESSAGE PREVIEW", detail);
        char preview[BS_MAX_TEXT + 2];
        snprintf(preview, sizeof preview, "%s%s", text, a->macro_editing ? "_" : "");
        wrapped(s, s->rows - 7, 2, s->left, s->width, STRONG, *preview ? preview : "(Not assigned)",
                true);
    } else {
        int row = s->rows >= 12 ? 3 : 2;
        put(s, row, s->left, s->width, ACCENT, "MACRO %u/9 / Ctrl+X then %u%s", a->macro_slot + 1,
            a->macro_slot + 1, a->macro_editing ? " / EDIT" : "");
        char preview[BS_MAX_TEXT + 2];
        snprintf(preview, sizeof preview, "%s%s", text, a->macro_editing ? "_" : "");
        wrapped(s, row + 1, s->rows >= 12 ? 3 : 1, s->left, s->width, STRONG,
                *preview ? preview : "(Not assigned)", true);
    }
}
static void command_modal(Screen *s, const BsApp *a) {
    struct MenuRow {
        const BsCommand *command;
        const char *group;
        size_t index;
    } rows[64];
    size_t count = 0, commands = 0, selected_row = 0;
    const char *group = "";
    for (size_t i = 0; i < bs_command_count; ++i) {
        const BsCommand *command = &bs_commands[i];
        if (!bs_command_available(a, command->key)) {
            continue;
        }
        if (strcmp(group, command->group)) {
            group = command->group;
            rows[count++] = (struct MenuRow){NULL, group, 0};
        }
        if (commands == a->command_scroll) {
            selected_row = count;
        }
        rows[count++] = (struct MenuRow){command, group, commands++};
    }
    int width = s->width < 64 ? s->width : 64;
    int height = (int)count + 3;
    if (height > s->rows - 4) {
        height = s->rows - 4;
    }
    if (width < 8 || height < 5) {
        put(s, 0, 0, s->columns, NOTICE, "q quit / Esc close");
        return;
    }
    int left = (s->columns - width) / 2, top = (s->rows - height) / 2;
    size_t room = (size_t)(height - 3), start = selected_row >= room ? selected_row - room + 1 : 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            char c = y == 0 || y == height - 1 ? '-' : ' ';
            if (x == 0 || x == width - 1) {
                c = y == 0 || y == height - 1 ? '+' : '|';
            }
            put(s, top + y, left + x, 1, MODAL, "%c", c);
        }
    }
    put(s, top, left + 2, width - 4, MODAL_KEY, " MENU ");
    for (size_t i = 0; i < room && start + i < count; ++i) {
        const struct MenuRow *row = &rows[start + i];
        if (!row->command) {
            put(s, top + 1 + (int)i, left + 3, width - 6, ACCENT, "%s", row->group);
        } else {
            unsigned char style = row->index == a->command_scroll ? SELECTED : MODAL;
            put(s, top + 1 + (int)i, left + 2, width - 4, style, "%*s", width - 4, "");
            const char *description = row->command->description;
            if (row->command->key >= '1' && row->command->key <= '9') {
                description = a->macros.text[row->command->key - '1'];
            }
            put(s, top + 1 + (int)i, left + 2, width - 4, style, "%c [%c] %s",
                row->index == a->command_scroll ? '>' : ' ', row->command->key, description);
        }
    }
    put(s, top + height - 2, left + 2, width - 4, MUTED,
        "Up/Down choose / Enter select / Esc close");
}
static Screen layout(const BsApp *a, unsigned columns, unsigned rows) {
    Screen s;
    memset(&s, 0, sizeof s);
    memset(s.text, ' ', sizeof s.text);
    s.rows = rows > SCREEN_ROWS ? SCREEN_ROWS : (int)rows;
    s.columns = columns > SCREEN_COLUMNS ? SCREEN_COLUMNS - 1 : columns > 1 ? (int)columns - 1 : 1;
    s.left = s.columns > 114 ? (s.columns - 110) / 2 : s.columns > 30 ? 2 : 0;
    s.width = s.columns - 2 * s.left;
    header(&s, a);
    if (a->state == BS_SETTINGS) {
        settings_screen(&s, a);
    } else if (a->state == BS_MACROS) {
        macros_screen(&s, a);
    } else {
        main_screen(&s, a);
    }
    if (s.rows >= 12) {
        if (s.rows >= 20) {
            section(&s, s.rows - 5, "STATUS", "");
        }
        wrapped(&s, s.rows - 4, 2, s.left, s.width,
                a->state >= BS_WAIT && a->state <= BS_RELEASE ? NOTICE : NORMAL, a->instruction,
                false);
        if (a->state != BS_SETTINGS && a->state != BS_MACROS) {
            put(&s, s.rows - 2, s.left, s.width, MUTED, "Input %.3f / clipping %u / tone %.2f%s",
                a->receiver.level, a->receiver.clipped, a->receiver.confidence,
                a->show_log ? " / Technical log open" : "");
        }
    } else {
        wrapped(&s, s.rows - 3, 2, s.left, s.width,
                a->state >= BS_WAIT && a->state <= BS_RELEASE ? NOTICE : NORMAL, a->instruction,
                false);
    }
    const char *keys =
        a->command_menu ? "Up/Down choose / Enter select / Esc close"
        : a->state == BS_MACROS
            ? (a->macro_editing ? "Enter keep / Ctrl+U clear / Ctrl+S save / Esc undo"
                                : "Up/Down choose / Enter edit / Ctrl+S save / Esc discard")
        : a->state == BS_SETTINGS    ? "Tab next / Shift+Tab back / Enter save / Esc cancel"
        : a->state == BS_FILE_SELECT ? "Enter prepare file / Esc cancel"
        : a->state == BS_WAIT        ? "Space start / Esc cancel"
        : a->state == BS_PLAYING || a->state == BS_DRAINING || a->state == BS_COUNTDOWN
            ? "Esc interrupt"
        : a->state == BS_RELEASE ? "Release the radio PTT"
                                 : "Enter prepare / Up/Down history";
    put(&s, s.rows - 1, s.left, s.width, ACCENT, "C-x menu / %s", keys);
    if (a->command_menu) {
        for (int row = 2; row < s.rows - 5; ++row) {
            memset(s.style[row], MUTED, sizeof s.style[row]);
        }
        command_modal(&s, a);
    }
    return s;
}
static size_t format_screen(const Screen *s, const Screen *previous, bool color, char *output,
                            size_t capacity) {
    if (capacity) {
        output[0] = 0;
    }
    bool same_size = previous && previous->rows == s->rows && previous->columns == s->columns;
    const char *styles[] = {"\033[0m",      "\033[0;90m", "\033[0;36m",   "\033[0;1m",
                            "\033[0;1;33m", "\033[0m",    "\033[0;1;36m", "\033[0;1;30;46m"};
    size_t used = 0;
    for (int y = 0; y < s->rows; ++y) {
        if (same_size && !memcmp(s->text[y], previous->text[y], (size_t)s->columns) &&
            (!color || !memcmp(s->style[y], previous->style[y], (size_t)s->columns))) {
            continue;
        }
        char position[32];
        int size = snprintf(position, sizeof position, "\033[%d;1H\033[0m", y + 1);
        if (used + (size_t)size >= capacity) {
            return 0;
        }
        memcpy(output + used, position, (size_t)size);
        used += (size_t)size;
        unsigned char current = NORMAL;
        for (int x = 0; x < s->columns; ++x) {
            unsigned char style = s->style[y][x];
            const char *escape = color && style != current ? styles[style] : "";
            size_t length = strlen(escape);
            if (used + length + 1 >= capacity) {
                return 0;
            }
            memcpy(output + used, escape, length);
            used += length;
            output[used++] = s->text[y][x];
            current = style;
        }
        /* Overwrite before clearing the unused tail; never blank a visible row first. */
        const char tail[] = "\033[0m\033[K";
        if (used + sizeof tail > capacity) {
            return 0;
        }
        memcpy(output + used, tail, sizeof tail);
        used += sizeof tail - 1;
    }
    return used;
}
size_t bs_tui_format(const BsApp *a, unsigned columns, unsigned rows, bool color, char *output,
                     size_t capacity) {
    Screen s = layout(a, columns, rows);
    return format_screen(&s, NULL, color, output, capacity);
}
void bs_tui_render(const BsApp *a) {
    char output[131072];
    unsigned columns, rows;
    bs_terminal_size(&columns, &rows);
    const char *term = getenv("TERM");
    bool color = !getenv("NO_COLOR") && (!term || strcmp(term, "dumb"));
    static Screen previous;
    static bool valid, previous_color;
    static unsigned previous_columns, previous_rows;
    Screen current = layout(a, columns, rows);
    /* Meter and bit updates must not repaint idle text. A resize invalidates the
       cache because terminals can reflow rows even when our layout cap is unchanged. */
    const Screen *baseline =
        valid && previous_color == color && previous_columns == columns && previous_rows == rows
            ? &previous
            : NULL;
    size_t count = format_screen(&current, baseline, color, output, sizeof output);
    if (!count) {
        return;
    }
    valid = fwrite(output, 1, count, stdout) == count && fflush(stdout) == 0;
    if (valid) {
        previous = current;
        previous_color = color;
        previous_columns = columns;
        previous_rows = rows;
    }
}
