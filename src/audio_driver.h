#ifndef BS_AUDIO_DRIVER_H
#define BS_AUDIO_DRIVER_H
/* Keep the vendor feature set identical in every translation unit. */
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#ifdef BS_AUDIO_TEST
#define MA_ENABLE_NULL
#elif defined(_WIN32)
#define MA_ENABLE_WASAPI
#elif defined(__APPLE__)
#define MA_ENABLE_COREAUDIO
#else
#define MA_ENABLE_PULSEAUDIO
#define MA_ENABLE_ALSA
#endif
#include <miniaudio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    ma_proc delay, update, release;
    uint64_t fixed_delay;
    bool pending;
    atomic_bool armed, broken;
} BsAudioClock;
/* The only native bridge: miniaudio has no portable hardware-drain API. These
   functions run on the device thread, except initialization before start. */
bool bs_audio_clock_init(BsAudioClock *clock, ma_device *device);
int64_t bs_audio_clock_delay(BsAudioClock *clock, ma_device *device);
void bs_audio_reject_recovery(ma_context *context);
#endif
