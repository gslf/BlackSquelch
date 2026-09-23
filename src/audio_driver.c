#define MINIAUDIO_IMPLEMENTATION
#include "audio_driver.h"
#include <math.h>

#if defined(MA_SUPPORT_ALSA) && defined(MA_ENABLE_ALSA)
static int reject_recovery(ma_snd_pcm_t *pcm, int error, int silent) {
    (void)pcm;
    (void)silent;
    /* Recovering an XRUN would hide missing modem samples behind a healthy stream. */
    return error;
}
#endif
void bs_audio_reject_recovery(ma_context *context) {
    (void)context;
#if defined(MA_SUPPORT_ALSA) && defined(MA_ENABLE_ALSA)
    if (context->backend == ma_backend_alsa) {
        context->alsa.snd_pcm_recover = (ma_proc)reject_recovery;
    }
#endif
}
#if defined(MA_SUPPORT_PULSEAUDIO) && defined(MA_ENABLE_PULSEAUDIO)
static void timing_updated(ma_pa_stream *stream, int success, void *user) {
    (void)stream;
    (void)success;
    ((BsAudioClock *)user)->pending = false;
}
static void pulse_underflow(ma_pa_stream *stream, void *user) {
    (void)stream;
    BsAudioClock *clock = user;
    if (atomic_load(&clock->armed)) {
        atomic_store(&clock->broken, true);
    }
}
#endif
#if defined(MA_SUPPORT_COREAUDIO) && defined(MA_ENABLE_COREAUDIO)
static bool core_property(ma_device *device, AudioObjectID object,
                          AudioObjectPropertySelector selector, UInt32 *value) {
    AudioObjectPropertyAddress address = {selector, kAudioObjectPropertyScopeOutput,
                                          kAudioObjectPropertyElementMain};
    UInt32 size = sizeof *value;
    return ((ma_AudioObjectGetPropertyData_proc)device->pContext->coreaudio
                .AudioObjectGetPropertyData)(object, &address, 0, NULL, &size, value) == noErr;
}
static bool core_delay(ma_device *device, uint64_t *frames) {
    UInt32 latency, safety, buffer;
    AudioObjectID id = device->coreaudio.deviceObjectIDPlayback;
    if (!core_property(device, id, kAudioDevicePropertyLatency, &latency) ||
        !core_property(device, id, kAudioDevicePropertySafetyOffset, &safety) ||
        !core_property(device, id, kAudioDevicePropertyBufferFrameSize, &buffer)) {
        return false;
    }
    AudioObjectPropertyAddress address = {kAudioDevicePropertyStreams,
                                          kAudioObjectPropertyScopeOutput,
                                          kAudioObjectPropertyElementMain};
    AudioStreamID streams[64];
    UInt32 size = sizeof streams, maximum = 0;
    if (((ma_AudioObjectGetPropertyData_proc)device->pContext->coreaudio
             .AudioObjectGetPropertyData)(id, &address, 0, NULL, &size, streams) != noErr ||
        size > sizeof streams) {
        return false;
    }
    for (size_t i = 0; i < size / sizeof streams[0]; ++i) {
        UInt32 stream_latency;
        /* Stream latency is a global property, unlike device output latency. */
        AudioObjectPropertyAddress stream_address = {kAudioStreamPropertyLatency,
                                                     kAudioObjectPropertyScopeGlobal,
                                                     kAudioObjectPropertyElementMain};
        UInt32 bytes = sizeof stream_latency;
        if (((ma_AudioObjectGetPropertyData_proc)
                 device->pContext->coreaudio.AudioObjectGetPropertyData)(
                streams[i], &stream_address, 0, NULL, &bytes, &stream_latency) != noErr) {
            return false;
        }
        if (stream_latency > maximum) {
            maximum = stream_latency;
        }
    }
    Float64 rate;
    address.mSelector = kAudioDevicePropertyNominalSampleRate;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    size = sizeof rate;
    if (((ma_AudioObjectGetPropertyData_proc)device->pContext->coreaudio
             .AudioObjectGetPropertyData)(id, &address, 0, NULL, &size, &rate) != noErr ||
        rate <= 0) {
        return false;
    }
    Float64 unit_latency;
    size = sizeof unit_latency;
    if (((ma_AudioUnitGetProperty_proc)device->pContext->coreaudio.AudioUnitGetProperty)(
            device->coreaudio.audioUnitPlayback, kAudioUnitProperty_Latency, kAudioUnitScope_Global,
            0, &unit_latency, &size) != noErr ||
        unit_latency < 0) {
        return false;
    }
    *frames = (uint64_t)ceil(((latency + (double)safety + buffer + maximum) / rate + unit_latency) *
                             device->sampleRate);
    return true;
}
#endif
bool bs_audio_clock_init(BsAudioClock *clock, ma_device *device) {
    memset(clock, 0, sizeof *clock);
    atomic_init(&clock->armed, false);
    atomic_init(&clock->broken, false);
    (void)device;
#if defined(MA_SUPPORT_ALSA) && defined(MA_ENABLE_ALSA)
    if (device->pContext->backend == ma_backend_alsa) {
        clock->delay =
            ma_dlsym(ma_device_get_log(device), device->pContext->alsa.asoundSO, "snd_pcm_delay");
        return clock->delay != NULL;
    }
#endif
#if defined(MA_SUPPORT_PULSEAUDIO) && defined(MA_ENABLE_PULSEAUDIO)
    if (device->pContext->backend == ma_backend_pulseaudio) {
        ma_handle library = device->pContext->pulse.pulseSO;
        clock->delay = ma_dlsym(ma_device_get_log(device), library, "pa_stream_get_latency");
        clock->update =
            ma_dlsym(ma_device_get_log(device), library, "pa_stream_update_timing_info");
        clock->release = ma_dlsym(ma_device_get_log(device), library, "pa_operation_unref");
        ma_proc set_underflow =
            ma_dlsym(ma_device_get_log(device), library, "pa_stream_set_underflow_callback");
        if (!clock->delay || !clock->update || !clock->release || !set_underflow) {
            return false;
        }
        ((void (*)(ma_pa_stream *, ma_pa_stream_notify_cb_t, void *))set_underflow)(
            device->pulse.pStreamPlayback, pulse_underflow, clock);
        return true;
    }
#endif
#if defined(MA_SUPPORT_WASAPI) && defined(MA_ENABLE_WASAPI)
    if (device->pContext->backend == ma_backend_wasapi) {
        MA_REFERENCE_TIME latency;
        if (FAILED(
                ma_IAudioClient_GetStreamLatency(device->wasapi.pAudioClientPlayback, &latency))) {
            return false;
        }
        clock->fixed_delay = (uint64_t)ceil((double)latency * device->sampleRate / 10000000.0);
        return true;
    }
#endif
#if defined(MA_SUPPORT_COREAUDIO) && defined(MA_ENABLE_COREAUDIO)
    if (device->pContext->backend == ma_backend_coreaudio) {
        return core_delay(device, &clock->fixed_delay);
    }
#endif
#ifdef BS_AUDIO_TEST
    if (device->pContext->backend == ma_backend_null) {
        clock->fixed_delay = device->playback.internalPeriodSizeInFrames;
        return true;
    }
#endif
    return false;
}
int64_t bs_audio_clock_delay(BsAudioClock *clock, ma_device *device) {
    (void)clock;
    (void)device;
#if defined(MA_SUPPORT_ALSA) && defined(MA_ENABLE_ALSA)
    if (device->pContext->backend == ma_backend_alsa) {
        ma_snd_pcm_sframes_t delay = 0;
        int result = ((int (*)(ma_snd_pcm_t *, ma_snd_pcm_sframes_t *))clock->delay)(
            device->alsa.pPCMPlayback, &delay);
        if (result < 0 || delay < 0) {
            return -1;
        }
        return delay;
    }
#endif
#if defined(MA_SUPPORT_PULSEAUDIO) && defined(MA_ENABLE_PULSEAUDIO)
    if (device->pContext->backend == ma_backend_pulseaudio) {
        /* Without interpolation, stale timing overestimates pending playback. Request
           fresh timing asynchronously; never block the PulseAudio mainloop callback. */
        if (!clock->pending) {
            clock->pending = true;
            ma_pa_operation *operation =
                ((ma_pa_operation * (*)(ma_pa_stream *, ma_pa_stream_success_cb_t, void *))
                     clock->update)(device->pulse.pStreamPlayback, timing_updated, clock);
            if (operation) {
                ((ma_pa_operation_unref_proc)clock->release)(operation);
            } else {
                clock->pending = false;
            }
        }
        uint64_t usec;
        int negative;
        if (((int (*)(const ma_pa_stream *, uint64_t *, int *))clock->delay)(
                device->pulse.pStreamPlayback, &usec, &negative) < 0) {
            return -2; /* Initial timing is asynchronous; the controller has a watchdog. */
        }
        return negative ? -1 : (int64_t)ceil((double)usec * device->sampleRate / 1000000.0);
    }
#endif
#if defined(MA_SUPPORT_WASAPI) && defined(MA_ENABLE_WASAPI)
    if (device->pContext->backend == ma_backend_wasapi) {
        ma_uint32 padding;
        if (FAILED(
                ma_IAudioClient_GetCurrentPadding(device->wasapi.pAudioClientPlayback, &padding))) {
            return -1;
        }
        return (int64_t)(padding + clock->fixed_delay);
    }
#endif
#if defined(MA_ENABLE_COREAUDIO) || defined(BS_AUDIO_TEST)
    return (int64_t)clock->fixed_delay;
#else
    return -1;
#endif
}
