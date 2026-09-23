#include "app.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static bool transmitting(const BsApp *a) {
    return a->state == BS_PLAYING || a->state == BS_DRAINING;
}
static unsigned next_bit_rate(unsigned rate, bool forward) {
    for (unsigned i = 0; i < BS_PROFILE_COUNT; ++i) {
        if (bs_bit_rates[i] == rate) {
            return bs_bit_rates[(i + (forward ? 1 : BS_PROFILE_COUNT - 1)) % BS_PROFILE_COUNT];
        }
    }
    return BS_DEFAULT_BIT_RATE;
}
static void settings(BsApp *a) {
    a->editing = a->config;
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
static void end_transmission(BsApp *a, bool success, const char *reason) {
    snprintf(a->instruction, sizeof a->instruction, "%s - RELEASE THE RADIO PTT.%s%.450s",
             success ? "Transmission completed" : "Transmission interrupted", *reason ? " " : "",
             reason);
    bs_log_add(&a->log, a->instruction);
    bs_log_message(&a->log, success ? "TX PLAYED" : "TX INTERRUPTED", a->config.call, a->frozen);
    a->state = BS_RELEASE;
    a->deadline = bs_time() + 3;
    /* Discard acoustic loopback and queued capture before reacquiring an unrelated frame. */
    if (a->audio && !bs_audio_reset(a->audio)) {
        bs_log_add(&a->log, bs_audio_error(a->audio));
        bs_audio_close(a->audio);
        a->audio = NULL;
    }
    bs_parser_abort(&a->parser, "Local transmission ended");
    bs_rx_init(&a->receiver, &a->parser);
    a->last_capture = bs_time();
}
static void audio_error(BsApp *a, const char *reason) {
    char copy[512];
    snprintf(copy, sizeof copy, "%s", reason);
    if (transmitting(a)) {
        end_transmission(a, false, copy);
    } else {
        bs_parser_abort(&a->parser, copy);
        snprintf(a->instruction, sizeof a->instruction,
                 "Audio offline. RELEASE PTT IF HELD: %.390s. Reconnect in settings.", copy);
    }
    if (a->audio) {
        bs_audio_close(a->audio);
    }
    a->audio = NULL;
    if (a->state != BS_RELEASE && a->state != BS_SETTINGS) {
        a->state = BS_COMPOSE;
    }
}
static void prepare(BsApp *a) {
    if (!a->audio) {
        strcpy(a->instruction, "Open audio devices in settings before transmitting.");
        return;
    }
    if (bs_parser_active(&a->parser)) {
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
    if (!bs_bit_rate_valid(a->config.bit_rate) ||
        !bs_frame_build(&a->frame, a->config.call, a->compose)) {
        strcpy(a->instruction, "Callsign must contain exactly six letters/digits; TX rate must be "
                               "10, 25 or 50. Check settings.");
        return;
    }
    strcpy(a->frozen, a->compose);
    strcpy(a->last, a->compose);
    a->state = BS_WAIT;
    a->frozen_bit_rate = a->config.bit_rate;
    snprintf(a->instruction, sizeof a->instruction,
             "HOLD THE RADIO PTT, THEN PRESS SPACE. Frozen frame: %.2f seconds at %u bit/s. Esc "
             "cancels.",
             bs_frame_duration(&a->frame, a->frozen_bit_rate), a->frozen_bit_rate);
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
static void settings_key(BsApp *a, int key) {
    if (key == 27 || key == 7) {
        a->state = BS_COMPOSE;
        strcpy(a->instruction, "Settings cancelled.");
        return;
    }
    if (key == 9) {
        a->setting = (a->setting + 1) % 5;
        return;
    }
    if (key == 13 || key == 10) {
        if (!bs_normalize(a->editing.call, true, NULL) || !a->input_count || !a->output_count ||
            !bs_bit_rate_valid(a->editing.bit_rate)) {
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
        bs_parser_abort(&a->parser, "Audio settings changed");
        bs_rx_init(&a->receiver, &a->parser);
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
            a->editing.bit_rate = next_bit_rate(a->editing.bit_rate, key == BS_KEY_UP);
        } else if (count) {
            *index = (*index + (key == BS_KEY_UP ? count - 1 : 1)) % count;
        }
    }
}
static void cancel(BsApp *a) {
    if (transmitting(a)) {
        end_transmission(a, false, "Cancelled by operator.");
    } else if (a->state != BS_RELEASE) {
        a->state = BS_COMPOSE;
        strcpy(a->instruction, "Cancelled. Release PTT if held.");
    }
}
static void run_command(BsApp *a, char key) {
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
    case 'o':
        settings(a);
        break;
    case 'd': {
        BsConfig editing = a->editing;
        settings(a);
        a->editing = editing;
        strcpy(a->instruction, "Device list refreshed. Check selections before saving.");
        break;
    }
    case 'r':
        strcpy(a->compose, a->last);
        prepare(a);
        break;
    case 'a':
        bs_parser_abort(&a->parser, "Reception explicitly cancelled by operator");
        bs_rx_init(&a->receiver, &a->parser);
        strcpy(a->instruction, "Local reception cancelled. Enter prepares the composed message.");
        break;
    case 'b': {
        a->config.bit_rate = next_bit_rate(a->config.bit_rate, true);
        bool saved = *a->config_path && bs_config_save(a->config_path, &a->config);
        snprintf(a->instruction, sizeof a->instruction,
                 "TX rate: %u bit/s (%s). RX detects all three rates automatically.",
                 a->config.bit_rate, saved ? "saved" : "session only");
        break;
    }
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
        a->compose[0] = 0;
        break;
    case 'p':
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
        } else if (key >= 'a' && key <= 'z' && bs_command_available(a, (char)key)) {
            a->command_menu = false;
            run_command(a, (char)key);
        }
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
    if (transmitting(a) || a->state == BS_COUNTDOWN || a->state == BS_RELEASE) {
        return;
    }
    if (a->state == BS_WAIT) {
        if (key == ' ') {
            if (bs_parser_active(&a->parser)) {
                strcpy(a->instruction, "A frame is arriving. Release PTT and wait, or abort "
                                       "reception from the command menu.");
            } else {
                a->state = BS_COUNTDOWN;
                a->deadline = bs_time() + 2;
            }
        }
        return;
    }
    if (key == 13 || key == 10) {
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
        if (bs_parser_active(&a->parser)) {
            a->state = BS_WAIT;
            strcpy(a->instruction,
                   "Reception started during countdown. Release PTT and wait; Space retries.");
        } else if (now >= a->deadline) {
            if (!a->audio || !bs_audio_begin(a->audio)) {
                audio_error(a, a->audio ? bs_audio_error(a->audio) : "Audio unavailable");
                return;
            }
            if (!bs_tx_init(&a->transmitter, &a->frame, a->config.amplitude, a->frozen_bit_rate)) {
                audio_error(a, "Invalid transmit rate");
                return;
            }
            a->pending_count = a->pending_offset = 0;
            a->played = 0;
            a->last_progress = now;
            a->deadline = now + bs_frame_duration(&a->frame, a->frozen_bit_rate) + 5;
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
        double duration = bs_frame_duration(&a->frame, a->frozen_bit_rate),
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
    bs_parser_init(&a->parser, bs_log_event, &a->log);
    bs_rx_init(&a->receiver, &a->parser);
    bool path = bs_config_path(a->config_path, sizeof a->config_path);
    bool loaded = path && bs_config_load(a->config_path, &a->config);
    if (!loaded) {
        memset(&a->config, 0, sizeof a->config);
        a->config.amplitude = 0.5;
        a->config.bit_rate = BS_DEFAULT_BIT_RATE;
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
