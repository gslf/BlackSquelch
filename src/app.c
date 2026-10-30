#include "app.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static bool transmitting(const BsApp *a) {
    return a->state == BS_PLAYING || a->state == BS_DRAINING;
}
static unsigned next_tones(unsigned tones) {
    return bs_tones_effective(tones) == 8 ? 16 : 8;
}
static bool planned_frame(const BsApp *a, size_t index, BsFrame *frame) {
    return a->tx_report
               ? bs_frame_build_repair(frame, a->frozen_call, &a->reports[index])
               : bs_file_frame(&a->outgoing_file, a->frozen_call, a->plan_indices[index], frame);
}
static bool freeze_plan(BsApp *a) {
    a->frozen_tones = bs_tones_effective(a->config.tones);
    a->total_samples = a->total_bits = 0;
    a->plan_position = 0;
    BsFrame frame;
    for (size_t i = 0; i < a->plan_count; ++i) {
        if (!planned_frame(a, i, &frame)) {
            return false;
        }
        uint64_t samples = bs_frame_samples(&frame, a->frozen_tones);
        if (!samples) {
            return false;
        }
        a->total_samples += samples;
        a->total_bits += frame.count;
    }
    return a->plan_count && planned_frame(a, 0, &a->frame);
}
static void save_received_file(BsApp *a) {
    char path[1200];
    if (bs_file_save(&a->incoming_file, "received-files", path, sizeof path)) {
        a->incoming_file.saved = true;
        snprintf(a->file_status, sizeof a->file_status, "FILE RECEIVED: %.380s (%u bytes)", path,
                 (unsigned)a->incoming_file.file.info.size);
        bs_log_message(&a->log, "FILE RECEIVED", a->incoming_file.call, a->file_status);
    } else {
        strcpy(a->file_status, "File verified but saving failed. Check received-files directory; "
                               "use Retry saving received file from the command menu.");
    }
    bs_log_add(&a->log, a->file_status);
}
static void receive_event(void *context, const BsEvent *event) {
    BsApp *a = context;
    bs_log_event(&a->log, event);
    if (a->have_outgoing_file &&
        bs_file_apply_report(&a->outgoing_file, a->last_file_call, event, a->requested)) {
        a->repair_pending = !event->repair.complete;
        if (a->repair_pending) {
            a->last_is_file = true;
        }
        snprintf(a->file_status, sizeof a->file_status, "%s from %s. %s",
                 event->repair.complete ? "Remote file receipt confirmed" : "File repair request",
                 event->call,
                 event->repair.complete ? "" : "Repeat prepares only requested blocks.");
        bs_log_add(&a->log, a->file_status);
        bs_log_message(&a->log, event->repair.complete ? "REMOTE RECEIPT" : "REPAIR REQUEST",
                       event->call, a->file_status);
    }
    BsFileResult result = bs_file_accept(&a->incoming_file, event);
    if (result == BS_FILE_COMPLETE) {
        save_received_file(a);
    } else if (result == BS_FILE_PROGRESS) {
        snprintf(a->file_status, sizeof a->file_status, "Receiving %s from %s: %zu/%zu blocks",
                 event->file.name, event->call, a->incoming_file.received,
                 bs_file_chunk_count(event->file.size));
        bs_log_add(&a->log, a->file_status);
    } else if (result == BS_FILE_BUSY || result == BS_FILE_ERROR) {
        snprintf(
            a->file_status, sizeof a->file_status, "%s",
            result == BS_FILE_BUSY
                ? "Another file is pending. Save it or abort reception to accept a new file."
                : "File metadata, duplicate data or whole-file checksum mismatch; file not saved.");
        bs_log_add(&a->log, a->file_status);
    }
}
static void settings(BsApp *a) {
    a->editing = a->config;
    a->editing.tones = bs_tones_effective(a->config.tones);
    a->state = BS_SETTINGS;
    a->setting = 0;
    a->input_count = bs_audio_devices(true, a->inputs, BS_DEVICE_LIMIT);
    a->output_count = bs_audio_devices(false, a->outputs, BS_DEVICE_LIMIT);
    a->input_index = a->output_index = 0;
    for (size_t i = 0; i < a->input_count; ++i) {
        if (!strcmp(a->config.input, a->inputs[i].id)) {
            a->input_index = i;
        }
    }
    for (size_t i = 0; i < a->output_count; ++i) {
        if (!strcmp(a->config.output, a->outputs[i].id)) {
            a->output_index = i;
        }
    }
}
static void history_add(BsApp *a, const char *text) {
    size_t latest = (a->history_head + BS_HISTORY_COUNT - 1) % BS_HISTORY_COUNT;
    if (!a->history_count || strcmp(a->history[latest], text)) {
        strcpy(a->history[a->history_head], text);
        a->history_head = (a->history_head + 1) % BS_HISTORY_COUNT;
        if (a->history_count < BS_HISTORY_COUNT) {
            ++a->history_count;
        }
    }
    a->history_offset = 0;
    a->history_draft[0] = 0;
}
static void history_key(BsApp *a, bool older) {
    if (older) {
        if (a->history_offset >= a->history_count) {
            return;
        }
        if (!a->history_offset) {
            strcpy(a->history_draft, a->compose);
        }
        ++a->history_offset;
    } else {
        if (!a->history_offset) {
            return;
        }
        --a->history_offset;
    }
    if (a->history_offset) {
        size_t index = (a->history_head + BS_HISTORY_COUNT - a->history_offset) % BS_HISTORY_COUNT;
        strcpy(a->compose, a->history[index]);
    } else {
        strcpy(a->compose, a->history_draft);
    }
}
static void end_transmission(BsApp *a, bool success, const char *reason) {
    snprintf(a->instruction, sizeof a->instruction, "%s - RELEASE THE RADIO PTT.%s%.450s",
             success ? "Transmission completed" : "Transmission interrupted", *reason ? " " : "",
             reason);
    bs_log_add(&a->log, a->instruction);
    bs_log_message(&a->log, success ? "TX PLAYED" : "TX INTERRUPTED", a->frozen_call, a->frozen);
    if (success && a->tx_file) {
        for (size_t i = 0; i < a->plan_count; ++i) {
            a->requested[a->plan_indices[i]] = 0;
        }
        a->repair_pending = false;
        for (size_t i = 0; i < BS_FILE_BLOCKS; ++i) {
            a->repair_pending |= a->requested[i] != 0;
        }
    }
    if (success && !a->tx_file && !a->tx_report) {
        history_add(a, a->frozen);
        a->compose[0] = 0;
    }
    a->state = BS_RELEASE;
    a->deadline = bs_time() + 3;
    /* Discard acoustic loopback and queued capture before reacquiring an unrelated frame. */
    if (a->audio && !bs_audio_reset(a->audio)) {
        bs_log_add(&a->log, bs_audio_error(a->audio));
        bs_audio_close(a->audio);
        a->audio = NULL;
    }
    bs_rx_abort(&a->receiver, "Local transmission ended");
    a->last_capture = bs_time();
}
static void audio_error(BsApp *a, const char *reason) {
    char copy[512];
    snprintf(copy, sizeof copy, "%s", reason);
    if (transmitting(a)) {
        end_transmission(a, false, copy);
    } else {
        bs_rx_abort(&a->receiver, copy);
        snprintf(a->instruction, sizeof a->instruction,
                 "Audio offline. RELEASE PTT IF HELD: %.390s. Reconnect in settings.", copy);
    }
    if (a->audio) {
        bs_audio_close(a->audio);
    }
    a->audio = NULL;
    if (a->state != BS_RELEASE && a->state != BS_SETTINGS && a->state != BS_MACROS) {
        a->state = BS_COMPOSE;
    }
}
static void prepare(BsApp *a) {
    if (!a->audio) {
        strcpy(a->instruction, "Open audio devices in settings before transmitting.");
        return;
    }
    if (bs_rx_active(&a->receiver)) {
        strcpy(a->instruction, "Receiving a frame. Wait or abort reception from the command menu.");
        return;
    }
    size_t bad;
    if (!bs_normalize(a->compose, false, &bad)) {
        if (bad < strlen(a->compose)) {
            snprintf(
                a->instruction, sizeof a->instruction,
                "Unsupported byte at position %zu: 0x%02X. Edit the text; nothing was removed.",
                bad + 1, (unsigned char)a->compose[bad]);
        } else {
            strcpy(a->instruction, "Text must contain 1-512 supported characters.");
        }
        return;
    }
    if (!bs_tones_valid(bs_tones_effective(a->config.tones)) ||
        !bs_frame_build(&a->frame, a->config.call, a->compose)) {
        strcpy(a->instruction, "Callsign must contain exactly six letters/digits; TX tones must be "
                               "8 or 16. Check settings.");
        return;
    }
    strcpy(a->frozen, a->compose);
    strcpy(a->last, a->compose);
    a->tx_file = a->last_is_file = a->tx_report = false;
    a->plan_count = 1;
    strcpy(a->frozen_call, a->config.call);
    a->total_bits = a->frame.count;
    a->state = BS_WAIT;
    a->frozen_tones = bs_tones_effective(a->config.tones);
    a->total_samples = bs_frame_samples(&a->frame, a->frozen_tones);
    snprintf(a->instruction, sizeof a->instruction,
             "HOLD THE RADIO PTT, THEN PRESS SPACE. Prepared message: %.2f seconds, %u tones. Esc "
             "cancels.",
             (double)a->total_samples / BS_RATE, a->frozen_tones);
    bs_log_add(&a->log, a->instruction);
}
static void prepare_file(BsApp *a, bool load) {
    if (!a->audio || bs_rx_active(&a->receiver)) {
        strcpy(a->instruction, "Audio must be ready and reception idle before preparing a file.");
        return;
    }
    if (!bs_tones_valid(bs_tones_effective(a->config.tones))) {
        strcpy(a->instruction, "Select 8 or 16 TX tones in settings.");
        return;
    }
    if (!bs_normalize(a->config.call, true, NULL)) {
        strcpy(a->instruction, "Set a valid six-character callsign before preparing a file.");
        return;
    }
    if (load &&
        !bs_file_load(&a->outgoing_file, a->file_path, a->instruction, sizeof a->instruction)) {
        return;
    }
    if (load) {
        strcpy(a->last_file_call, a->config.call);
        memset(a->requested, 0, sizeof a->requested);
        a->repair_pending = false;
    }
    strcpy(a->frozen_call, a->last_file_call);
    a->tx_file = a->last_is_file = true;
    a->have_outgoing_file = true;
    a->tx_report = false;
    a->plan_count = 0;
    for (size_t i = 0; i < bs_file_chunk_count(a->outgoing_file.info.size); ++i) {
        if (!a->repair_pending || a->requested[i]) {
            a->plan_indices[a->plan_count++] = (uint16_t)i;
        }
    }
    if (!freeze_plan(a)) {
        strcpy(a->instruction, "Invalid file metadata, callsign or TX profile. Check settings.");
        return;
    }
    a->tx_file_index = a->plan_indices[0];
    a->state = BS_WAIT;
    snprintf(a->frozen, sizeof a->frozen, "%s %s (%u bytes)",
             a->repair_pending ? "REPAIR FILE" : "FILE", a->outgoing_file.info.name,
             (unsigned)a->outgoing_file.info.size);
    snprintf(a->instruction, sizeof a->instruction,
             "HOLD THE RADIO PTT, THEN PRESS SPACE. %zu blocks of up to 64 bytes, %.1f seconds, "
             "%u tones. Esc cancels.",
             a->plan_count, (double)a->total_samples / BS_RATE, a->frozen_tones);
    bs_log_add(&a->log, a->instruction);
}
static void prepare_report(BsApp *a) {
    if (!a->audio || bs_rx_active(&a->receiver) || !a->incoming_file.active) {
        return;
    }
    a->tx_file = false;
    a->tx_report = true;
    strcpy(a->frozen_call, a->config.call);
    a->plan_count = 0;
    size_t pages = (bs_file_chunk_count(a->incoming_file.file.info.size) + BS_REPAIR_WINDOW - 1) /
                   BS_REPAIR_WINDOW;
    for (size_t i = 0; i < pages; ++i) {
        if (bs_file_report(&a->incoming_file, i, &a->reports[a->plan_count])) {
            ++a->plan_count;
        }
    }
    if (!freeze_plan(a)) {
        strcpy(a->instruction, "Could not prepare the file reception report.");
        return;
    }
    snprintf(a->frozen, sizeof a->frozen, "%s for %s",
             a->incoming_file.complete ? "FILE RECEIPT" : "MISSING FILE BLOCKS",
             a->incoming_file.file.info.name);
    a->state = BS_WAIT;
    snprintf(a->instruction, sizeof a->instruction,
             "HOLD THE RADIO PTT, THEN PRESS SPACE. File reception report: %.1f seconds, %u tones.",
             (double)a->total_samples / BS_RATE, a->frozen_tones);
    bs_log_add(&a->log, a->instruction);
}

