#ifndef BS_MFSK_H
#define BS_MFSK_H
#include "fec.h"
#define BS_MFSK_BAUD 25
#define BS_MFSK_SYMBOL_SAMPLES 1920
#define BS_MFSK_TRAINING 32
#define BS_MFSK_SYMBOL_MAX 2600
#define BS_MFSK_HISTORY 1360
#define BS_MFSK_WINDOW 256
#define BS_MFSK_FIR 97
/* DSP runs at 8 kHz, analysis every millisecond, with a 32 ms matched window. */
typedef struct {
    double osc_re[16], osc_im[16], step_re[16], step_im[16];
    float mixed_re[16][BS_MFSK_WINDOW], mixed_im[16][BS_MFSK_WINDOW];
    double sum_re[16], sum_im[16], half_re[16], half_im[16];
    float energy[BS_MFSK_HISTORY][16], offset[BS_MFSK_HISTORY];
    uint8_t winner[2][BS_MFSK_HISTORY];
    float gain[16];
    float fir[BS_MFSK_FIR], fir_taps[BS_MFSK_FIR], confidence;
    double period, next_symbol, candidate_score, anchor_edge;
    uint64_t tick, candidate_started, candidate_time, anchor_symbol;
    unsigned decimation, position, hop, tones, candidate_tones, quiet, fir_position;
    size_t stream_position, coded_position, frame_bits;
    bool locked, payload, have_anchor;
    float frequency_offset;
    int8_t coded[BS_FEC_CODED_MAX];
} BsMfskReceiver;
bool bs_tones_valid(unsigned tones);
unsigned
bs_tones_effective(unsigned tones); /* Zero-initialized settings select the default 8 tones. */
unsigned bs_mfsk_training(unsigned tones, unsigned index);
double bs_mfsk_frequency(unsigned tones, unsigned tone);
size_t bs_mfsk_symbol_count(size_t bits, unsigned tones);
bool bs_mfsk_encode(const BsFrame *frame, unsigned tones, uint8_t *symbols, size_t *count);
void bs_mfsk_rx_init(BsMfskReceiver *receiver);
void bs_mfsk_rx_sample(BsMfskReceiver *receiver, BsParser *parser, float sample);
void bs_mfsk_rx_abort(BsMfskReceiver *receiver, BsParser *parser, const char *reason);
#endif
