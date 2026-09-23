#ifndef BS_MODEM_H
#define BS_MODEM_H
#include "protocol.h"
#define BS_RATE 48000
#define BS_DEFAULT_BIT_RATE 10
#define BS_PROFILE_COUNT 3
#define BS_WINDOW 240
#define BS_HOP 48
#define BS_BLOCK 480
extern const unsigned bs_bit_rates[BS_PROFILE_COUNT];
bool bs_bit_rate_valid(unsigned bit_rate);
unsigned bs_symbol_samples(unsigned bit_rate);
double bs_frame_duration(const BsFrame *frame, unsigned bit_rate);

typedef struct {
    const BsFrame *frame;
    uint64_t sample;
    unsigned symbol_samples;
    double phase, amplitude;
} BsTransmitter;
typedef struct BsReceiver BsReceiver;
typedef struct {
    BsReceiver *receiver;
    BsParser candidate;
    unsigned bit_rate, missing;
    double period, boundary, last_transition, anchor_edge, tracked_symbols, integrated[2];
    int previous_tone, last_bit;
    bool running, decided, uncertain;
} BsClock;
struct BsReceiver {
    BsParser *parser;
    BsClock clocks[BS_PROFILE_COUNT];
    int selected, preview;
    double cosine[2][BS_WINDOW], sine[2][BS_WINDOW];
    double history[BS_WINDOW], dc, power, period;
    uint64_t samples;
    unsigned history_position, hop, clipped, bit_rate;
    float level, confidence;
};
/* Invalid rates fail before any waveform can be generated. */
bool bs_tx_init(BsTransmitter *tx, const BsFrame *frame, double amplitude, unsigned bit_rate);
size_t bs_tx_read(BsTransmitter *tx, int16_t *samples, size_t capacity);
void bs_rx_init(BsReceiver *rx, BsParser *parser);
void bs_rx_read(BsReceiver *rx, const int16_t *samples, size_t count);
#endif
