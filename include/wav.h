#ifndef BS_WAV_H
#define BS_WAV_H
#include "modem.h"
bool bs_wav_write(const char *path, const BsFrame *frame, double amplitude, unsigned bit_rate);
bool bs_wav_decode(const char *path, BsReceiver *receiver, char *error, size_t capacity);
#endif
