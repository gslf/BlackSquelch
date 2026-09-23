#include "app.h"
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SCREEN_ROWS 120
#define SCREEN_COLUMNS 240
enum { NORMAL, MUTED, ACCENT, STRONG, NOTICE, MODAL, MODAL_KEY };
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
                           "SETTINGS"};
    return names[state];
}
static void header(Screen *s, const BsApp *a) {
    const char *logo[] = {"+++++++++++++++++", "\\--- BlackSquelch", ":#/ GSLF",
                          "+++++++++++++++++"};
    for (int i = 0; i < 4; ++i) {
        put(s, i, s->left, s->width, i == 1 ? STRONG : ACCENT, "%s", logo[i]);
    }
    if (s->width >= 54) {
        int x = s->left + 24, width = s->width - 24;
        put(s, 0, x, width, STRONG, "%s",
            a->state == BS_COMPOSE && !a->audio ? "AUDIO OFFLINE" : state_name(a->state));
        put(s, 1, x, width, NORMAL, "%s   /   AUDIO %s",
            *a->config.call ? a->config.call : "------", a->audio ? "READY" : "OFFLINE");
        if (a->receiver.bit_rate) {
            put(s, 2, x, width, MUTED, "TX %u bit/s   /   RX %u bit/s", a->config.bit_rate,
                a->receiver.bit_rate);
        } else {
            put(s, 2, x, width, MUTED, "TX %u bit/s   /   RX AUTO (10/25/50)", a->config.bit_rate);
        }
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
        snprintf(text, capacity, "%u bit/s", a->editing.bit_rate);
        break;
    }
}
static void settings_screen(Screen *s, const BsApp *a) {
    const char *labels[] = {"Callsign", "Audio input", "Audio output", "Output level", "TX rate"};
    const char *hints[] = {"Exactly 6 ASCII letters/digits", "Up / Down to select microphone",
                           "Up / Down to select speaker", "Up / Down to adjust amplitude",
                           "Up / Down: 10, 25, 50 bit/s"};
    if (s->rows >= 12) {
        section(s, 5, "SETTINGS", "Tab selects a field");
    }
    if (s->rows >= 22) {
        for (unsigned i = 0; i < 5; ++i) {
            char value[256];
            setting_value(a, i, value, sizeof value);
            unsigned char style = a->setting == i ? STRONG : NORMAL;
            put(s, 7 + (int)i * 2, s->left, s->width, style, "%c %-13s %s",
                a->setting == i ? '>' : ' ', labels[i], value);
            if (a->setting == i) {
                put(s, 8 + (int)i * 2, s->left + 16, s->width - 16, MUTED, "%s", hints[i]);
            }
        }
        put(s, 18, s->left, s->width, MUTED, "RX detects the sender's rate automatically.");
    } else {
        char value[256];
        setting_value(a, a->setting, value, sizeof value);
        put(s, s->rows >= 12 ? 7 : 4, s->left, s->width, STRONG, "%u/5 %s: %s", a->setting + 1,
            labels[a->setting], value);
        if (s->rows >= 12) {
            put(s, 8, s->left, s->width, MUTED, "%s", hints[a->setting]);
        }
    }
}
static void composer(Screen *s, const BsApp *a, int row, int lines) {
    BsFrame estimate;
    bool valid = bs_frame_build(&estimate, a->config.call, a->compose);
    char details[120];
    if (valid) {
        snprintf(details, sizeof details, "%zu/512  |  %u bit/s  |  %.2f s + 2 s PTT",
                 strlen(a->compose), a->config.bit_rate,
                 bs_frame_duration(&estimate, a->config.bit_rate));
    } else {
        snprintf(details, sizeof details, "%zu/512  |  %u bit/s", strlen(a->compose),
                 a->config.bit_rate);
    }
    section(s, row, "COMPOSE", details);
    char text[BS_MAX_TEXT + 4];
    snprintf(text, sizeof text, "%s_", a->compose);
    put(s, row + 1, s->left, 2, ACCENT, "> ");
    wrapped(s, row + 1, lines, s->left + 2, s->width - 2, STRONG, text, true);
}
static void main_screen(Screen *s, const BsApp *a) {
    if (s->rows < 20) {
        if (s->rows >= 12) {
            if (s->rows >= 15) {
                section(s, 5, "RECEIVE", "LIVE");
                put(s, 6, s->left, s->width, NORMAL, "%s", a->log.current);
            } else {
                put(s, 5, s->left, s->width, NORMAL, "%s", a->log.current);
            }
            composer(s, a, s->rows - 6, 1);
        } else {
            put(s, 4, s->left, s->width, MUTED, "%s", a->log.current);
            put(s, 5, s->left, s->width, STRONG, "> %s_",
                a->compose + (strlen(a->compose) > (size_t)(s->width > 4 ? s->width - 4 : 0)
                                  ? strlen(a->compose) - (size_t)(s->width > 4 ? s->width - 4 : 0)
                                  : 0));
        }
        return;
    }
    section(s, 5, "MESSAGES", "RX results / local TX status");
    size_t messages = a->log.messages < 2 ? a->log.messages : 2;
    if (!messages) {
        put(s, 6, s->left, s->width, MUTED, "No messages yet.");
    } else {
        for (size_t i = 0; i < messages; ++i) {
            put(s, 6 + (int)i, s->left, s->width, NORMAL, "%s",
                a->log.conversation[(a->log.messages - messages + i) % 16]);
        }
    }
    section(s, 8, "RECEIVE LOG", a->log.scroll ? "HISTORY" : "LIVE");
    int current = s->rows - 11;
    size_t room = current > 9 ? (size_t)(current - 9) : 0;
    size_t end = a->log.count > a->log.scroll ? a->log.count - a->log.scroll : 0;
    size_t begin = end > room ? end - room : 0;
    for (size_t i = begin; i < end; ++i) {
        put(s, 9 + (int)(i - begin), s->left, s->width, MUTED, "%s",
            a->log.rows[(a->log.head + BS_LOG_ROWS - a->log.count + i) % BS_LOG_ROWS]);
    }
    wrapped(s, current, 2, s->left, s->width, ACCENT,
            *a->log.current ? a->log.current : "Waiting for a preamble...", false);
    put(s, current + 2, s->left, s->width, MUTED, "Unverified %s: %s",
        *a->log.call ? a->log.call : "------", a->log.text);
    composer(s, a, s->rows - 7, 2);
}
static void command_modal(Screen *s, const BsApp *a) {
    const BsCommand *visible[32];
    size_t count = 0;
    for (size_t i = 0; i < bs_command_count; ++i) {
        if (bs_command_available(a, bs_commands[i].key)) {
            visible[count++] = &bs_commands[i];
        }
    }
    int width = s->width < 52 ? s->width : 52;
    int height = (int)count + 4;
    int maximum = s->rows >= 14 ? s->rows - 9 : s->rows - 2;
    if (height > maximum) {
        height = maximum;
    }
    if (width < 4 || height < 4) {
        put(s, 0, 0, s->columns, NOTICE, "q quit; Esc close");
        return;
    }
    int left = (s->columns - width) / 2;
    int top = s->rows >= 14 ? 5 + (maximum - height) / 2 : 0;
    size_t room = (size_t)(height - 4);
    if (!room) {
        room = 1;
    }
    size_t start = a->command_scroll;
    if (start >= count) {
        start = count ? count - 1 : 0;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            char c = y == 0 || y == height - 1 ? '-' : ' ';
            if (x == 0 || x == width - 1) {
                c = y == 0 || y == height - 1 ? '+' : '|';
            }
            put(s, top + y, left + x, 1, MODAL, "%c", c);
        }
    }
    put(s, top, left + 2, width - 4, MODAL_KEY, " COMMANDS ");
    for (size_t i = 0; i < room && start + i < count; ++i) {
        const BsCommand *command = visible[start + i];
        put(s, top + 1 + (int)i, left + 3, 1, MODAL_KEY, "%c", command->key);
        put(s, top + 1 + (int)i, left + 7, width - 9, MODAL, "%s", command->description);
    }
    put(s, top + height - 2, left + 2, width - 4, MODAL, "%s",
        count > room ? "Up/Down scroll | Esc / C-g close" : "Press a letter | Esc / C-g close");
}
static Screen layout(const BsApp *a, unsigned columns, unsigned rows) {
    Screen s;
    memset(&s, 0, sizeof s);
    memset(s.text, ' ', sizeof s.text);
    s.rows = rows > SCREEN_ROWS ? SCREEN_ROWS : (int)rows;
    /* Keep the final terminal column unused to avoid automatic wrapping. */
    s.columns = columns > SCREEN_COLUMNS ? SCREEN_COLUMNS - 1 : columns > 1 ? (int)columns - 1 : 1;
    s.left = s.columns > 114 ? (s.columns - 110) / 2 : s.columns > 30 ? 2 : 0;
    s.width = s.columns - 2 * s.left;
    header(&s, a);
    if (a->state == BS_SETTINGS) {
        settings_screen(&s, a);
    } else {
        main_screen(&s, a);
    }
    if (s.rows >= 12) {
        wrapped(&s, s.rows - 4, 2, s.left, s.width, NOTICE, a->instruction, false);
        if (a->state != BS_SETTINGS) {
            put(&s, s.rows - 2, s.left, s.width, MUTED, "MIC %.3f  |  CLIP %u  |  TONE %.2f",
                a->receiver.level, a->receiver.clipped, a->receiver.confidence);
        }
    } else {
        put(&s, s.rows - 2, s.left, s.width, NOTICE, "%s", a->instruction);
    }
    const char *keys =
        a->command_menu           ? "Esc / C-g close"
        : a->state == BS_SETTINGS ? "Tab field | Enter save | Esc cancel"
        : a->state == BS_WAIT     ? "Space start | Esc cancel"
        : a->state == BS_PLAYING || a->state == BS_DRAINING || a->state == BS_COUNTDOWN
            ? "Esc interrupt"
        : a->state == BS_RELEASE ? ""
                                 : "Enter prepare | Esc cancel";
    put(&s, s.rows - 1, s.left, s.width, ACCENT, "C-x commands%s%s", *keys ? " | " : "", keys);
    if (a->command_menu) {
        for (int row = 4; row < s.rows - 4; ++row) {
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
    const char *styles[] = {"\033[0m",      "\033[0;90m",    "\033[0;36m",     "\033[0;1m",
                            "\033[0;1;33m", "\033[0;37;44m", "\033[0;1;97;44m"};
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
