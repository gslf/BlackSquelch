/* Exercise the real adapter against miniaudio's explicit test-only null device. */
#include "../src/audio.c"
#include "platform.h"
#include <assert.h>
#include <limits.h>
static void queue_test(void) {
    BsAudioQueue *queue = calloc(1, sizeof *queue);
    int16_t *input = calloc(BS_AUDIO_CAPACITY + 1, sizeof *input);
    int16_t *output = calloc(BS_AUDIO_CAPACITY + 1, sizeof *output);
    assert(queue && input && output);
    bs_audio_queue_init(queue);
    atomic_store(&queue->read, UINT_MAX - 50);
    atomic_store(&queue->write, UINT_MAX - 50);
    for (unsigned i = 0; i < BS_AUDIO_CAPACITY; ++i) {
        input[i] = (int16_t)i;
    }
    assert(bs_audio_queue_write(queue, input, BS_AUDIO_CAPACITY + 1) == BS_AUDIO_CAPACITY);
    assert(bs_audio_queue_count(queue) == BS_AUDIO_CAPACITY);
    assert(!bs_audio_queue_write(queue, input, 1));
    assert(bs_audio_queue_read(queue, output, 117) == 117);
    assert(bs_audio_queue_read(queue, output + 117, BS_AUDIO_CAPACITY) == BS_AUDIO_CAPACITY - 117);
    assert(!memcmp(input, output, BS_AUDIO_CAPACITY * sizeof *input));
    assert(!bs_audio_queue_count(queue));
    free(queue);
    free(input);
    free(output);
}
static void callback_test(void) {
    BsAudio *a = calloc(1, sizeof *a);
    assert(a);
    bs_audio_queue_init(&a->output);
    bs_audio_queue_init(&a->input);
    atomic_init(&a->fault, false);
    atomic_init(&a->eof, false);
    atomic_init(&a->played, 0);
    atomic_init(&a->clock.armed, false);
    atomic_init(&a->clock.broken, false);
    a->clock.fixed_delay = BS_BLOCK * 3;
    ma_context context = {0};
    context.backend = ma_backend_null;
    ma_device device = {0};
    device.pContext = &context;
    device.pUserData = a;
    device.playback.format = ma_format_s16;
    device.playback.channels = 1;
    ma_data_converter_config conversion =
        ma_data_converter_config_init(ma_format_s16, ma_format_s16, 1, 1, BS_RATE, BS_RATE);
    assert(ma_data_converter_init(&conversion, NULL, &a->output_converter) == MA_SUCCESS);
    int16_t in[BS_BLOCK], out[BS_BLOCK];
    for (size_t i = 0; i < BS_BLOCK; ++i) {
        in[i] = (int16_t)(i + 1);
    }
    a->begun = true;
    assert(bs_audio_play(a, in, BS_BLOCK) == BS_BLOCK);
    memset(out, 0, sizeof out);
    playback_data(&device, out, NULL, BS_BLOCK);
    assert(!out[0] && !a->consumed); /* Prefill cannot insert a gap in the preamble. */
    assert(!bs_audio_finish(a));
    playback_data(&device, out, NULL, BS_BLOCK);
    assert(!memcmp(in, out, sizeof in));
    assert(a->consumed == BS_BLOCK && !bs_audio_finish(a));
    for (unsigned i = 0; i < 3; ++i) {
        playback_data(&device, out, NULL, BS_BLOCK);
        assert(!bs_audio_finish(a));
    }
    playback_data(&device, out, NULL, BS_BLOCK);
    assert(bs_audio_finish(a) == 1);
    atomic_store(&a->eof, false);
    playback_data(&device, out, NULL, BS_BLOCK);
    assert(bs_audio_played(a) == -1); /* Missing producer samples are never hidden. */
    atomic_store(&a->fault, false);
    int16_t *flood = calloc(BS_AUDIO_CAPACITY + 1, sizeof *flood);
    assert(flood);
    capture_data(&device, NULL, flood, BS_AUDIO_CAPACITY + 1);
    assert(bs_audio_capture(a, out, BS_BLOCK) == -1);
    free(flood);
    ma_data_converter_uninit(&a->output_converter, NULL);
    free(a);
}
static void device_test(void) {
    char error[256];
    assert(!bs_audio_open("missing-device", "default", error, sizeof error));
    assert(strstr(error, "unavailable"));
    BsAudio *a = bs_audio_open("default", "default", error, sizeof error);
    if (!a) {
        fprintf(stderr, "%s\n", error);
    }
    assert(a);
    int16_t samples[BS_BLOCK] = {0};
    for (unsigned run = 0; run < 3; ++run) {
        assert(bs_audio_begin(a));
        for (unsigned i = 0; i < 10; ++i) {
            assert(bs_audio_play(a, samples, BS_BLOCK) == BS_BLOCK);
        }
        assert(!bs_audio_finish(a));
        double deadline = bs_time() + 3;
        int finished = 0;
        while (!(finished = bs_audio_finish(a)) && bs_time() < deadline) {
            assert(bs_audio_capture(a, samples, BS_BLOCK) >= 0);
            bs_sleep(2);
        }
        if (finished != 1) {
            fprintf(stderr, "%s\n", bs_audio_error(a));
        }
        assert(finished == 1 && bs_audio_played(a) == 10 * BS_BLOCK);
        assert(bs_audio_reset(a));
    }
    assert(bs_audio_begin(a));
    assert(bs_audio_play(a, samples, BS_BLOCK) == BS_BLOCK);
    assert(bs_audio_reset(a));
    assert(!a->submitted && !a->begun && !bs_audio_queue_count(&a->output));
    bs_audio_close(a);
}
int main(void) {
    queue_test();
    callback_test();
    device_test();
    puts("PASS audio: bounded ring, cursor wrap, prefill, delayed completion, overflow/underrun, "
         "null device, reset");
}
