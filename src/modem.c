#include "modem.h"
#include <math.h>
#include <string.h>
#define TAU 6.28318530717958647693
const unsigned bs_bit_rates[BS_PROFILE_COUNT] = {10, 25, 50};
bool bs_bit_rate_valid(unsigned bit_rate) {
    for (unsigned i = 0; i < BS_PROFILE_COUNT; ++i) {
        if (bs_bit_rates[i] == bit_rate) {
            return true;
        }
    }
    return false;
}
unsigned bs_symbol_samples(unsigned bit_rate) {
    return bs_bit_rate_valid(bit_rate) ? BS_RATE / bit_rate : 0;
}
double bs_frame_duration(const BsFrame *frame, unsigned bit_rate) {
    return bs_bit_rate_valid(bit_rate) ? (double)frame->count / bit_rate : 0;
}
bool bs_tx_init(BsTransmitter *tx, const BsFrame *frame, double amplitude, unsigned bit_rate) {
    memset(tx, 0, sizeof *tx);
    if (!bs_bit_rate_valid(bit_rate)) {
        return false;
    }
    tx->frame = frame;
    tx->symbol_samples = bs_symbol_samples(bit_rate);
    tx->amplitude = amplitude < 0 ? 0 : amplitude > 1 ? 1 : amplitude;
    return true;
}
size_t bs_tx_read(BsTransmitter *tx, int16_t *samples, size_t capacity) {
    if (!tx->symbol_samples) {
        return 0;
    }
    uint64_t total = tx->frame->count * (uint64_t)tx->symbol_samples;
    size_t n = 0;
    while (n < capacity && tx->sample < total) {
        unsigned bit = tx->frame->bits[tx->sample / tx->symbol_samples];
        /* Only the transmission edges are ramped; phase survives every bit transition. */
        double ramp =
            fmin(1.0, fmin((double)tx->sample / 240.0, (double)(total - 1 - tx->sample) / 240.0));
        samples[n++] = (int16_t)lrint(sin(tx->phase) * 32767 * tx->amplitude * ramp);
        tx->phase += TAU * (bit ? 2200 : 1200) / BS_RATE;
        if (tx->phase >= TAU) {
            tx->phase -= TAU;
        }
        ++tx->sample;
    }
    return n;
}
static void candidate_event(void *context, const BsEvent *event) {
    BsClock *clock = context;
    BsReceiver *rx = clock->receiver;
    int index = (int)(clock - rx->clocks);
    /* Early votes are competing timing hypotheses, not decoded data. Show a
       candidate only after four alternating decisions, and avoid flickering
       between hypotheses while the current candidate is still plausible. */
    if (event->kind != BS_PROGRESS && event->kind != BS_FIELD_DONE) {
        return;
    }
    if (event->received >= 4 &&
        (rx->preview < 0 || rx->clocks[rx->preview].candidate.received < 2)) {
        rx->preview = index;
    }
    if (rx->preview == index && rx->parser->event) {
        rx->parser->event(rx->parser->context, event);
    }
}
static void search_rates(BsReceiver *rx) {
    rx->selected = rx->preview = -1;
    rx->bit_rate = 0;
    rx->period = 0;
    for (unsigned i = 0; i < BS_PROFILE_COUNT; ++i) {
        BsClock *clock = &rx->clocks[i];
        memset(clock, 0, sizeof *clock);
        clock->receiver = rx;
        clock->bit_rate = bs_bit_rates[i];
        clock->period = bs_symbol_samples(clock->bit_rate);
        clock->previous_tone = -1;
        bs_parser_init(&clock->candidate, candidate_event, clock);
        clock->candidate.bit_rate = clock->bit_rate;
    }
}
void bs_rx_init(BsReceiver *rx, BsParser *parser) {
    memset(rx, 0, sizeof *rx);
    rx->parser = parser;
    search_rates(rx);
    for (int t = 0; t < 2; ++t) {
        for (int i = 0; i < BS_WINDOW; ++i) {
            double phase = TAU * (t ? 2200 : 1200) * i / BS_RATE;
            rx->cosine[t][i] = cos(phase);
            rx->sine[t][i] = sin(phase);
        }
    }
}
static void acquire_rate(BsReceiver *rx, unsigned index) {
    BsClock *clock = &rx->clocks[index];
    BsEventFn event = rx->parser->event;
    void *context = rx->parser->context;
    /* Transfer the observed preamble state, preserving the application's event
       sink. Only this clock can feed the body; payload transitions cannot change rate. */
    *rx->parser = clock->candidate;
    rx->parser->event = event;
    rx->parser->context = context;
    rx->selected = (int)index;
    rx->bit_rate = clock->bit_rate;
    rx->period = clock->period;
    if (event) {
        BsEvent detected = {0};
        detected.kind = BS_RATE_DETECTED;
        detected.field = BS_PREAMBLE;
        detected.position = 32;
        detected.bit_rate = clock->bit_rate;
        detected.symbol_ms = clock->period * 1000 / BS_RATE;
        detected.confidence = rx->confidence;
        detected.detail = "Preamble acquired; receive rate locked";
        event(context, &detected);
    }
}
static void clock_window(BsReceiver *rx, unsigned index, const double energy[2], bool signal) {
    BsClock *clock = &rx->clocks[index];
    BsParser *parser = rx->selected >= 0 ? rx->parser : &clock->candidate;
    double nominal = bs_symbol_samples(clock->bit_rate);
    double now = (double)rx->samples - BS_WINDOW / 2.0;
    int tone = energy[1] > energy[0];
    if (signal) {
        clock->missing = 0;
    } else {
        clock->missing += BS_HOP;
    }
    if (!clock->running && signal) {
        clock->running = true;
        clock->boundary = now;
        clock->last_transition = now;
        clock->previous_tone = tone;
    }
    if (!clock->running) {
        return;
    }
    if (clock->missing >= 10 * nominal) {
        if (rx->selected >= 0) {
            bs_parser_abort(rx->parser, "Signal absent for ten symbols");
            search_rates(rx);
        } else {
            clock->running = false;
            clock->integrated[0] = clock->integrated[1] = 0;
            clock->decided = clock->uncertain = false;
            clock->period = nominal;
            bs_parser_init(parser, candidate_event, clock);
            parser->bit_rate = clock->bit_rate;
        }
        return;
    }
    if (signal && tone != clock->previous_tone) {
        double old_amplitude = sqrt(energy[clock->previous_tone]);
        double new_amplitude = sqrt(energy[tone]);
        /* Interpolate the mixed analysis window to locate an edge more precisely
           than one window. The 1 ms hop also leaves enough decisions at 50 bit/s. */
        double edge = rx->samples - BS_WINDOW +
                      BS_WINDOW * old_amplitude / (old_amplitude + new_amplitude + 1e-12);
        double elapsed = edge - clock->last_transition;
        double symbols = floor(elapsed / clock->period + 0.5);
        double predicted =
            clock->boundary + round((edge - clock->boundary) / clock->period) * clock->period;
        double error = edge - predicted;
        if (parser->field == BS_PREAMBLE && parser->received < 2) {
            clock->boundary = edge;
            clock->anchor_edge = edge;
            clock->tracked_symbols = 0;
            clock->last_transition = edge;
            clock->decided = false;
            clock->uncertain = false;
            clock->integrated[0] = clock->integrated[1] = 0;
        } else if (fabs(error) < 0.20 * clock->period && symbols >= 1) {
            double measured = elapsed / symbols;
            if (measured > 0.97 * nominal && measured < 1.03 * nominal) {
                if (!clock->tracked_symbols) {
                    clock->anchor_edge = clock->last_transition;
                }
                clock->tracked_symbols += symbols;
                /* Average over the whole accepted timing baseline. A short local
                   estimate can drift by a full bit during 512 identical characters. */
                double average = (edge - clock->anchor_edge) / clock->tracked_symbols;
                clock->period =
                    clock->tracked_symbols >= 8 ? average : 0.9 * clock->period + 0.1 * measured;
                clock->last_transition = edge;
            }
            clock->boundary += 0.35 * error;
        }
        clock->previous_tone = tone;
    }
    while (now > clock->boundary + clock->period) {
        clock->boundary += clock->period;
        clock->decided = clock->uncertain = false;
        clock->integrated[0] = clock->integrated[1] = 0;
    }
    double phase = (now - clock->boundary) / clock->period;
    if (phase >= 0.20 && phase <= 0.80) {
        if (signal) {
            clock->integrated[0] += energy[0];
            clock->integrated[1] += energy[1];
        } else {
            clock->uncertain = true;
        }
    }
    if (phase >= 0.80 && !clock->decided) {
        double a = clock->integrated[0], b = clock->integrated[1];
        float confidence = (float)(fabs(a - b) / (a + b + 1e-12));
        if (clock->uncertain && confidence > 0.4f) {
            confidence = 0.4f;
        }
        if (clock->missing > clock->period / 4) {
            confidence = 0;
        }
        int bit = a + b > 1e-9 ? b > a : clock->last_bit;
        bs_parser_bit(parser, bit, confidence);
        clock->last_bit = bit;
        clock->decided = true;
        if (rx->selected < 0 && bs_parser_active(parser)) {
            acquire_rate(rx, index);
        } else if (rx->selected >= 0 && !bs_parser_active(parser)) {
            search_rates(rx);
        }
    }
    if (rx->selected >= 0) {
        rx->period = rx->clocks[rx->selected].period;
    }
}
static void analyze_window(BsReceiver *rx) {
    double re[2] = {0}, im[2] = {0};
    unsigned index = rx->history_position;
    for (unsigned i = 0; i < BS_WINDOW; ++i) {
        double x = rx->history[index];
        for (unsigned tone = 0; tone < 2; ++tone) {
            re[tone] += x * rx->cosine[tone][i];
            im[tone] += x * rx->sine[tone][i];
        }
        if (++index == BS_WINDOW) {
            index = 0;
        }
    }
    double energy[2] = {re[0] * re[0] + im[0] * im[0], re[1] * re[1] + im[1] * im[1]};
    double sum = energy[0] + energy[1];
    double quality = fabs(energy[1] - energy[0]) / (sum + 1e-12);
    double coherence = 2 * fmax(energy[0], energy[1]) / (BS_WINDOW * rx->power + 1e-12);
    bool signal = rx->power / BS_WINDOW > 1e-7 && coherence > 0.30 && quality > 0.25;
    rx->level = (float)sqrt(fmax(0, rx->power / BS_WINDOW));
    rx->confidence = signal ? (float)(quality * fmin(1, coherence)) : 0;
    if (rx->selected >= 0) {
        clock_window(rx, (unsigned)rx->selected, energy, signal);
    } else {
        for (unsigned i = 0; i < BS_PROFILE_COUNT; ++i) {
            clock_window(rx, i, energy, signal);
            if (rx->selected >= 0) {
                break;
            }
        }
    }
}
void bs_rx_read(BsReceiver *rx, const int16_t *samples, size_t count) {
    rx->clipped = 0;
    for (size_t i = 0; i < count; ++i) {
        double x = samples[i] / 32768.0;
        if (samples[i] >= 32760 || samples[i] <= -32760) {
            ++rx->clipped;
        }
        rx->dc += 0.001 * (x - rx->dc);
        x -= rx->dc;
        double old = rx->history[rx->history_position];
        rx->power += x * x - old * old;
        rx->history[rx->history_position] = x;
        if (++rx->history_position == BS_WINDOW) {
            rx->history_position = 0;
        }
        ++rx->samples;
        if (++rx->hop == BS_HOP) {
            rx->hop = 0;
            if (rx->samples >= BS_WINDOW) {
                analyze_window(rx);
            }
        }
    }
}
