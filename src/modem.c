#include "modem.h"
#include <math.h>
#include <string.h>
#define TAU 6.28318530717958647693
uint64_t bs_frame_samples(const BsFrame *frame, unsigned tones) {
    if (!bs_tones_valid(tones) || !frame->count || frame->count > BS_MAX_BITS) {
        return 0;
    }
    return bs_mfsk_symbol_count(frame->count, tones) * (uint64_t)BS_MFSK_SYMBOL_SAMPLES;
}
double bs_frame_duration(const BsFrame *frame, unsigned tones) {
    return (double)bs_frame_samples(frame, tones) / BS_RATE;
}
bool bs_tx_init(BsTransmitter *tx, const BsFrame *frame, double amplitude, unsigned tones) {
    memset(tx, 0, sizeof *tx);
    if (!bs_frame_samples(frame, tones) || !isfinite(amplitude) ||
        !bs_mfsk_encode(frame, tones, tx->symbols, &tx->symbol_count)) {
        return false;
    }
    tx->frame = frame;
    tx->tones = tones;
    tx->amplitude = amplitude < 0 ? 0 : amplitude > 1 ? 1 : amplitude;
    return true;
}
size_t bs_tx_read(BsTransmitter *tx, int16_t *samples, size_t capacity) {
    uint64_t total = tx->symbol_count * (uint64_t)BS_MFSK_SYMBOL_SAMPLES;
    size_t n = 0;
    while (n < capacity && tx->sample < total) {
        size_t symbol = (size_t)(tx->sample / BS_MFSK_SYMBOL_SAMPLES);
        double frequency = bs_mfsk_frequency(tx->tones, tx->symbols[symbol]);
        double ramp =
            fmin(1.0, fmin((double)tx->sample / 240.0, (double)(total - 1 - tx->sample) / 240.0));
        samples[n++] = (int16_t)lrint(sin(tx->phase) * 32767 * tx->amplitude * ramp);
        tx->phase += TAU * frequency / BS_RATE;
        if (tx->phase >= TAU) {
            tx->phase -= TAU;
        }
        ++tx->sample;
    }
    return n;
}
void bs_rx_init(BsReceiver *rx, BsParser *parser) {
    memset(rx, 0, sizeof *rx);
    rx->parser = parser;
    bs_mfsk_rx_init(&rx->mfsk);
}
void bs_rx_read(BsReceiver *rx, const int16_t *samples, size_t count) {
    rx->clipped = 0;
    for (size_t i = 0; i < count; ++i) {
        double x = samples[i] / 32768.0;
        rx->clipped += samples[i] >= 32760 || samples[i] <= -32760;
        rx->dc += 0.001 * (x - rx->dc);
        x -= rx->dc;
        rx->power += 0.001 * (x * x - rx->power);
        bs_mfsk_rx_sample(&rx->mfsk, rx->parser, (float)x);
        if (rx->mfsk.locked) {
            rx->tones = rx->mfsk.tones;
        }
    }
    rx->level = (float)sqrt(fmax(0, rx->power));
    rx->confidence = rx->mfsk.confidence;
}
bool bs_rx_active(const BsReceiver *rx) {
    return rx->mfsk.locked || rx->mfsk.candidate_started ||
           (rx->parser && bs_parser_active(rx->parser));
}
void bs_rx_abort(BsReceiver *rx, const char *reason) {
    bs_mfsk_rx_abort(&rx->mfsk, rx->parser, reason);
    bs_parser_abort(rx->parser, reason);
    bs_rx_init(rx, rx->parser);
}
