#ifndef BS_AUDIO_H
#define BS_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define BS_DEVICE_LIMIT 64
#define BS_DEVICE_ID 256
typedef struct {
    char id[BS_DEVICE_ID], name[128];
} BsDevice;
typedef struct BsAudio BsAudio;
size_t bs_audio_devices(bool input, BsDevice *devices, size_t capacity);
BsAudio *bs_audio_open(const char *input, const char *output, char *error, size_t capacity);
void bs_audio_close(BsAudio *audio);
/* Nonblocking operations: zero means try again; negative means the stream is invalid. */
int bs_audio_capture(BsAudio *audio, int16_t *samples, size_t capacity);
int bs_audio_play(BsAudio *audio, const int16_t *samples, size_t count);
bool bs_audio_begin(BsAudio *audio);
/* Finish is polled until hardware playback has ended; progress counts played samples. */
int bs_audio_finish(BsAudio *audio);
int64_t bs_audio_played(BsAudio *audio);
bool bs_audio_reset(BsAudio *audio);
const char *bs_audio_error(BsAudio *audio);
#endif
