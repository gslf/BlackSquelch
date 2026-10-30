#include "mfsk.h"
#include <math.h>
#include <string.h>
#define TAU 6.28318530717958647693
static const unsigned training[32] = {0, 15, 3, 12, 6, 9,  5, 10, 1, 14, 7, 8,  2, 13, 4, 11,
                                      5, 12, 1, 10, 7, 14, 3, 8,  4, 15, 6, 13, 0, 11, 2, 9};
bool bs_tones_valid(unsigned tones) {
    return tones == 8 || tones == 16;
}
unsigned bs_tones_effective(unsigned tones) {
    return tones ? tones : 8;
}
unsigned bs_mfsk_training(unsigned tones, unsigned index) {
    return training[index % BS_MFSK_TRAINING] % tones;
}
double bs_mfsk_frequency(unsigned tones, unsigned tone) {
    return tones == 8 ? 900 + 200 * tone : 800 + 100 * tone;
}
static unsigned band(unsigned tones, unsigned tone) {
    return tones == 8 ? tone * 2 + 1 : tone;
}
static unsigned gray(unsigned tone) {
    return tone ^ (tone >> 1);
}
static unsigned ungray(unsigned value) {
    unsigned tone = value;
    while ((value >>= 1)) {
        tone ^= value;
    }
    return tone;
}
static void whiten(uint8_t *bits, size_t count) {
    unsigned state = 0x1ff;
    for (size_t i = 0; i < count; ++i) {
        bits[i] ^= state & 1;
        state = (state >> 1) | (((state ^ (state >> 4)) & 1) << 8);
    }
}
size_t bs_mfsk_symbol_count(size_t bits, unsigned tones) {
    if ((tones != 8 && tones != 16) || bits < 136 || bits > BS_MAX_BITS || bits % 8) {
        return 0;
    }
    size_t data = (96 + bs_fec_size(bits - 64)) / (tones == 8 ? 3 : 4);
    return BS_MFSK_TRAINING + data + 2 * ((data - 1) / 16);
}
static void append(const uint8_t *coded, size_t bits, unsigned tones, uint8_t *symbols,
                   size_t *count, size_t *data_symbols) {
    unsigned width = tones == 8 ? 3 : 4;
    for (size_t i = 0; i < bits; i += width) {
        if (*data_symbols && *data_symbols % 16 == 0) {
            symbols[(*count)++] = 0;
            symbols[(*count)++] = (uint8_t)(tones - 1);
        }
        unsigned value = 0;
        for (unsigned j = 0; j < width; ++j) {
            value = (value << 1) | coded[i + j];
        }
        symbols[(*count)++] = (uint8_t)ungray(value);
        ++*data_symbols;
    }
}
bool bs_mfsk_encode(const BsFrame *frame, unsigned tones, uint8_t *symbols, size_t *count) {
    size_t expected = bs_mfsk_symbol_count(frame->count, tones);
    if (!expected || expected > BS_MFSK_SYMBOL_MAX || frame->count < 136) {
        return false;
    }
    for (size_t i = 0; i < frame->count; ++i) {
        unsigned prefix_bit = i < 32   ? i % 2 == 0
                              : i < 64 ? (BS_SYNC >> (63 - i)) & 1
                                       : frame->bits[i];
        if (frame->bits[i] > 1 || frame->bits[i] != prefix_bit) {
            return false;
        }
    }
    uint8_t header_bytes[5] = {1, (uint8_t)(frame->count >> 8), (uint8_t)frame->count, 0, 0};
    uint16_t crc = bs_crc16(header_bytes, 3);
    header_bytes[3] = (uint8_t)(crc >> 8);
    header_bytes[4] = (uint8_t)crc;
    uint8_t header[40], coded[BS_FEC_CODED_MAX], payload[BS_MAX_BITS];
    for (unsigned i = 0; i < 40; ++i) {
        header[i] = (header_bytes[i / 8] >> (7 - i % 8)) & 1;
    }
    *count = 0;
    for (unsigned i = 0; i < BS_MFSK_TRAINING; ++i) {
        symbols[(*count)++] = (uint8_t)bs_mfsk_training(tones, i);
    }
    size_t data_symbols = 0;
    bs_fec_encode(header, 40, coded, sizeof coded);
    append(coded, 96, tones, symbols, count, &data_symbols);
    memcpy(payload, frame->bits + 64, frame->count - 64);
    whiten(payload, frame->count - 64);
    bs_fec_encode(payload, frame->count - 64, coded, sizeof coded);
    append(coded, bs_fec_size(frame->count - 64), tones, symbols, count, &data_symbols);
    return *count == expected;
}
static void tune(BsMfskReceiver *rx, float offset) {
    rx->frequency_offset = offset;
    for (unsigned i = 0; i < 16; ++i) {
        double phase = TAU * (800 + 100 * i + offset) / 8000;
        rx->step_re[i] = cos(phase);
        rx->step_im[i] = sin(phase);
    }
}
void bs_mfsk_rx_init(BsMfskReceiver *rx) {
    memset(rx, 0, sizeof *rx);
    for (unsigned i = 0; i < 16; ++i) {
        rx->osc_re[i] = 1;
        rx->gain[i] = 1;
    }
    double sum = 0;
    for (unsigned i = 0; i < BS_MFSK_FIR; ++i) {
        int k = (int)i - (BS_MFSK_FIR - 1) / 2;
        double sinc = k ? sin(TAU * 3000 * k / 48000) / (TAU * k / 2) : 2.0 * 3000 / 48000;
        double tap = sinc * (0.54 - 0.46 * cos(TAU * i / (BS_MFSK_FIR - 1)));
        rx->fir_taps[i] = (float)tap;
        sum += tap;
    }
    for (unsigned i = 0; i < BS_MFSK_FIR; ++i) {
        rx->fir_taps[i] = (float)(rx->fir_taps[i] / sum);
    }
    rx->period = 40;
    tune(rx, 0);
}
static void notify(BsParser *p, BsEventKind kind, unsigned tones, const char *detail) {
    if (p->event) {
        BsEvent event = {0};
        event.kind = kind;
        event.tones = tones;
        event.symbol_ms = 40;
        event.detail = detail;
        p->event(p->context, &event);
    }
}
void bs_mfsk_rx_abort(BsMfskReceiver *rx, BsParser *p, const char *reason) {
    if (rx->locked) {
        notify(p, BS_INTERRUPTED, rx->tones, reason);
    }
    bs_mfsk_rx_init(rx);
}
static void fail(BsMfskReceiver *rx, BsParser *p, const char *reason) {
    notify(p, BS_INVALID, rx->tones, reason);
    bs_mfsk_rx_init(rx);
}
static void acquire(BsMfskReceiver *rx, BsParser *p) {
    rx->tones = rx->candidate_tones;
    rx->locked = true;
    rx->payload = false;
    rx->stream_position = rx->coded_position = 0;
    rx->period = 40;
    rx->next_symbol = (double)rx->candidate_time + rx->period;
    rx->have_anchor = false;
    float sums[16] = {0};
    unsigned counts[16] = {0};
    double frequency = 0, weight = 0;
    for (unsigned i = 0; i < BS_MFSK_TRAINING; ++i) {
        uint64_t time = rx->candidate_time - (31 - i) * 40;
        unsigned b = band(rx->tones, bs_mfsk_training(rx->tones, i));
        float energy = rx->energy[time % BS_MFSK_HISTORY][b];
        sums[b] += energy;
        ++counts[b];
        frequency += rx->offset[time % BS_MFSK_HISTORY] * energy;
        weight += energy;
    }
    float average = 0;
    for (unsigned i = 0; i < rx->tones; ++i) {
        unsigned b = band(rx->tones, i);
        rx->gain[b] = sums[b] / (float)counts[b];
        average += rx->gain[b] / rx->tones;
    }
    for (unsigned i = 0; i < rx->tones; ++i) {
        unsigned b = band(rx->tones, i);
        rx->gain[b] = fmaxf(average * 0.08f, rx->gain[b]);
    }
    tune(rx, (float)fmax(-20, fmin(20, frequency / (weight + 1e-12))));
    bs_parser_init(p, p->event, p->context);
    p->tones = rx->tones;
    notify(p, BS_MODE_DETECTED, rx->tones, "MFSK training acquired; FEC and interleaving active");
}
static void search(BsMfskReceiver *rx, BsParser *p) {
    if (rx->tick < 31 * 40 + 32) {
        return;
    }
    for (unsigned mode = 0; mode < 2; ++mode) {
        unsigned tones = mode ? 16 : 8;
        if (rx->winner[mode][rx->tick % BS_MFSK_HISTORY] != bs_mfsk_training(tones, 31)) {
            continue;
        }
        double score = 0;
        unsigned matches = 0;
        for (unsigned i = 0; i < 32; ++i) {
            size_t t = (rx->tick - (31 - i) * 40) % BS_MFSK_HISTORY;
            unsigned tone = bs_mfsk_training(tones, i);
            matches += rx->winner[mode][t] == tone;
            float total = 0;
            for (unsigned j = 0; j < tones; ++j) {
                total += rx->energy[t][band(tones, j)];
            }
            score += rx->energy[t][band(tones, tone)] / (total + 1e-9f);
        }
        score /= 32;
        if (matches >= 28 && score > 0.70 &&
            (!rx->candidate_started || score > rx->candidate_score)) {
            if (!rx->candidate_started) {
                rx->candidate_started = rx->tick;
            }
            rx->candidate_score = score;
            rx->candidate_time = rx->tick;
            rx->candidate_tones = tones;
        }
    }
    if (rx->candidate_started && rx->tick >= rx->candidate_started + 24) {
        acquire(rx, p);
    }
}
static void pilot_clock(BsMfskReceiver *rx) {
    /* Known 0 -> last-tone transitions provide a timing reference every 720 ms.
       The 32 ms rectangular analysis window crosses at boundary + 16 ms. */
    unsigned a = band(rx->tones, 0), b = band(rx->tones, rx->tones - 1);
    double predicted = rx->next_symbol - 20;
    int64_t center = (int64_t)llround(predicted);
    for (int64_t t = center - 8; t < center + 8; ++t) {
        if (t < 1 || (uint64_t)(t + 1) > rx->tick) {
            continue;
        }
        const float *e0 = rx->energy[(uint64_t)t % BS_MFSK_HISTORY];
        const float *e1 = rx->energy[(uint64_t)(t + 1) % BS_MFSK_HISTORY];
        double d0 = e0[b] / rx->gain[b] - e0[a] / rx->gain[a];
        double d1 = e1[b] / rx->gain[b] - e1[a] / rx->gain[a];
        if (d0 <= 0 && d1 > 0 && e1[b] / rx->gain[b] > 0.03) {
            double edge = (double)t - d0 / (d1 - d0);
            if (rx->have_anchor) {
                double measured =
                    (edge - rx->anchor_edge) / (double)(rx->stream_position - rx->anchor_symbol);
                if (measured > 39.6 && measured < 40.4) {
                    rx->period = 0.8 * rx->period + 0.2 * measured;
                }
            } else {
                rx->have_anchor = true;
                rx->anchor_edge = edge;
                rx->anchor_symbol = rx->stream_position;
            }
            rx->next_symbol += 0.35 * fmax(-4, fmin(4, edge - predicted));
            float residual = rx->offset[rx->tick % BS_MFSK_HISTORY];
            if (fabsf(residual) < 12) {
                tune(rx, fmaxf(-25, fminf(25, rx->frequency_offset + 0.15f * residual)));
            }
            break;
        }
    }
}
static void decode_symbol(BsMfskReceiver *rx, BsParser *p) {
    unsigned pilot = (unsigned)(rx->stream_position % 18);
    if (pilot >= 16) {
        if (pilot == 17) {
            pilot_clock(rx);
        }
        ++rx->stream_position;
        return;
    }
    const float *energy = rx->energy[rx->tick % BS_MFSK_HISTORY];
    float scores[16], best = 0;
    for (unsigned i = 0; i < rx->tones; ++i) {
        scores[i] = energy[band(rx->tones, i)] / rx->gain[band(rx->tones, i)];
        best = fmaxf(best, scores[i]);
    }
    unsigned width = rx->tones == 8 ? 3 : 4;
    for (unsigned bit = 0; bit < width; ++bit) {
        float one = 0, zero = 0;
        for (unsigned i = 0; i < rx->tones; ++i) {
            if ((gray(i) >> (width - bit - 1)) & 1) {
                one = fmaxf(one, scores[i]);
            } else {
                zero = fmaxf(zero, scores[i]);
            }
        }
        /* Max-log likelihoods preserve ambiguous/erased symbols for soft Viterbi. */
        float reliability = (one - zero) / (fmaxf(best, 0.02f));
        rx->coded[rx->coded_position++] = (int8_t)lrintf(110 * fmaxf(-1, fminf(1, reliability)));
    }
    ++rx->stream_position;
    if (!rx->payload && rx->coded_position == 96) {
        uint8_t bits[40], bytes[5] = {0};
        if (!bs_fec_decode(rx->coded, 40, bits, sizeof bits)) {
            fail(rx, p, "MFSK header FEC failed");
            return;
        }
        for (unsigned i = 0; i < 40; ++i) {
            bytes[i / 8] = (uint8_t)((bytes[i / 8] << 1) | bits[i]);
        }
        size_t length = (size_t)bytes[1] * 256 + bytes[2];
        if (bytes[0] != 1 || length < 136 || length > BS_MAX_BITS || length % 8 ||
            bs_crc16(bytes, 3) != (uint16_t)((unsigned)bytes[3] * 256 + bytes[4])) {
            fail(rx, p, "MFSK protected header invalid; frame discarded");
            return;
        }
        rx->frame_bits = length;
        rx->payload = true;
        rx->coded_position = 0;
    } else if (rx->payload && rx->coded_position == bs_fec_size(rx->frame_bits - 64)) {
        uint8_t bits[BS_MAX_BITS];
        if (!bs_fec_decode(rx->coded, rx->frame_bits - 64, bits, sizeof bits)) {
            fail(rx, p, "MFSK payload FEC failed");
            return;
        }
        whiten(bits, rx->frame_bits - 64);
        /* The inner CRC remains the authority; Viterbi output alone is never verified. */
        bs_parser_init(p, p->event, p->context);
        p->tones = rx->tones;
        for (unsigned i = 0; i < 64; ++i) {
            bs_parser_bit(p, i < 32 ? i % 2 == 0 : (BS_SYNC >> (63 - i)) & 1, 1);
        }
        for (size_t i = 0; i < rx->frame_bits - 64; ++i) {
            bs_parser_bit(p, bits[i], 1);
        }
        if (bs_parser_active(p)) {
            bs_parser_abort(p, "MFSK decoded body incomplete");
        }
        bs_mfsk_rx_init(rx);
    }
}
static void analyze(BsMfskReceiver *rx, BsParser *p) {
    size_t t = rx->tick % BS_MFSK_HISTORY;
    unsigned winner = 0;
    float maximum = 0;
    for (unsigned i = 0; i < 16; ++i) {
        rx->energy[t][i] = (float)(rx->sum_re[i] * rx->sum_re[i] + rx->sum_im[i] * rx->sum_im[i]);
        if (rx->energy[t][i] > maximum) {
            maximum = rx->energy[t][i];
            winner = i;
        }
    }
    float runner = 0;
    for (unsigned i = 0; i < 16; ++i) {
        if (i != winner) {
            runner = fmaxf(runner, rx->energy[t][i]);
        }
    }
    rx->confidence = (maximum - runner) / (maximum + runner + 1e-9f);
    rx->winner[1][t] = (uint8_t)winner;
    unsigned winner8 = 0;
    for (unsigned i = 1; i < 8; ++i) {
        if (rx->energy[t][band(8, i)] > rx->energy[t][band(8, winner8)]) {
            winner8 = i;
        }
    }
    rx->winner[0][t] = (uint8_t)winner8;
    double nr = rx->half_re[winner], ni = rx->half_im[winner];
    double old_re = rx->sum_re[winner] - nr, old_im = rx->sum_im[winner] - ni;
    rx->offset[t] =
        (float)(atan2(ni * old_re - nr * old_im, nr * old_re + ni * old_im) / (TAU * 0.016));
    if (!rx->locked) {
        search(rx, p);
    } else {
        float reference = rx->gain[band(rx->tones, rx->tones == 8 ? winner8 : winner)];
        rx->quiet = maximum < fmaxf(1e-6f, reference * 1e-4f) ? rx->quiet + 1 : 0;
        if (rx->quiet > 400) {
            bs_mfsk_rx_abort(rx, p, "MFSK signal absent for 400 ms");
        } else if ((double)rx->tick >= rx->next_symbol) {
            decode_symbol(rx, p);
            if (rx->locked) {
                rx->next_symbol += rx->period;
            }
        }
    }
}
void bs_mfsk_rx_sample(BsMfskReceiver *rx, BsParser *p, float sample) {
    rx->fir[rx->fir_position] = sample;
    rx->fir_position = (rx->fir_position + 1) % BS_MFSK_FIR;
    if (++rx->decimation < 6) {
        return;
    }
    rx->decimation = 0;
    float x = 0;
    unsigned at = rx->fir_position;
    for (unsigned i = 0; i < BS_MFSK_FIR; ++i) {
        if (!at) {
            at = BS_MFSK_FIR;
        }
        x += rx->fir[--at] * rx->fir_taps[i];
    }
    unsigned pos = rx->position, middle = (pos + BS_MFSK_WINDOW / 2) % BS_MFSK_WINDOW;
    for (unsigned i = 0; i < 16; ++i) {
        double re = rx->osc_re[i] * rx->step_re[i] - rx->osc_im[i] * rx->step_im[i];
        double im = rx->osc_im[i] * rx->step_re[i] + rx->osc_re[i] * rx->step_im[i];
        rx->osc_re[i] = re;
        rx->osc_im[i] = im;
        float mr = (float)(x * re), mi = (float)(-x * im);
        rx->half_re[i] += mr - rx->mixed_re[i][middle];
        rx->half_im[i] += mi - rx->mixed_im[i][middle];
        rx->sum_re[i] += mr - rx->mixed_re[i][pos];
        rx->sum_im[i] += mi - rx->mixed_im[i][pos];
        rx->mixed_re[i][pos] = mr;
        rx->mixed_im[i][pos] = mi;
        if (pos == 0) {
            double norm = hypot(re, im);
            rx->osc_re[i] /= norm;
            rx->osc_im[i] /= norm;
        }
    }
    rx->position = (pos + 1) % BS_MFSK_WINDOW;
    if (++rx->hop == 8) {
        rx->hop = 0;
        ++rx->tick;
        analyze(rx, p);
    }
}
