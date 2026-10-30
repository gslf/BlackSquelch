/* Include the controller to exercise its private transitions with a deterministic audio clock.
   The production binary still compiles it as an independent translation unit. */
#include "../src/app.c"
#include <assert.h>
struct BsAudio {
    unsigned writes, resets;
    int finish;
    int64_t played;
    bool fail_begin;
    BsReceiver *sink;
    uint64_t samples;
    BsApp *owner;
    bool drop_block;
    size_t dropped;
};
size_t bs_audio_devices(bool input, BsDevice *devices, size_t capacity) {
    (void)input;
    (void)devices;
    (void)capacity;
    return 0;
}
BsAudio *bs_audio_open(const char *i, const char *o, char *e, size_t c) {
    (void)i;
    (void)o;
    (void)e;
    (void)c;
    return NULL;
}
void bs_audio_close(BsAudio *a) {
    free(a);
}
int bs_audio_capture(BsAudio *a, int16_t *s, size_t n) {
    (void)a;
    (void)s;
    (void)n;
    return 0;
}
int bs_audio_play(BsAudio *a, const int16_t *s, size_t n) {
    if (a->sink) {
        if (a->drop_block && a->owner->tx_file && a->owner->tx_file_index == a->dropped) {
            int16_t silence[BS_BLOCK] = {0};
            assert(n <= BS_BLOCK);
            bs_rx_read(a->sink, silence, n);
        } else {
            bs_rx_read(a->sink, s, n);
        }
    }
    a->samples += n;
    ++a->writes;
    return (int)n;
}
bool bs_audio_begin(BsAudio *a) {
    return !a->fail_begin;
}
int bs_audio_finish(BsAudio *a) {
    return a->finish;
}
int64_t bs_audio_played(BsAudio *a) {
    return a->played;
}
bool bs_audio_reset(BsAudio *a) {
    ++a->resets;
    return true;
}
const char *bs_audio_error(BsAudio *a) {
    (void)a;
    return "Injected audio failure";
}
void bs_tui_render(const BsApp *a) {
    (void)a;
}
static BsApp *fresh(void) {
    BsApp *a = calloc(1, sizeof *a);
    assert(a);
    a->audio = calloc(1, sizeof *a->audio);
    assert(a->audio);
    strcpy(a->config.call, "IU2ABC");
    a->config.amplitude = 0.5;
    a->config.tones = 8;
    strcpy(a->compose, "HELLO");
    bs_parser_init(&a->parser, receive_event, a);
    bs_rx_init(&a->receiver, &a->parser);
    a->last_capture = bs_time();
    return a;
}
static void dispose(BsApp *a) {
    bs_audio_close(a->audio);
    free(a);
}
static void command(BsApp *a, char key) {
    handle_key(a, 24);
    assert(a->command_menu);
    handle_key(a, key);
}
static void test_tone_selection(void) {
    BsApp *a = fresh();
    strcpy(a->config_path, "test-config.txt");
    strcpy(a->config.input, "input-device");
    strcpy(a->config.output, "output-device");
    settings(a);
    for (unsigned i = 0; i < 4; ++i) {
        settings_key(a, 9);
    }
    assert(a->setting == 4 && a->editing.tones == 8);
    settings_key(a, BS_KEY_UP);
    assert(a->editing.tones == 16);
    settings_key(a, BS_KEY_DOWN);
    assert(a->editing.tones == 8);
    settings_key(a, 27);
    for (unsigned tones = 8; tones <= 16; tones += 8) {
        a->config.tones = tones;
        assert(bs_config_save(a->config_path, &a->config));
        BsConfig loaded;
        assert(bs_config_load(a->config_path, &loaded) && loaded.tones == tones);
        prepare(a);
        assert(a->state == BS_WAIT && a->frozen_tones == tones);
        a->config.tones = tones == 8 ? 16 : 8; /* Frozen profile survives later changes. */
        handle_key(a, ' ');
        double now = bs_time();
        a->deadline = now;
        a->last_capture = now;
        tick(a, now);
        assert(a->state == BS_PLAYING && a->transmitter.tones == tones);
        assert(fabs(a->deadline - now - bs_frame_duration(&a->frame, tones) - 5) < 1e-7);
        for (unsigned pass = 0; pass < 1000 && a->state == BS_PLAYING; ++pass) {
            tick(a, now);
        }
        assert(a->state == BS_DRAINING &&
               a->transmitter.sample == bs_frame_samples(&a->frame, tones));
        handle_key(a, 27);
        a->state = BS_COMPOSE;
    }
    a->config.tones = 2;
    prepare(a);
    assert(a->state == BS_COMPOSE);
    a->config.tones = 8;
    strcpy(a->config.call, "ABCDE");
    prepare(a);
    assert(a->state == BS_COMPOSE);
    remove(a->config_path);
    dispose(a);
    puts("PASS MFSK profiles: settings, persistence, frozen tones, exact samples/deadlines, "
         "removed mode rejected");
}
static void test_config_schema(void) {
    const char *path = "test-config.txt";
    BsConfig config = {
        .call = "IU2ABC", .input = "default", .output = "default", .amplitude = 0.5, .tones = 16};
    assert(bs_config_save(path, &config));
    BsConfig loaded;
    assert(bs_config_load(path, &loaded) && loaded.tones == 16);
    const char *lines[] = {"schema=2",       "callsign=IU2ABC", "input=default",
                           "output=default", "amplitude=0.5",   "tones=16"};
    const char *extra[] = {"schema=1", "unknown=value", "callsign=ABCDEF", "tones=8", "bad line"};
    for (unsigned missing = 0; missing < 6; ++missing) {
        FILE *file = fopen(path, "w");
        assert(file);
        for (unsigned i = 0; i < 6; ++i) {
            if (i != missing) {
                fprintf(file, "%s\n", lines[i]);
            }
        }
        fclose(file);
        assert(!bs_config_load(path, &loaded));
    }
    for (size_t j = 0; j < sizeof extra / sizeof extra[0]; ++j) {
        assert(bs_config_save(path, &config));
        FILE *file = fopen(path, "a");
        assert(file);
        fprintf(file, "%s\n", extra[j]);
        fclose(file);
        assert(!bs_config_load(path, &loaded));
    }
    config.tones = 2;
    assert(!bs_config_save(path, &config));
    config.tones = 8;
    strcpy(config.input, "bad\ninput");
    assert(!bs_config_save(path, &config));
    remove(path);
    puts("PASS config: complete schema required; unknown, duplicate and missing fields rejected");
}
static void test_commands(void) {
    BsApp *a = fresh();
    handle_key(a, 24);
    assert(a->command_menu);
    handle_key(a, 'z');
    assert(a->command_menu && !strcmp(a->compose, "HELLO"));
    handle_key(a, 7);
    assert(!a->command_menu && !strcmp(a->compose, "HELLO"));
    handle_key(a, 2);
    handle_key(a, 19);
    handle_key(a, 17);
    assert(a->state == BS_COMPOSE && a->config.tones == 8 && !a->quit);
    command(a, 'u');
    assert(!*a->compose && !a->command_menu);
    handle_key(a, 24);
    handle_key(a, BS_KEY_DOWN);
    handle_key(a, 13);
    assert(!a->command_menu &&
           a->state == BS_MACROS); /* Arrow selection skips unavailable actions. */
    handle_key(a, 27);
    handle_key(a, 24);
    a->command_scroll = 999;
    handle_key(a, BS_KEY_UP);
    assert(a->command_scroll < bs_command_count);
    handle_key(a, 7);
    command(a, 'q');
    assert(a->quit);
    dispose(a);
    puts("PASS commands: prefix, unavailable keys, dismissal, removed shortcuts, clear and quit");
}
static void test_file_transfer(void) {
    const char *path = "test-Mixed Case.png";
    FILE *file = fopen(path, "wb");
    assert(file);
    for (unsigned i = 0; i < 300; ++i) {
        assert(fputc((int)(i & 255), file) != EOF);
    }
    assert(!fclose(file));
    BsApp *sender = fresh(), *recipient = fresh();
    sender->config.tones = 16;
    sender->audio->sink = &recipient->receiver;
    command(sender, 't');
    assert(sender->state == BS_FILE_SELECT);
    for (const char *p = path; *p; ++p) {
        handle_key(sender, (unsigned char)*p);
    }
    assert(!strcmp(sender->file_path, path));
    handle_key(sender, 13);
    assert(sender->state == BS_WAIT && sender->tx_file && sender->last_is_file);
    assert(!strcmp(sender->compose, "HELLO") && sender->frozen_tones == 16);
    assert(sender->outgoing_file.info.size == 300);
    uint32_t id = sender->outgoing_file.info.id;
    size_t total_bits = sender->total_bits;
    uint64_t total_samples = sender->total_samples;
    /* Later disk changes cannot change the prepared bytes or a manual repeat. */
    file = fopen(path, "wb");
    assert(file && fputs("changed", file) >= 0 && !fclose(file));
    handle_key(sender, ' ');
    double now = bs_time();
    sender->deadline = now;
    for (unsigned i = 0; i < 1000 && sender->state != BS_DRAINING; ++i) {
        tick(sender, now);
    }
    assert(sender->state == BS_DRAINING && sender->tx_file_index == bs_file_chunk_count(300) - 1);
    assert(sender->audio->samples == total_samples);
    assert(recipient->incoming_file.complete && recipient->incoming_file.saved);
    assert(!sender->history_count && !strcmp(sender->compose, "HELLO"));
    assert(!memcmp(recipient->incoming_file.file.bytes, sender->outgoing_file.bytes, 300));
    assert(strstr(recipient->file_status, "FILE RECEIVED"));
    assert(!strstr(sender->instruction, "completed"));
    sender->audio->finish = 1;
    tick(sender, now);
    assert(sender->state == BS_RELEASE && strstr(sender->instruction, "completed"));
    sender->state = BS_COMPOSE;
    command(sender, 'r');
    assert(sender->state == BS_WAIT && sender->tx_file_index == 0);
    assert(sender->outgoing_file.info.id == id && sender->total_bits == total_bits);
    handle_key(sender, 27);
    command(sender, 't');
    strcpy(sender->file_path, "no-such-file");
    handle_key(sender, 13);
    assert(sender->state == BS_FILE_SELECT && sender->outgoing_file.info.id == id);
    handle_key(sender, 27);
    command(sender, 'r');
    assert(sender->state == BS_WAIT && sender->outgoing_file.info.size == 300);
    handle_key(sender, 27);
    /* Incomplete file assemblies stay abortable between frames. */
    recipient->incoming_file.complete = recipient->incoming_file.saved = false;
    command(recipient, 'a');
    assert(!recipient->incoming_file.active);
    prepare(sender);
    assert(!sender->tx_file && !sender->last_is_file);
    char received[256];
    snprintf(received, sizeof received, "received-files/%08X-0-test-Mixed_Case.png", (unsigned)id);
    file = fopen(received, "rb");
    assert(file);
    for (unsigned i = 0; i < 300; ++i) {
        assert(fgetc(file) == (int)(i & 255));
    }
    assert(fgetc(file) == EOF && !fclose(file));
    remove(received);
    remove(path);
    dispose(sender);
    dispose(recipient);
    puts("PASS live file transfer: path case/spaces, frozen bytes, multi-block playback, automatic "
         "save, repeat and abort");
}
static void play_prepared(BsApp *a) {
    assert(a->state == BS_WAIT);
    handle_key(a, ' ');
    double now = bs_time();
    a->deadline = now;
    a->last_capture = now;
    a->audio->finish = 1;
    for (unsigned i = 0; i < 10000 && a->state != BS_RELEASE; ++i) {
        tick(a, now);
    }
    assert(a->state == BS_RELEASE && strstr(a->instruction, "completed"));
    a->state = BS_COMPOSE;
}
static void test_text_history(void) {
    BsApp *a = fresh();
    prepare(a);
    play_prepared(a);
    assert(!*a->compose && a->history_count == 1 && !strcmp(a->history[0], "HELLO"));
    assert(strstr(a->log.conversation[0], "HELLO"));
    strcpy(a->compose, "MY DRAFT");
    handle_key(a, BS_KEY_UP);
    assert(!strcmp(a->compose, "HELLO"));
    handle_key(a, 'X');
    assert(!strcmp(a->compose, "HELLOX") && !strcmp(a->history[0], "HELLO"));
    handle_key(a, BS_KEY_DOWN);
    assert(!strcmp(a->compose, "MY DRAFT"));
    strcpy(a->compose, "SECOND MESSAGE");
    prepare(a);
    play_prepared(a);
    assert(!*a->compose && a->history_count == 2);
    handle_key(a, BS_KEY_UP);
    assert(!strcmp(a->compose, "SECOND MESSAGE"));
    handle_key(a, BS_KEY_UP);
    handle_key(a, BS_KEY_UP);
    assert(!strcmp(a->compose, "HELLO") && a->history_offset == 2);
    handle_key(a, BS_KEY_DOWN);
    assert(!strcmp(a->compose, "SECOND MESSAGE"));
    handle_key(a, BS_KEY_DOWN);
    assert(!*a->compose);
    command(a, 'r');
    play_prepared(a);
    assert(a->history_count == 2); /* Consecutive duplicates do not fill history. */
    strcpy(a->compose, "KEEP IF CANCELLED");
    prepare(a);
    handle_key(a, ' ');
    double now = bs_time();
    a->deadline = now;
    a->last_capture = now;
    tick(a, now);
    assert(a->state == BS_PLAYING);
    handle_key(a, 27);
    assert(a->state == BS_RELEASE && !strcmp(a->compose, "KEEP IF CANCELLED") &&
           a->history_count == 2);
    a->state = BS_COMPOSE;
    prepare(a);
    play_prepared(a);
    assert(!*a->compose && a->history_count == 3);
    handle_key(a, BS_KEY_UP);
    assert(!strcmp(a->compose, "KEEP IF CANCELLED"));
    command(a, 'u');
    handle_key(a, BS_KEY_UP);
    handle_key(a, BS_KEY_DOWN);
    assert(!*a->compose); /* Clear also resets the saved browsing draft. */
    for (unsigned i = 0; i < BS_HISTORY_COUNT + 5; ++i) {
        char message[40];
        snprintf(message, sizeof message, "MESSAGE %u", i);
        history_add(a, message);
    }
    strcpy(a->compose, "RESTORED DRAFT");
    for (unsigned i = 0; i < BS_HISTORY_COUNT + 5; ++i) {
        handle_key(a, BS_KEY_UP);
    }
    assert(a->history_count == BS_HISTORY_COUNT && !strcmp(a->compose, "MESSAGE 5"));
    for (unsigned i = 0; i < BS_HISTORY_COUNT; ++i) {
        handle_key(a, BS_KEY_DOWN);
    }
    assert(!strcmp(a->compose, "RESTORED DRAFT"));
    a->state = BS_RELEASE;
    handle_key(a, BS_KEY_UP);
    assert(!strcmp(a->compose, "MESSAGE 36")); /* Recall is available immediately at completion. */
    dispose(a);
    puts("PASS text history: clear after drain, no clear on interruption, recall/edit, draft "
         "restoration and bounded ring");
}
static void type_keys(BsApp *a, const char *text) {
    for (const char *p = text; *p; ++p) {
        handle_key(a, (unsigned char)*p);
    }
}
static void test_macro_editor(void) {
    BsApp *a = fresh();
    strcpy(a->macros_path, "test-editor-macros.txt");
    command(a, 'w');
    assert(a->state == BS_MACROS && a->macro_editing && !strcmp(a->macro_text, "HELLO"));
    handle_key(a, 19);
    assert(a->state == BS_COMPOSE && !strcmp(a->macros.text[0], "HELLO"));
    BsMacros loaded;
    assert(bs_macros_load(a->macros_path, &loaded) && !strcmp(loaded.text[0], "HELLO"));
    command(a, 'u');
    command(a, '1');
    assert(!strcmp(a->compose, "HELLO") && a->state == BS_COMPOSE && !a->audio->writes);
    command(a, '1');
    assert(!strcmp(a->compose, "HELLO HELLO"));
    command(a, 'k');
    assert(a->state == BS_MACROS && !a->macro_editing);
    handle_key(a, 13);
    handle_key(a, 21);
    type_keys(a, "cq de iu2abc");
    handle_key(a, 13);
    assert(!a->macro_editing && !strcmp(a->editing_macros.text[0], "CQ DE IU2ABC"));
    handle_key(a, 27);
    assert(a->state == BS_COMPOSE && !strcmp(a->macros.text[0], "HELLO"));
    command(a, 'k');
    handle_key(a, BS_KEY_DOWN);
    handle_key(a, 13);
    type_keys(a, "bad@macro");
    handle_key(a, 19);
    assert(a->state == BS_MACROS && a->macro_editing && strstr(a->instruction, "Unsupported"));
    handle_key(a, 21);
    type_keys(a, "second message");
    strcpy(a->macros_path, "no-such-directory/macros.txt");
    handle_key(a, 19);
    assert(a->state == BS_MACROS && !*a->macros.text[1] &&
           strstr(a->instruction, "Could not save"));
    strcpy(a->macros_path, "test-editor-macros.txt");
    handle_key(a, 19);
    assert(a->state == BS_COMPOSE && !strcmp(a->macros.text[1], "SECOND MESSAGE"));
    memset(a->compose, 'A', BS_MAX_TEXT);
    a->compose[BS_MAX_TEXT] = 0;
    command(a, '2');
    assert(strlen(a->compose) == BS_MAX_TEXT && strstr(a->instruction, "exceed"));
    command(a, 'k');
    handle_key(a, 13);
    handle_key(a, 21);
    handle_key(a, 19);
    assert(!*a->macros.text[0] && !bs_command_available(a, '1'));
    settings(a);
    command(a, 'k');
    assert(a->state == BS_MACROS && a->macro_return_state == BS_SETTINGS);
    handle_key(a, 27);
    assert(a->state == BS_SETTINGS);
    handle_key(a, BS_KEY_BACK_TAB);
    assert(a->setting == 4);
    handle_key(a, 27);
    a->compose[0] = 0;
    command(a, 'k');
    audio_error(a, "Device went away while editing macros");
    assert(!a->audio && a->state == BS_MACROS);
    handle_key(a, 27);
    command(a, '2');
    assert(!strcmp(a->compose, "SECOND MESSAGE")); /* Macro insertion works offline. */
    remove(a->macros_path);
    dispose(a);
    puts("PASS macro editor: assign draft, save/reload, prefix insertion, cancel, "
         "invalid/overflow/save failure and offline editing");
}
static void test_selective_mfsk(void) {
    const char *source = "test-selective.bin";
    FILE *file = fopen(source, "wb");
    assert(file);
    for (unsigned i = 0; i < 130; ++i) {
        assert(fputc((int)(i ^ 0xA5), file) != EOF);
    }
    assert(!fclose(file));
    BsApp *sender = fresh(), *recipient = fresh();
    strcpy(recipient->config.call, "IU2XYZ");
    sender->config.tones = 8;
    recipient->config.tones = 16;
    sender->audio->sink = &recipient->receiver;
    sender->audio->owner = sender;
    sender->audio->drop_block = true;
    sender->audio->dropped = 1;
    recipient->audio->sink = &sender->receiver;
    command(sender, 't');
    strcpy(sender->file_path, source);
    handle_key(sender, 13);
    assert(sender->plan_count == 3 && sender->frozen_tones == 8);
    uint32_t id = sender->outgoing_file.info.id;
    uint64_t full_samples = sender->total_samples;
    play_prepared(sender);
    assert(recipient->incoming_file.received == 2 && !recipient->incoming_file.complete);
    assert(!recipient->incoming_file.present[1] && recipient->incoming_file.present[2]);
    prepare(sender);
    assert(!sender->last_is_file && sender->have_outgoing_file);
    handle_key(sender, 27);
    command(recipient, 'm');
    assert(recipient->tx_report && recipient->plan_count == 1 && recipient->frozen_tones == 16);
    play_prepared(recipient);
    assert(sender->repair_pending && sender->requested[1] && !sender->requested[0]);
    command(sender, 'r');
    assert(sender->plan_count == 1 && sender->plan_indices[0] == 1);
    assert(sender->total_samples < full_samples && sender->outgoing_file.info.id == id);
    sender->audio->drop_block = false;
    play_prepared(sender);
    assert(recipient->incoming_file.complete && recipient->incoming_file.saved);
    assert(!sender->repair_pending && !sender->requested[1]);
    command(recipient, 'm');
    assert(recipient->reports[0].complete);
    play_prepared(recipient);
    assert(strstr(sender->file_status, "receipt confirmed"));
    char received[256];
    snprintf(received, sizeof received, "received-files/%08X-0-test-selective.bin", (unsigned)id);
    file = fopen(received, "rb");
    assert(file);
    for (unsigned i = 0; i < 130; ++i) {
        assert(fgetc(file) == (int)(i ^ 0xA5));
    }
    assert(fgetc(file) == EOF && !fclose(file));
    remove(received);
    remove(source);
    /* FEC buffers a frame, so the parser alone is not an adequate RX interlock. */
    BsFrame frame;
    assert(bs_frame_build(&frame, "IU2XYZ", "INTERLOCK"));
    BsTransmitter tx;
    assert(bs_tx_init(&tx, &frame, 0.5, 16));
    int16_t block[BS_BLOCK];
    for (unsigned i = 0; i < 200 && !sender->receiver.mfsk.locked; ++i) {
        size_t n = bs_tx_read(&tx, block, BS_BLOCK);
        bs_rx_read(&sender->receiver, block, n);
    }
    assert(sender->receiver.mfsk.locked && !bs_parser_active(&sender->parser));
    prepare(sender);
    assert(sender->state == BS_COMPOSE && strstr(sender->instruction, "Receiving"));
    command(sender, 'a');
    assert(!bs_rx_active(&sender->receiver));
    prepare(sender);
    assert(sender->state == BS_WAIT && sender->frozen_tones == 8);
    dispose(sender);
    dispose(recipient);
    puts("PASS selective MFSK: lost block, MFSK16 NACK, MFSK8 repair only, exact file, receipt and "
         "RX interlock");
}
int main(void) {
    test_text_history();
    test_macro_editor();
    test_selective_mfsk();
    test_file_transfer();
    test_tone_selection();
    test_config_schema();
    test_commands();
    BsApp *a = fresh();
    double now = bs_time();
    handle_key(a, 13);
    assert(a->state == BS_WAIT && !a->audio->writes);
    handle_key(a, 'Z');
    assert(!strcmp(a->frozen, "HELLO") && !strcmp(a->compose, "HELLO"));
    handle_key(a, ' ');
    assert(a->state == BS_COUNTDOWN);
    tick(a, now + 0.1);
    assert(!a->audio->writes);
    a->deadline = now;
    tick(a, now);
    assert(a->state == BS_PLAYING && a->audio->writes);
    /* Generated samples may be queued in full while the playback clock is still at zero. */
    for (unsigned i = 0; i < 1000 && a->state == BS_PLAYING; ++i) {
        tick(a, now);
    }
    assert(a->state == BS_DRAINING && a->played == 0 && !strstr(a->instruction, "completed"));
    tick(a, now);
    assert(a->state == BS_DRAINING);
    a->audio->finish = 1;
    a->audio->played = (int64_t)a->total_samples;
    tick(a, now);
    assert(a->state == BS_RELEASE && strstr(a->instruction, "completed") && a->audio->resets == 1);
    assert(!strstr(a->instruction, "confirmation"));
    assert(strstr(a->log.conversation[0], "TX PLAYED"));
    assert(!strstr(a->log.conversation[0], "confirmation"));
    dispose(a);
    a = fresh();
    prepare(a);
    handle_key(a, ' ');
    a->deadline = now;
    tick(a, now);
    handle_key(a, 27);
    assert(a->state == BS_RELEASE && strstr(a->instruction, "interrupted") &&
           strstr(a->instruction, "RELEASE"));
    dispose(a);
    a = fresh();
    prepare(a);
    handle_key(a, ' ');
    a->audio->fail_begin = true;
    a->deadline = now;
    tick(a, now);
    assert(!a->audio && strstr(a->instruction, "RELEASE"));
    dispose(a);
    a = fresh();
    for (int i = 0; i < 32; ++i) {
        bs_parser_bit(&a->parser, i % 2 == 0, 1);
    }
    prepare(a);
    assert(a->state == BS_COMPOSE && strstr(a->instruction, "Receiving"));
    handle_key(a, 24);
    assert(bs_parser_active(&a->parser));
    handle_key(a, 'a');
    prepare(a);
    assert(a->state == BS_WAIT);
    dispose(a);
    a = fresh();
    prepare(a);
    handle_key(a, ' ');
    for (int i = 0; i < 32; ++i) {
        bs_parser_bit(&a->parser, i % 2 == 0, 1);
    }
    tick(a, now);
    assert(a->state == BS_WAIT && !a->audio->writes);
    dispose(a);
    a = fresh();
    strcpy(a->compose, "BAD@TEXT");
    prepare(a);
    assert(a->state == BS_COMPOSE && strstr(a->instruction, "position 4"));
    dispose(a);
    a = fresh();
    prepare(a);
    handle_key(a, 27);
    assert(a->state == BS_COMPOSE);
    command(a, 'r');
    assert(a->state == BS_WAIT);
    dispose(a);
    puts("PASS application: frozen frame, visual countdown, hardware drain, cancellation, errors, "
         "RX interlock, repeat");
    return 0;
}
