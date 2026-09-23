/* Include the controller to exercise its private transitions with a deterministic audio clock.
   The production binary still compiles it as an independent translation unit. */
#include "../src/app.c"
#include <assert.h>
struct BsAudio {
    unsigned writes, resets;
    int finish;
    int64_t played;
    bool fail_begin;
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
    (void)s;
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
    a->config.bit_rate = BS_DEFAULT_BIT_RATE;
    strcpy(a->compose, "HELLO");
    bs_parser_init(&a->parser, bs_log_event, &a->log);
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
static void test_rate_selection(void) {
    BsApp *a = fresh();
    strcpy(a->config_path, "test-config.txt");
    strcpy(a->config.input, "input-device");
    strcpy(a->config.output, "output-device");
    const unsigned expected[] = {25, 50, 10};
    for (unsigned i = 0; i < BS_PROFILE_COUNT; ++i) {
        command(a, 'b');
        assert(a->config.bit_rate == expected[i]);
        BsConfig saved;
        assert(bs_config_load(a->config_path, &saved) && saved.bit_rate == expected[i]);
        prepare(a);
        assert(a->state == BS_WAIT && a->frozen_bit_rate == expected[i]);
        command(a, 'b');
        assert(a->command_menu);
        handle_key(a, 7);
        assert(a->frozen_bit_rate == expected[i] && a->config.bit_rate == expected[i]);
        handle_key(a, ' ');
        double now = bs_time();
        a->deadline = now;
        tick(a, now);
        assert(a->state == BS_PLAYING);
        assert(a->transmitter.symbol_samples == bs_symbol_samples(expected[i]));
        assert(fabs(a->deadline - now - bs_frame_duration(&a->frame, expected[i]) - 5) < 1e-7);
        for (unsigned pass = 0; pass < 1000 && a->state == BS_PLAYING; ++pass) {
            tick(a, now);
        }
        assert(a->state == BS_DRAINING);
        assert(a->transmitter.sample == a->frame.count * (uint64_t)bs_symbol_samples(expected[i]));
        handle_key(a, 27);
        a->state = BS_COMPOSE;
    }
    settings(a);
    for (unsigned i = 0; i < 4; ++i) {
        settings_key(a, 9);
    }
    assert(a->setting == 4);
    settings_key(a, BS_KEY_UP);
    assert(a->editing.bit_rate == 25);
    settings_key(a, BS_KEY_DOWN);
    assert(a->editing.bit_rate == 10);
    settings_key(a, 27);
    assert(a->config.bit_rate == 10);
    a->config.bit_rate = 30;
    prepare(a);
    assert(a->state == BS_COMPOSE);
    a->config.bit_rate = 10;
    strcpy(a->config.call, "ABCDE");
    prepare(a);
    assert(a->state == BS_COMPOSE);
    remove(a->config_path);
    dispose(a);
    puts("PASS TX profiles: TUI cycling/settings, persistence, frozen rate, exact samples and "
         "deadlines");
}
static void test_config_schema(void) {
    const char *path = "test-config.txt";
    BsConfig config = {.call = "IU2ABC",
                       .input = "default",
                       .output = "default",
                       .amplitude = 0.5,
                       .bit_rate = 25};
    assert(bs_config_save(path, &config));
    BsConfig loaded;
    assert(bs_config_load(path, &loaded) && loaded.bit_rate == 25);
    const char *lines[] = {"schema=1",       "callsign=IU2ABC", "input=default",
                           "output=default", "amplitude=0.5",   "bit_rate=25"};
    const char *extra[] = {"schema=2", "unknown=value", "callsign=ABCDEF", "bit_rate=50",
                           "bad line"};
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
    config.bit_rate = 30;
    assert(!bs_config_save(path, &config));
    config.bit_rate = 10;
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
    assert(a->state == BS_COMPOSE && a->config.bit_rate == 10 && !a->quit);
    command(a, 'u');
    assert(!*a->compose && !a->command_menu);
    command(a, 'q');
    assert(a->quit);
    dispose(a);
    puts("PASS commands: prefix, unavailable keys, dismissal, removed shortcuts, clear and quit");
}
int main(void) {
    test_rate_selection();
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
    a->audio->played = (int64_t)a->frame.count * bs_symbol_samples(a->frozen_bit_rate);
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
