#ifndef BS_WAV_H
#define BS_WAV_H
#include "modem.h"
typedef bool (*BsFrameSource)(void *context, size_t index, BsFrame *frame);
bool bs_wav_write_frames(const char *path, size_t count, BsFrameSource source, void *context,
                         double amplitude, unsigned tones);
bool bs_wav_write(const char *path, const BsFrame *frame, double amplitude, unsigned tones);
bool bs_wav_decode(const char *path, BsReceiver *receiver, char *error, size_t capacity);
#endif
