#ifndef BS_MODEM_H
#define BS_MODEM_H
#include "protocol.h"
#include "mfsk.h"
#define BS_RATE 48000
#define BS_BLOCK 480
#define BS_DEFAULT_TONES 8
uint64_t bs_frame_samples(const BsFrame *frame, unsigned tones);
double bs_frame_duration(const BsFrame *frame, unsigned tones);
typedef struct {
    const BsFrame *frame;
    uint64_t sample;
    double phase, amplitude;
    unsigned tones;
    uint8_t symbols[BS_MFSK_SYMBOL_MAX];
    size_t symbol_count;
} BsTransmitter;
typedef struct {
    BsMfskReceiver mfsk;
    BsParser *parser;
    double dc, power;
    unsigned clipped, tones;
    float level, confidence;
} BsReceiver;
bool bs_tx_init(BsTransmitter *tx, const BsFrame *frame, double amplitude, unsigned tones);
size_t bs_tx_read(BsTransmitter *tx, int16_t *samples, size_t capacity);
void bs_rx_init(BsReceiver *rx, BsParser *parser);
void bs_rx_read(BsReceiver *rx, const int16_t *samples, size_t count);
bool bs_rx_active(const BsReceiver *rx);
void bs_rx_abort(BsReceiver *rx, const char *reason);
#endif
