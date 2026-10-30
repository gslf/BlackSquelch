#include "modem.h"
#include "fec.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    unsigned verified, acquired, invalid, tones;
    char text[BS_MAX_TEXT + 1];
} Result;
static void collect(void *context, const BsEvent *event) {
    Result *r = context;
    if (event->kind == BS_VERIFIED) {
        ++r->verified;
        r->tones = event->tones;
        strcpy(r->text, event->text);
    } else if (event->kind == BS_MODE_DETECTED) {
        ++r->acquired;
        printf("acquired %u tones\n", event->tones);
    } else if (event->kind == BS_INVALID) {
        ++r->invalid;
        printf("invalid %s\n", event->detail);
    }
}
static void test_fec(void) {
    uint8_t bits[BS_MAX_BITS], encoded[BS_FEC_CODED_MAX], decoded[BS_MAX_BITS];
    int8_t soft[BS_FEC_CODED_MAX];
    bits[0] = 1;
    assert(bs_fec_encode(bits, 1, encoded, sizeof encoded));
    const char *impulse = "11010011111011";
    for (size_t i = 0; i < 48; ++i) {
        assert(encoded[(i % 3) * 16 + i / 3] == (i < 14 ? impulse[i] - '0' : 0));
    }
    for (size_t count = 1; count <= BS_MAX_BITS;
         count = count == 1     ? 160
                 : count == 160 ? 512
                                : BS_MAX_BITS + (count == BS_MAX_BITS)) {
        for (size_t i = 0; i < count; ++i) {
            bits[i] = (uint8_t)((i * 7919 + i / 7) & 1);
        }
        size_t n = bs_fec_size(count);
        assert(bs_fec_encode(bits, count, encoded, sizeof encoded));
        for (size_t i = 0; i < n; ++i) {
            soft[i] = encoded[i] ? 100 : -100;
        }
        if (count >= 160) {
            memset(soft + n / 3, 0, 32); /* A physical burst, before deinterleaving. */
        }
        assert(bs_fec_decode(soft, count, decoded, sizeof decoded));
        assert(!memcmp(bits, decoded, count));
    }
    puts("PASS convolutional FEC: impulse vector, soft erasures, termination and maximum size");
}
static void stream(BsReceiver *rx, const BsFrame *frame, unsigned tones) {
    BsTransmitter tx;
    assert(bs_tx_init(&tx, frame, 0.5, tones));
    int16_t block[BS_BLOCK];
    size_t n;
    while ((n = bs_tx_read(&tx, block, BS_BLOCK))) {
        bs_rx_read(rx, block, n);
    }
}
static uint32_t rng = UINT32_C(0x91A3B275);
static double random_noise(void) {
    rng = rng * UINT32_C(1664525) + UINT32_C(1013904223);
    return ((double)(rng >> 8) / 16777216.0) * 2 - 1;
}
static void test_radio_chain(void) {
    const struct {
        const char *name;
        double frequency, noise, interference;
        bool unequal;
    } cases[] = {
        {"frequency +12 Hz", 12, 0.02, 0, false},
        {"frequency -12 Hz", -12, 0.02, 0, false},
        {"unequal tone response", 0, 0.03, 0, true},
        {"narrowband interferer", 0, 0.03, 0.10, false},
        {"combined audio impairments", 8, 0.04, 0.06, true},
    };
    BsReceiver *rx = malloc(sizeof *rx);
    assert(rx);
    BsFrame frame;
    assert(bs_frame_build(&frame, "IU2ABC", bs_alphabet));
    for (unsigned tones = 8; tones <= 16; tones += 8) {
        BsTransmitter tx;
        assert(bs_tx_init(&tx, &frame, 0.5, tones));
        for (size_t c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
            Result result = {0};
            BsParser parser;
            bs_parser_init(&parser, collect, &result);
            bs_rx_init(rx, &parser);
            uint64_t total = bs_frame_samples(&frame, tones);
            double phase = 0;
            int16_t block[713];
            for (uint64_t at = 0; at < total;) {
                size_t n = total - at < 713 ? (size_t)(total - at) : 713;
                for (size_t j = 0; j < n; ++j) {
                    unsigned tone = tx.symbols[(at + j) / BS_MFSK_SYMBOL_SAMPLES];
                    double gain = cases[c].unequal ? 0.4 + 0.6 * tone / (tones - 1) : 1;
                    if (cases[c].unequal && tone == 5) {
                        gain = 0.15;
                    }
                    double value = 0.5 * gain * sin(phase) + cases[c].noise * random_noise() +
                                   cases[c].interference *
                                       sin(6.283185307179586 * 1500 * (double)(at + j) / BS_RATE);
                    phase += 6.283185307179586 *
                             (bs_mfsk_frequency(tones, tone) + cases[c].frequency) / BS_RATE;
                    if (phase > 6.283185307179586) {
                        phase -= 6.283185307179586;
                    }
                    block[j] = (int16_t)lrint(fmax(-32768, fmin(32767, value * 32767)));
                }
                bs_rx_read(rx, block, n);
                at += n;
            }
            printf("MFSK%u %-28s verified=%u invalid=%u\n", tones, cases[c].name, result.verified,
                   result.invalid);
            fflush(stdout);
            assert(result.verified == 1 && result.tones == tones &&
                   !strcmp(result.text, bs_alphabet));
        }
        Result result = {0};
        BsParser parser;
        bs_parser_init(&parser, collect, &result);
        bs_rx_init(rx, &parser);
        /* Destroy every protected-header data symbol, while retaining the pilots.
           A FEC output with an invalid header CRC/version must never expose a message. */
        size_t header_symbols = 96 / (tones == 8 ? 3 : 4), data = 0;
        for (size_t i = BS_MFSK_TRAINING; data < header_symbols; ++i) {
            if ((i - BS_MFSK_TRAINING) % 18 < 16) {
                tx.symbols[i] = 0;
                ++data;
            }
        }
        int16_t block[BS_BLOCK];
        size_t n;
        while ((n = bs_tx_read(&tx, block, BS_BLOCK))) {
            bs_rx_read(rx, block, n);
        }
        assert(!result.verified && result.invalid);
        stream(rx, &frame, tones);
        assert(result.verified == 1);
    }
    free(rx);
    puts("PASS audio DSP: AFC, tone gain calibration, narrowband interference, mixed impairments, "
         "protected header recovery");
}
int main(void) {
    test_fec();
    test_radio_chain();
    BsReceiver *rx = malloc(sizeof *rx);
    assert(rx);
    BsParser parser;
    Result result = {0};
    bs_parser_init(&parser, collect, &result);
    bs_rx_init(rx, &parser);
    BsFrame frame;
    assert(bs_frame_build(&frame, "IU2ABC", "CIAO"));
    stream(rx, &frame, 8);
    assert(result.verified == 1 && result.tones == 8 && !strcmp(result.text, "CIAO"));
    stream(rx, &frame, 16);
    assert(result.verified == 2 && result.tones == 16);
    BsTransmitter invalid;
    assert(!bs_tx_init(&invalid, &frame, 0.5, 2));
    free(rx);
    puts("PASS MFSK auto profiles: 8 -> 16 without resetting receiver; removed 2-tone mode "
         "rejected");
    return 0;
}
