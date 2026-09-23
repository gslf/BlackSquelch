#include "audio_queue.h"
#include <string.h>
void bs_audio_queue_init(BsAudioQueue *queue) {
    atomic_init(&queue->read, 0);
    atomic_init(&queue->write, 0);
}
size_t bs_audio_queue_count(BsAudioQueue *queue) {
    unsigned read = atomic_load_explicit(&queue->read, memory_order_acquire);
    return atomic_load_explicit(&queue->write, memory_order_acquire) - read;
}
size_t bs_audio_queue_write(BsAudioQueue *queue, const int16_t *samples, size_t count) {
    unsigned write = atomic_load_explicit(&queue->write, memory_order_relaxed);
    unsigned read = atomic_load_explicit(&queue->read, memory_order_acquire);
    size_t room = BS_AUDIO_CAPACITY - (write - read);
    if (count > room) {
        count = room;
    }
    size_t offset = write % BS_AUDIO_CAPACITY;
    size_t first = count < BS_AUDIO_CAPACITY - offset ? count : BS_AUDIO_CAPACITY - offset;
    memcpy(queue->samples + offset, samples, first * sizeof *samples);
    memcpy(queue->samples, samples + first, (count - first) * sizeof *samples);
    atomic_store_explicit(&queue->write, write + (unsigned)count, memory_order_release);
    return count;
}
size_t bs_audio_queue_read(BsAudioQueue *queue, int16_t *samples, size_t count) {
    unsigned read = atomic_load_explicit(&queue->read, memory_order_relaxed);
    unsigned write = atomic_load_explicit(&queue->write, memory_order_acquire);
    if (count > write - read) {
        count = write - read;
    }
    size_t offset = read % BS_AUDIO_CAPACITY;
    size_t first = count < BS_AUDIO_CAPACITY - offset ? count : BS_AUDIO_CAPACITY - offset;
    memcpy(samples, queue->samples + offset, first * sizeof *samples);
    memcpy(samples + first, queue->samples, (count - first) * sizeof *samples);
    atomic_store_explicit(&queue->read, read + (unsigned)count, memory_order_release);
    return count;
}
