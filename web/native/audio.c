/* A bounded, nonblocking browser audio adapter. All calls run on the modem worker. */
#include "web_audio.h"
#include "audio_queue.h"
#include "platform.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct BsAudio {
    BsAudioQueue capture, playback;
    uint64_t submitted, pulled, played;
    bool active, ending;
    double drained_at;
    char error[256];
};
static BsAudio *current;
static bool ready;
static double output_latency = 0.15;
static unsigned generation;
void bs_web_audio_ready(int value, double latency) {
    ready = value != 0;
    output_latency = latency >= 0.02 && latency < 2 ? latency : 0.15;
}
size_t bs_audio_devices(bool input, BsDevice *devices, size_t capacity) {
    if (!capacity) return 0;
    strcpy(devices[0].id, "default");
    strcpy(devices[0].name, input ? "Browser microphone" : "Browser speaker");
    return 1;
}
BsAudio *bs_audio_open(const char *input, const char *output, char *error, size_t capacity) {
    (void)input; (void)output;
    if (!ready) {
        snprintf(error, capacity, "Choose audio devices, then tap Start audio.");
        return NULL;
    }
    current = calloc(1, sizeof *current);
    if (!current) snprintf(error, capacity, "Not enough memory for audio.");
    else { bs_audio_queue_init(&current->capture); bs_audio_queue_init(&current->playback); }
    return current;
}
void bs_audio_close(BsAudio *audio) {
    if (audio == current) { current = NULL; ++generation; }
    free(audio);
}
int bs_audio_capture(BsAudio *audio, int16_t *samples, size_t capacity) {
    return *audio->error ? -1 : (int)bs_audio_queue_read(&audio->capture, samples, capacity);
}
int bs_audio_play(BsAudio *audio, const int16_t *samples, size_t count) {
    if (*audio->error) return -1;
    size_t written = bs_audio_queue_write(&audio->playback, samples, count);
    audio->submitted += written;
    return (int)written;
}
bool bs_audio_reset(BsAudio *audio) {
    bs_audio_queue_init(&audio->capture);
    bs_audio_queue_init(&audio->playback);
    audio->submitted = audio->pulled = audio->played = 0;
    audio->active = audio->ending = false;
    audio->drained_at = 0;
    ++generation;
    return !*audio->error;
}
bool bs_audio_begin(BsAudio *audio) {
    if (!bs_audio_reset(audio)) return false;
    audio->active = true;
    return true;
}
int bs_audio_finish(BsAudio *audio) {
    if (*audio->error) return -1;
    audio->ending = true;
    if (audio->played < audio->submitted) return 0;
    if (!audio->drained_at) audio->drained_at = bs_time();
    return bs_time() - audio->drained_at >= output_latency;
}
int64_t bs_audio_played(BsAudio *audio) { return *audio->error ? -1 : (int64_t)audio->played; }
const char *bs_audio_error(BsAudio *audio) { return audio->error; }
void bs_web_audio_fault(const char *message) {
    if (current) snprintf(current->error, sizeof current->error, "%s", message);
}
int bs_web_capture(const int16_t *samples, int count) {
    if (!current || count <= 0 || count > 8192) return 0;
    /* The shared controller discards loopback while keeping its capture watchdog live. */
    size_t written = bs_audio_queue_write(&current->capture, samples, (size_t)count);
    if (written != (size_t)count) bs_web_audio_fault("Capture buffer overflow; reconnect audio.");
    return (int)written;
}
int bs_web_output(int16_t *samples, int capacity) {
    if (!current || !current->active || capacity <= 0 || capacity > 8192) return 0;
    /* Include samples already sent to the worklet in the bound on outstanding audio. */
    uint64_t pending = current->pulled - current->played;
    size_t room = pending < 5760 ? (size_t)(5760 - pending) : 0;
    if ((size_t)capacity > room) capacity = (int)room;
    size_t count = bs_audio_queue_read(&current->playback, samples, (size_t)capacity);
    current->pulled += count;
    return (int)count;
}
void bs_web_ack(unsigned epoch, double played) {
    if (current && generation == epoch && isfinite(played) && played >= 0 &&
        played >= (double)current->played && played <= (double)current->pulled)
        current->played = (uint64_t)played;
}
unsigned bs_web_generation(void) { return generation; }
double bs_web_submitted(void) { return current ? (double)current->submitted : 0; }
int bs_web_transmitting(void) { return current && current->active; }
int bs_web_ending(void) { return current && current->ending && current->pulled == current->submitted; }
