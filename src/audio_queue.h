#ifndef BS_AUDIO_QUEUE_H
#define BS_AUDIO_QUEUE_H
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#define BS_AUDIO_CAPACITY 32768u
/* One producer and one consumer own opposite cursors. Unsigned subtraction also
   works across cursor wrap; the distance never exceeds this power-of-two ring. */
typedef struct {
    int16_t samples[BS_AUDIO_CAPACITY];
    atomic_uint read, write;
} BsAudioQueue;
void bs_audio_queue_init(BsAudioQueue *queue);
size_t bs_audio_queue_count(BsAudioQueue *queue);
size_t bs_audio_queue_write(BsAudioQueue *queue, const int16_t *samples, size_t count);
size_t bs_audio_queue_read(BsAudioQueue *queue, int16_t *samples, size_t count);
#endif
