#ifndef BS_WEB_AUDIO_H
#define BS_WEB_AUDIO_H
#include "audio.h"
void bs_web_audio_ready(int ready, double latency);
int bs_web_capture(const int16_t *samples, int count);
int bs_web_output(int16_t *samples, int capacity);
void bs_web_ack(unsigned generation, double played);
unsigned bs_web_generation(void);
double bs_web_submitted(void);
int bs_web_transmitting(void);
int bs_web_ending(void);
void bs_web_audio_fault(const char *message);
#endif