static void edit(char *text, size_t max, int key) {
    size_t n = strlen(text);
    if ((key == 8 || key == 127 || key == BS_KEY_DELETE) && n) {
        text[n - 1] = 0;
    } else if (key >= 32 && key <= 255 && n < max) {
        if (key >= 'a' && key <= 'z') {
            key -= 'a' - 'A';
        }
        text[n] = (char)key;
        text[n + 1] = 0;
    }
}
static void open_macros(BsApp *a, bool from_composition) {
    a->macro_return_state = a->state;
    a->editing_macros = a->macros;
    a->macro_slot = 0;
    a->macro_editing = from_composition;
    if (from_composition) {
        for (unsigned i = 0; i < BS_MACRO_COUNT; ++i) {
            if (!*a->macros.text[i]) {
                a->macro_slot = i;
                break;
            }
        }
        strcpy(a->macro_text, a->compose);
    }
    a->state = BS_MACROS;
    strcpy(a->instruction, "Choose a shortcut with Up/Down; Enter edits its message. Ctrl+S saves "
                           "all macros. Esc returns without saving.");
}
static bool accept_macro_text(BsApp *a) {
    size_t bad;
    if (*a->macro_text && !bs_normalize(a->macro_text, false, &bad)) {
        snprintf(a->instruction, sizeof a->instruction,
                 "Unsupported macro byte at position %zu. Use the message alphabet; empty disables "
                 "the shortcut.",
                 bad + 1);
        return false;
    }
    strcpy(a->editing_macros.text[a->macro_slot], a->macro_text);
    a->macro_editing = false;
    return true;
}
static void macros_key(BsApp *a, int key) {
    if (key == 19) { /* Ctrl+S is local to this screen, never a transmit command. */
        if (a->macro_editing && !accept_macro_text(a)) {
            return;
        }
        if (!*a->macros_path || !bs_macros_save(a->macros_path, &a->editing_macros)) {
            strcpy(a->instruction, "Could not save macros. Check the preferences directory, then "
                                   "retry Ctrl+S. Your edits are still here.");
            return;
        }
        a->macros = a->editing_macros;
        a->state = a->macro_return_state;
        strcpy(a->instruction,
               "Macros saved. Use the command prefix followed by 1-9 to insert a message.");
    } else if (key == 27 || key == 7) {
        if (a->macro_editing) {
            a->macro_editing = false;
            strcpy(a->instruction,
                   "This message edit was cancelled. Ctrl+S saves other changes; Esc returns.");
        } else {
            a->state = a->macro_return_state;
            strcpy(a->instruction, "Macro changes discarded.");
        }
    } else if (key == 13 || key == 10) {
        if (a->macro_editing) {
            if (accept_macro_text(a)) {
                strcpy(a->instruction,
                       "Message updated. Choose another shortcut or press Ctrl+S to save.");
            }
        } else {
            strcpy(a->macro_text, a->editing_macros.text[a->macro_slot]);
            a->macro_editing = true;
            strcpy(a->instruction,
                   "Edit this shortcut's message. Enter keeps the edit; Ctrl+U clears; "
                   "Ctrl+S saves all; Esc cancels this edit.");
        }
    } else if (a->macro_editing) {
        if (key == 21) {
            a->macro_text[0] = 0;
        } else {
            edit(a->macro_text, BS_MAX_TEXT, key);
        }
    } else if (key == BS_KEY_UP || key == BS_KEY_BACK_TAB) {
        a->macro_slot = (a->macro_slot + BS_MACRO_COUNT - 1) % BS_MACRO_COUNT;
    } else if (key == BS_KEY_DOWN || key == 9) {
        a->macro_slot = (a->macro_slot + 1) % BS_MACRO_COUNT;
    } else if (key >= '1' && key <= '9') {
        a->macro_slot = (unsigned)(key - '1');
    }
}
static void insert_macro(BsApp *a, unsigned slot) {
    const char *text = a->macros.text[slot];
    size_t length = strlen(a->compose), extra = strlen(text);
    bool space = length && extra && a->compose[length - 1] != ' ' && *text != ' ';
    if (length + extra + space > BS_MAX_TEXT) {
        strcpy(a->instruction,
               "This macro would exceed 512 characters. Clear or shorten the message first.");
        return;
    }
    if (space) {
        a->compose[length++] = ' ';
    }
    strcpy(a->compose + length, text);
    snprintf(a->instruction, sizeof a->instruction,
             "Macro %u inserted. Edit the message or press Enter to prepare.", slot + 1);
}
static void settings_key(BsApp *a, int key) {
    if (key == 27 || key == 7) {
        a->state = BS_COMPOSE;
        strcpy(a->instruction, "Settings cancelled.");
        return;
    }
    if (key == 9 || key == BS_KEY_BACK_TAB) {
        a->setting = (a->setting + (key == 9 ? 1 : 4)) % 5;
        return;
    }
    if (key == 13 || key == 10) {
        if (!bs_normalize(a->editing.call, true, NULL) || !a->input_count || !a->output_count ||
            !bs_tones_valid(bs_tones_effective(a->editing.tones))) {
            strcpy(a->instruction,
                   "Select available audio devices and a valid six-character callsign.");
            return;
        }
        strcpy(a->editing.input, a->inputs[a->input_index].id);
        strcpy(a->editing.output, a->outputs[a->output_index].id);
        if (a->audio) {
            bs_audio_close(a->audio);
        }
        a->audio = bs_audio_open(a->editing.input, a->editing.output, a->instruction,
                                 sizeof a->instruction);
        if (!a->audio) {
            return;
        }
        a->config = a->editing;
        bool saved = *a->config_path && bs_config_save(a->config_path, &a->config);
        a->state = BS_COMPOSE;
        a->last_capture = bs_time();
        bs_rx_abort(&a->receiver, "Audio settings changed");
        strcpy(a->instruction, saved ? "Preferences saved. Listening; compose a short message."
                                     : "Audio ready, but preferences could not be saved.");
    } else if (a->setting == 0) {
        edit(a->editing.call, BS_CALL_CHARS, key);
    } else if (key == BS_KEY_UP || key == BS_KEY_DOWN) {
        size_t *index = a->setting == 1 ? &a->input_index : &a->output_index;
        size_t count = a->setting == 1 ? a->input_count : a->output_count;
        if (a->setting == 3) {
            a->editing.amplitude =
                fmax(0.05, fmin(1, a->editing.amplitude + (key == BS_KEY_UP ? 0.05 : -0.05)));
        } else if (a->setting == 4) {
            a->editing.tones = next_tones(a->editing.tones);
        } else if (count) {
            *index = (*index + (key == BS_KEY_UP ? count - 1 : 1)) % count;
        }
    }
}
static void cancel(BsApp *a) {
    if (transmitting(a)) {
        end_transmission(a, false, "Cancelled by operator.");
    } else if (a->state == BS_MACROS) {
        a->state = a->macro_return_state;
        a->macro_editing = false;
        strcpy(a->instruction, "Macro changes discarded.");
    } else if (a->state != BS_RELEASE) {
        a->state = BS_COMPOSE;
        strcpy(a->instruction, "Cancelled. Release PTT if held.");
    }
}
static void run_command(BsApp *a, char key) {
    if (key >= '1' && key <= '9') {
        insert_macro(a, (unsigned)(key - '1'));
        return;
    }
    switch (key) {
    case 'q':
        a->quit = true;
        break;
    case 'c':
        cancel(a);
        break;
    case 's':
        prepare(a);
        break;
    case 't':
        a->state = BS_FILE_SELECT;
        strcpy(a->instruction, "Type the path of a file (any format, max 1 MiB). Enter prepares; "
                               "case and spaces are preserved. Esc cancels.");
        break;
    case 'v':
        save_received_file(a);
        break;
    case 'm':
        prepare_report(a);
        break;
    case 'o':
        settings(a);
        break;
    case 'k':
        open_macros(a, false);
        break;
    case 'w':
        open_macros(a, true);
        break;
    case 'l':
        a->show_log = !a->show_log;
        break;
    case 'd': {
        BsConfig editing = a->editing;
        settings(a);
        a->editing = editing;
        strcpy(a->instruction, "Device list refreshed. Check selections before saving.");
        break;
    }
    case 'r':
        if (a->last_is_file) {
            prepare_file(a, false);
        } else {
            strcpy(a->compose, a->last);
            prepare(a);
        }
        break;
    case 'a':
        if (a->incoming_file.active && !a->incoming_file.saved) {
            memset(&a->incoming_file, 0, sizeof a->incoming_file);
            strcpy(a->file_status, "Pending file reception discarded.");
        }
        bs_rx_abort(&a->receiver, "Reception explicitly cancelled by operator");
        strcpy(a->instruction, "Local reception cancelled. Enter prepares the composed message.");
        break;
    case 'e': {
        /* Export is explicit; ordinary live logging stays memory-only. */
        char path[96];
        snprintf(path, sizeof path, "blacksquelch-log-%llu.txt",
                 (unsigned long long)(bs_time() * 1000));
        bool ok = bs_log_export(&a->log, path);
        snprintf(a->instruction, sizeof a->instruction, "%s: %s",
                 ok ? "Log exported" : "Log export failed", path);
        break;
    }
    case 'u':
        if (a->state == BS_FILE_SELECT) {
            a->file_path[0] = 0;
        } else {
            a->compose[0] = 0;
            a->history_offset = 0;
            a->history_draft[0] = 0;
        }
        break;
    case 'p':
        a->show_log = true;
        a->log.scroll += 5;
        if (a->log.scroll > a->log.count) {
            a->log.scroll = a->log.count;
        }
        break;
    case 'n':
        a->log.scroll = a->log.scroll > 5 ? a->log.scroll - 5 : 0;
        break;
    case 'f':
        a->log.scroll = 0;
        break;
    default:
        break;
    }
}
static void handle_key(BsApp *a, int key) {
    if (key == 3) {
        a->quit = true;
        return;
    }
    if (key == 24) {
        a->command_menu = !a->command_menu;
        a->command_scroll = 0;
        return;
    }
    if (a->command_menu) {
        size_t count = 0;
        for (size_t i = 0; i < bs_command_count; ++i) {
            count += bs_command_available(a, bs_commands[i].key);
        }
        if (a->command_scroll >= count) {
            a->command_scroll = count ? count - 1 : 0;
        }
        if (key == 27 || key == 7) {
            a->command_menu = false;
        } else if (key == BS_KEY_UP) {
            if (a->command_scroll) {
                --a->command_scroll;
            }
        } else if (key == BS_KEY_DOWN) {
            size_t available = 0;
            for (size_t i = 0; i < bs_command_count; ++i) {
                available += bs_command_available(a, bs_commands[i].key);
            }
            if (a->command_scroll + 1 < available) {
                ++a->command_scroll;
            }
        } else if (key == 13 || key == 10) {
            size_t index = 0;
            for (size_t i = 0; i < bs_command_count; ++i) {
                if (bs_command_available(a, bs_commands[i].key)) {
                    if (index++ == a->command_scroll) {
                        a->command_menu = false;
                        run_command(a, bs_commands[i].key);
                        break;
                    }
                }
            }
        } else if (key >= 32 && key <= 126 && bs_command_available(a, (char)key)) {
            a->command_menu = false;
            run_command(a, (char)key);
        }
        return;
    }
    if (a->state == BS_MACROS) {
        macros_key(a, key);
        return;
    }
    if (a->state == BS_SETTINGS) {
        settings_key(a, key);
        return;
    }
    if (key == 27 || key == 7) {
        cancel(a);
        return;
    }
    if (a->state == BS_FILE_SELECT) {
        if (key == 13 || key == 10) {
            prepare_file(a, true);
        } else {
            size_t n = strlen(a->file_path);
            if ((key == 8 || key == 127 || key == BS_KEY_DELETE) && n) {
                a->file_path[n - 1] = 0;
            } else if (key >= 32 && key <= 255 && n + 1 < sizeof a->file_path) {
                a->file_path[n] = (char)key;
                a->file_path[n + 1] = 0;
            }
        }
        return;
    }
    if (a->state == BS_RELEASE && !a->tx_file && !a->tx_report &&
        (key == BS_KEY_UP || key == BS_KEY_DOWN)) {
        history_key(a, key == BS_KEY_UP);
        return;
    }
    if (transmitting(a) || a->state == BS_COUNTDOWN || a->state == BS_RELEASE) {
        return;
    }
    if (a->state == BS_WAIT) {
        if (key == ' ') {
            if (bs_rx_active(&a->receiver)) {
                strcpy(a->instruction, "A frame is arriving. Release PTT and wait, or abort "
                                       "reception from the command menu.");
            } else {
                a->state = BS_COUNTDOWN;
                a->deadline = bs_time() + 2;
            }
        }
        return;
    }
    if (key == BS_KEY_UP || key == BS_KEY_DOWN) {
        history_key(a, key == BS_KEY_UP);
    } else if (key == BS_KEY_PAGE_UP || key == BS_KEY_PAGE_DOWN || key == BS_KEY_END) {
        a->show_log = true;
        run_command(a, key == BS_KEY_PAGE_UP ? 'p' : key == BS_KEY_PAGE_DOWN ? 'n' : 'f');
    } else if (key == 13 || key == 10) {
        prepare(a);
    } else {
        edit(a->compose, BS_MAX_TEXT, key);
    }
}
static void tick(BsApp *a, double now) {
    if (a->audio) {
        int16_t samples[BS_BLOCK];
        /* Bound each service pass so input floods cannot starve keyboard handling. */
        for (unsigned i = 0; i < 32; ++i) {
            int n = bs_audio_capture(a->audio, samples, BS_BLOCK);
            if (n < 0) {
                audio_error(a, bs_audio_error(a->audio));
                break;
            }
            if (!n) {
                break;
            }
            a->last_capture = now;
            if (!transmitting(a)) {
                bs_rx_read(&a->receiver, samples, (size_t)n);
            }
        }
        if (a->audio && now - a->last_capture > 1.5) {
            audio_error(a, "No capture samples for 1.5 seconds");
        }
    }
    if (a->state == BS_COUNTDOWN) {
        if (bs_rx_active(&a->receiver)) {
            a->state = BS_WAIT;
            strcpy(a->instruction,
                   "Reception started during countdown. Release PTT and wait; Space retries.");
        } else if (now >= a->deadline) {
            if (!a->audio || !bs_audio_begin(a->audio)) {
                audio_error(a, a->audio ? bs_audio_error(a->audio) : "Audio unavailable");
                return;
            }
            if (!bs_tx_init(&a->transmitter, &a->frame, a->config.amplitude, a->frozen_tones)) {
                audio_error(a, "Invalid transmit rate");
                return;
            }
            a->pending_count = a->pending_offset = 0;
            a->played = 0;
            a->last_progress = now;
            a->deadline = now + (double)a->total_samples / BS_RATE + 5;
            a->state = BS_PLAYING;
            bs_log_add(&a->log, "TX audio started; keep PTT pressed");
        } else {
            snprintf(a->instruction, sizeof a->instruction,
                     "KEEP PTT PRESSED. Audio starts in %.1f seconds.", a->deadline - now);
        }
    }
    if (a->state == BS_PLAYING) {
        for (unsigned i = 0; i < 32; ++i) {
            if (a->pending_offset == a->pending_count) {
                a->pending_count = bs_tx_read(&a->transmitter, a->pending, BS_BLOCK);
                a->pending_offset = 0;
                if (!a->pending_count) {
                    if ((a->tx_file || a->tx_report) && a->plan_position + 1 < a->plan_count) {
                        ++a->plan_position;
                        a->tx_file_index = a->tx_file ? a->plan_indices[a->plan_position] : 0;
                        if (!planned_frame(a, a->plan_position, &a->frame) ||
                            !bs_tx_init(&a->transmitter, &a->frame, a->config.amplitude,
                                        a->frozen_tones)) {
                            audio_error(a, "Cannot prepare next transfer block");
                            return;
                        }
                        continue;
                    }
                    a->state = BS_DRAINING;
                    break;
                }
            }
            int n = bs_audio_play(a->audio, a->pending + a->pending_offset,
                                  a->pending_count - a->pending_offset);
            if (n < 0) {
                audio_error(a, bs_audio_error(a->audio));
                return;
            }
            if (!n) {
                break;
            }
            a->pending_offset += (size_t)n;
        }
    }
    if (transmitting(a)) {
        /* A stopped queue may no longer expose a valid playback clock. Check completion
           before querying it, so successful draining cannot become a spurious error. */
        if (a->state == BS_DRAINING) {
            int done = bs_audio_finish(a->audio);
            if (done < 0) {
                audio_error(a, bs_audio_error(a->audio));
                return;
            }
            if (done) {
                end_transmission(a, true, "");
                return;
            }
        }
        int64_t played = bs_audio_played(a->audio);
        if (played < 0) {
            audio_error(a, bs_audio_error(a->audio));
            return;
        }
        if ((uint64_t)played > a->played) {
            a->last_progress = now;
        }
        a->played = (uint64_t)played;
        double duration = (double)a->total_samples / BS_RATE,
               seconds = fmin(duration, (double)a->played / BS_RATE);
        snprintf(a->instruction, sizeof a->instruction,
                 "KEEP PTT PRESSED | %.1f%% | %.1f seconds remaining | %s",
                 100 * seconds / duration, duration - seconds,
                 a->state == BS_DRAINING ? "DRAINING" : "PLAYING");
        if (now > a->deadline || now - a->last_progress > 2) {
            audio_error(a, "Playback clock stopped or exceeded deadline");
            return;
        }
    }
    if (a->state == BS_RELEASE && now >= a->deadline) {
        a->state = BS_COMPOSE;
    }
}
int bs_app_run(void) {
    BsApp *a = calloc(1, sizeof *a);
    if (!a) {
        return 1;
    }
    bs_parser_init(&a->parser, receive_event, a);
    bs_rx_init(&a->receiver, &a->parser);
    bool path = bs_config_path(a->config_path, sizeof a->config_path);
    if (path && bs_macros_path(a->config_path, a->macros_path, sizeof a->macros_path)) {
        bs_macros_load(a->macros_path, &a->macros);
    }
    bool loaded = path && bs_config_load(a->config_path, &a->config);
    if (!loaded) {
        memset(&a->config, 0, sizeof a->config);
        a->config.amplitude = 0.5;
        a->config.tones = 8;
    }
    strcpy(a->instruction, "Listening. Compose a short message, then press Enter.");
    if (loaded) {
        a->audio =
            bs_audio_open(a->config.input, a->config.output, a->instruction, sizeof a->instruction);
    }
    if (!a->audio) {
        if (!loaded) {
            strcpy(a->instruction, "First run: set callsign and audio devices, then press Enter.");
        }
        settings(a);
    }
    if (!bs_terminal_open()) {
        fprintf(stderr,
                "An interactive ANSI terminal is required. Use --help for offline commands.\n");
        bs_audio_close(a->audio);
        free(a);
        return 1;
    }
    atexit(bs_terminal_close);
    a->last_capture = bs_time();
    while (!a->quit && !bs_quit_requested()) {
        double now = bs_time();
        for (unsigned i = 0; i < 64; ++i) {
            int key = bs_key();
            if (key < 0) {
                break;
            }
            handle_key(a, key);
            if (a->quit) {
                break;
            }
        }
        if (a->quit || bs_quit_requested()) {
            break;
        }
        tick(a, now);
        if (now - a->last_render >= 0.05) {
            bs_tui_render(a);
            a->last_render = now;
        }
        bs_sleep(5);
    }
    bool was_transmitting = transmitting(a);
    if (was_transmitting) {
        end_transmission(a, false, "Application exiting.");
    }
    bs_audio_close(a->audio);
    bs_terminal_close();
    if (was_transmitting || a->state == BS_COUNTDOWN || a->state == BS_WAIT) {
        fprintf(stderr, "Transmission cancelled. RELEASE THE RADIO PTT.\n");
    } else if (a->state == BS_RELEASE) {
        fprintf(stderr, "Audio is stopped. RELEASE THE RADIO PTT.\n");
    }
    free(a);
    return 0;
}
