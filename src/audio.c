#include "audio.h"
#include "audio_driver.h"
#include "audio_queue.h"
#include "modem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct BsAudio {
    ma_context context;
    ma_device capture, playback;
    ma_device_id input_id, output_id;
    bool context_ready, capture_ready, playback_ready, input_default, output_default, begun;
    BsAudioQueue input, output;
    BsAudioClock clock;
    ma_data_converter output_converter;
    bool converter_ready;
    atomic_bool fault, closing, eof;
    atomic_uint_fast64_t played;
    uint64_t submitted, rendered, content_start, consumed;
    char error[256];
};
static bool context_open(ma_context *context) {
#ifdef BS_AUDIO_TEST
    ma_backend backends[] = {ma_backend_null};
#elif defined(_WIN32)
    ma_backend backends[] = {ma_backend_wasapi};
#elif defined(__APPLE__)
    ma_backend backends[] = {ma_backend_coreaudio};
#else
    ma_backend backends[] = {ma_backend_pulseaudio, ma_backend_alsa};
#endif
    return ma_context_init(backends, sizeof backends / sizeof backends[0], NULL, context) ==
           MA_SUCCESS;
}
static void device_id(ma_backend backend, const ma_device_id *native, char *id, size_t capacity) {
    const unsigned char *bytes = (const unsigned char *)native;
    size_t length = 0;
    const char *name = ma_get_backend_name(backend);
    switch (backend) {
    case ma_backend_wasapi:
        while (length < sizeof native->wasapi && (bytes[length] || bytes[length + 1])) {
            length += 2;
        }
        break;
    case ma_backend_alsa:
        length = strlen(native->alsa);
        break;
    case ma_backend_pulseaudio:
        length = strlen(native->pulse);
        break;
    case ma_backend_coreaudio:
        length = strlen(native->coreaudio);
        break;
    default:
        break;
    }
    /* Hash only the active native ID, never padding in the backend union. */
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < length; ++i) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    snprintf(id, capacity, "%s:%016llx", name, (unsigned long long)hash);
}
size_t bs_audio_devices(bool input, BsDevice *devices, size_t capacity) {
    if (!capacity) {
        return 0;
    }
    ma_context context;
    if (!context_open(&context)) {
        return 0;
    }
    snprintf(devices[0].id, sizeof devices[0].id, "default");
    snprintf(devices[0].name, sizeof devices[0].name, "System default (%s)",
             ma_get_backend_name(context.backend));
    size_t count = 1;
    ma_device_info *info;
    ma_uint32 total;
    ma_result result = input ? ma_context_get_devices(&context, NULL, NULL, &info, &total)
                             : ma_context_get_devices(&context, &info, &total, NULL, NULL);
    if (result == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < total && count < capacity; ++i) {
            device_id(context.backend, &info[i].id, devices[count].id, sizeof devices[count].id);
            snprintf(devices[count].name, sizeof devices[count].name, "%.127s", info[i].name);
            ++count;
        }
    }
    ma_context_uninit(&context);
    return count;
}
static bool resolve(BsAudio *a, bool input, const char *id, ma_device_id *native,
                    bool *use_default) {
    *use_default = !strcmp(id, "default");
    if (*use_default) {
        return true;
    }
    ma_device_info *info;
    ma_uint32 count;
    ma_result result = input ? ma_context_get_devices(&a->context, NULL, NULL, &info, &count)
                             : ma_context_get_devices(&a->context, &info, &count, NULL, NULL);
    unsigned matches = 0;
    if (result == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            char candidate[BS_DEVICE_ID];
            device_id(a->context.backend, &info[i].id, candidate, sizeof candidate);
            if (!strcmp(candidate, id)) {
                *native = info[i].id;
                ++matches;
            }
        }
    }
    return matches == 1;
}
static void notification(const ma_device_notification *event) {
    BsAudio *a = event->pDevice->pUserData;
    if (!atomic_load(&a->closing) &&
        (event->type == ma_device_notification_type_stopped ||
         event->type == ma_device_notification_type_rerouted ||
         event->type == ma_device_notification_type_interruption_began)) {
        atomic_store(&a->fault, true);
    }
}
static void capture_data(ma_device *device, void *output, const void *input, ma_uint32 count) {
    (void)output;
    BsAudio *a = device->pUserData;
    if (!input || bs_audio_queue_write(&a->input, input, count) != count) {
        atomic_store(&a->fault, true);
    }
}
static void playback_data(ma_device *device, void *output, const void *input, ma_uint32 count) {
    (void)input;
    BsAudio *a = device->pUserData;
    bool armed = atomic_load(&a->clock.armed);
    if (armed) {
        int64_t delay = bs_audio_clock_delay(&a->clock, device);
        if (delay == -1) {
            atomic_store(&a->fault, true);
        } else if (delay >= 0 && a->rendered > (uint64_t)delay + a->content_start) {
            uint64_t played = a->rendered - (uint64_t)delay - a->content_start;
            if (played > a->consumed) {
                played = a->consumed;
            }
            if (played > atomic_load(&a->played)) {
                atomic_store(&a->played, played);
            }
        }
    }
    /* Buffer a short lead before emitting the first bit. Once started, any gap
       before EOF invalidates the frame instead of inserting an unreported silence. */
    bool eof = atomic_load_explicit(&a->eof, memory_order_acquire);
    size_t available = bs_audio_queue_count(&a->output);
    if (!armed && (available >= BS_BLOCK * 8 || (eof && available))) {
        a->content_start = a->rendered;
        atomic_store(&a->clock.armed, true);
        armed = true;
    }
    if (armed && !atomic_load(&a->fault) && !atomic_load(&a->clock.broken)) {
        size_t total = 0;
        int16_t mono[BS_BLOCK];
        while (total < count) {
            size_t chunk = count - total < BS_BLOCK ? count - total : BS_BLOCK;
            size_t n = bs_audio_queue_read(&a->output, mono, chunk);
            if (n) {
                void *destination = (unsigned char *)output +
                                    total * ma_get_bytes_per_frame(device->playback.format,
                                                                   device->playback.channels);
                ma_uint64 frames_in = n, frames_out = n;
                if (ma_data_converter_process_pcm_frames(&a->output_converter, mono, &frames_in,
                                                         destination, &frames_out) != MA_SUCCESS ||
                    frames_in != n || frames_out != n) {
                    atomic_store(&a->fault, true);
                }
            }
            a->consumed += n;
            total += n;
            if (n < chunk) {
                /* Reload EOF after acquiring the final producer cursor. */
                if (!atomic_load_explicit(&a->eof, memory_order_acquire)) {
                    atomic_store(&a->fault, true);
                }
                break;
            }
        }
    }
    a->rendered += count;
}
static bool open_device(BsAudio *a, bool input) {
    ma_device_config config =
        ma_device_config_init(input ? ma_device_type_capture : ma_device_type_playback);
    config.sampleRate = BS_RATE;
    config.periodSizeInFrames = BS_BLOCK;
    config.periods = 3;
    config.noFixedSizedCallback = MA_TRUE;
    config.pUserData = a;
    config.notificationCallback = notification;
    config.wasapi.noAutoStreamRouting = MA_TRUE;
    config.alsa.noMMap = MA_TRUE;
    config.dataCallback = input ? capture_data : playback_data;
    config.capture.pDeviceID = a->input_default ? NULL : &a->input_id;
    config.playback.pDeviceID = a->output_default ? NULL : &a->output_id;
    config.capture.format = ma_format_s16;
    config.capture.channels = 1;
    /* Native output format avoids a converter splitting one hardware request into
       several callbacks, which would make submitted-frame timing ambiguous. */
    config.playback.format = ma_format_unknown;
    config.playback.channels = 0;
    ma_device *device = input ? &a->capture : &a->playback;
    ma_result result = ma_device_init(&a->context, &config, device);
    if (result != MA_SUCCESS) {
        snprintf(a->error, sizeof a->error, "Cannot open %s: %s", input ? "input" : "output",
                 ma_result_description(result));
        return false;
    }
    if (input) {
        a->capture_ready = true;
    } else {
        a->playback_ready = true;
        if (device->playback.internalSampleRate != BS_RATE ||
            !device->playback.converter.isPassthrough || !bs_audio_clock_init(&a->clock, device)) {
            snprintf(a->error, sizeof a->error,
                     "Output requires native 48 kHz and a readable playback clock.");
            return false;
        }
        ma_data_converter_config conversion = ma_data_converter_config_init(
            ma_format_s16, device->playback.format, 1, device->playback.channels, BS_RATE, BS_RATE);
        if (ma_data_converter_init(&conversion, NULL, &a->output_converter) != MA_SUCCESS) {
            snprintf(a->error, sizeof a->error, "Cannot initialize output conversion.");
            return false;
        }
        a->converter_ready = true;
    }
    return true;
}
static void stop_devices(BsAudio *a) {
    atomic_store(&a->closing, true);
    if (a->playback_ready) {
        ma_device_uninit(&a->playback);
        a->playback_ready = false;
    }
    if (a->converter_ready) {
        ma_data_converter_uninit(&a->output_converter, NULL);
        a->converter_ready = false;
    }
    if (a->capture_ready) {
        ma_device_uninit(&a->capture);
        a->capture_ready = false;
    }
    atomic_store(&a->closing, false);
    a->begun = false;
}
BsAudio *bs_audio_open(const char *input, const char *output, char *error, size_t capacity) {
    BsAudio *a = calloc(1, sizeof *a);
    if (!a) {
        snprintf(error, capacity, "Out of memory");
        return NULL;
    }
    atomic_init(&a->fault, false);
    atomic_init(&a->closing, false);
    atomic_init(&a->eof, false);
    atomic_init(&a->played, 0);
    atomic_init(&a->clock.broken, false);
    bs_audio_queue_init(&a->input);
    bs_audio_queue_init(&a->output);
    a->context_ready = context_open(&a->context);
    if (!a->context_ready) {
        snprintf(a->error, sizeof a->error, "No usable audio backend is available.");
    } else if (!resolve(a, true, input, &a->input_id, &a->input_default) ||
               !resolve(a, false, output, &a->output_id, &a->output_default)) {
        snprintf(a->error, sizeof a->error,
                 "Selected audio device is unavailable; select it in settings.");
    } else {
        bs_audio_reject_recovery(&a->context);
        if (open_device(a, false) && open_device(a, true)) {
            if (ma_device_start(&a->capture) == MA_SUCCESS) {
                return a;
            }
            snprintf(a->error, sizeof a->error, "Cannot start audio capture.");
        }
    }
    snprintf(error, capacity, "%s", a->error);
    bs_audio_close(a);
    return NULL;
}
void bs_audio_close(BsAudio *a) {
    if (a) {
        stop_devices(a);
        if (a->context_ready) {
            ma_context_uninit(&a->context);
        }
        free(a);
    }
}
static bool failed(BsAudio *a) {
    if (atomic_load(&a->fault) || atomic_load(&a->clock.broken)) {
        snprintf(a->error, sizeof a->error,
                 "Audio interrupted: device changed, stream stopped, or buffer over/underrun.");
        return true;
    }
    return false;
}
int bs_audio_capture(BsAudio *a, int16_t *samples, size_t count) {
    return failed(a) ? -1 : (int)bs_audio_queue_read(&a->input, samples, count);
}
int bs_audio_play(BsAudio *a, const int16_t *samples, size_t count) {
    if (failed(a) || !a->begun || atomic_load(&a->eof)) {
        return -1;
    }
    size_t written = bs_audio_queue_write(&a->output, samples, count);
    a->submitted += written;
    return (int)written;
}
bool bs_audio_begin(BsAudio *a) {
    if (failed(a) || a->begun) {
        return false;
    }
    a->begun = true;
    if (ma_device_start(&a->playback) != MA_SUCCESS) {
        snprintf(a->error, sizeof a->error, "Cannot start audio playback.");
        return false;
    }
    return true;
}
int64_t bs_audio_played(BsAudio *a) {
    return failed(a) ? -1 : (int64_t)atomic_load(&a->played);
}
int bs_audio_finish(BsAudio *a) {
    atomic_store_explicit(&a->eof, true, memory_order_release);
    if (failed(a)) {
        return -1;
    }
    return a->submitted && atomic_load(&a->played) >= a->submitted ? 1 : 0;
}
bool bs_audio_reset(BsAudio *a) {
    /* Recreate streams: stopping may pause queued audio on some miniaudio backends. */
    stop_devices(a);
    bs_audio_queue_init(&a->input);
    bs_audio_queue_init(&a->output);
    atomic_store(&a->fault, false);
    atomic_store(&a->eof, false);
    atomic_store(&a->played, 0);
    a->submitted = a->rendered = a->content_start = a->consumed = 0;
    if (!open_device(a, false) || !open_device(a, true) ||
        ma_device_start(&a->capture) != MA_SUCCESS) {
        snprintf(a->error, sizeof a->error, "Cannot reopen audio after transmission.");
        return false;
    }
    return true;
}
const char *bs_audio_error(BsAudio *a) {
    return a->error;
}
