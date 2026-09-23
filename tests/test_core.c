#include "protocol.h"
#include "modem.h"
#include "wav.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    unsigned verified, invalid, crc_error, interrupted, progress, uncertain, acquired, bit_rate;
    char call[BS_CALL_CHARS + 1], text[BS_MAX_TEXT + 1], partial[33];
} Results;
static void collect(void *context, const BsEvent *e) {
    Results *r = context;
    if (e->kind == BS_RATE_DETECTED) {
        ++r->acquired;
        r->bit_rate = e->bit_rate;
        assert(e->symbol_ms > 0);
    }
    if (e->kind == BS_PROGRESS) {
        ++r->progress;
        if (e->uncertain) {
            ++r->uncertain;
        }
    }
    if (e->kind == BS_INVALID) {
        ++r->invalid;
    }
    if (e->kind == BS_BAD_CRC) {
        ++r->crc_error;
    }
    if (e->kind == BS_INTERRUPTED) {
        ++r->interrupted;
        strcpy(r->partial, e->bits);
    }
    if (e->kind == BS_VERIFIED) {
        ++r->verified;
        strcpy(r->call, e->call);
        strcpy(r->text, e->text);
    }
}
static Results parse(const BsFrame *f) {
    Results r = {0};
    BsParser p;
    bs_parser_init(&p, collect, &r);
    for (size_t i = 0; i < f->count; ++i) {
        bs_parser_bit(&p, f->bits[i], 1);
    }
    return r;
}
static void change(BsFrame *f, size_t position, unsigned value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) {
        f->bits[position + i] = (uint8_t)((value >> (width - i - 1)) & 1);
    }
}
static void repair_crc(BsFrame *f) {
    uint8_t bytes[400];
    size_t n = 0;
    for (size_t i = 64; i < f->count - 16; i += 8) {
        unsigned byte = 0;
        for (unsigned j = 0; j < 8; ++j) {
            byte = byte * 2 + f->bits[i + j];
        }
        bytes[n++] = (uint8_t)byte;
    }
    change(f, f->count - 16, bs_crc16(bytes, n), 16);
}
static void test_protocol(void) {
    assert(bs_crc16((const uint8_t *)"123456789", 9) == 0x29b1);
    assert(strlen(bs_alphabet) == 47);
    for (int i = 0; i < 47; ++i) {
        assert(bs_code((unsigned char)bs_alphabet[i]) == i);
    }
    assert(bs_code(0) == -1 && bs_code(27) == -1 && bs_code(255) == -1);
    char normalized[] = "iu2abc";
    assert(bs_normalize(normalized, true, NULL));
    assert(!strcmp(normalized, "IU2ABC"));
    char unsupported[] = "a@b";
    size_t bad;
    assert(!bs_normalize(unsupported, false, &bad));
    assert(bad == 1 && !strcmp(unsupported, "A@B"));
    BsFrame f;
    assert(bs_frame_build(&f, "IU2ABC", "CIAO"));
    const uint8_t golden[] = {0xaa, 0xaa, 0xaa, 0xaa, 0x1a, 0xcf, 0xfc, 0x1d, 0x21, 0x47,
                              0x00, 0x04, 0x20, 0x00, 0x40, 0x88, 0x00, 0xe0, 0xac, 0x84};
    assert(f.count == sizeof golden * 8);
    for (size_t i = 0; i < f.count; ++i) {
        assert(f.bits[i] == ((golden[i / 8] >> (7 - i % 8)) & 1));
    }
    Results r = parse(&f);
    assert(r.verified == 1 && !strcmp(r.text, "CIAO") && r.progress == f.count);
    BsFrame corrupt = f;
    corrupt.bits[122] ^= 1;
    r = parse(&corrupt);
    assert(!r.verified && r.crc_error);
    corrupt = f;
    corrupt.bits[f.count - 17] = 1;
    repair_crc(&corrupt);
    r = parse(&corrupt);
    assert(!r.verified && r.invalid);
    for (unsigned code = 47; code < 64; ++code) {
        corrupt = f;
        change(&corrupt, 116, code, 6);
        repair_crc(&corrupt);
        r = parse(&corrupt);
        assert(!r.verified && r.invalid);
    }
    for (unsigned code = 36; code < 64; ++code) {
        corrupt = f;
        change(&corrupt, 64, code, 6);
        repair_crc(&corrupt);
        r = parse(&corrupt);
        assert(!r.verified && r.invalid);
    }
    const char *bad_calls[] = {"", "A", "ABCD", "ABCDE", "ABCDEFG", "ABCDEFGHIJ", "AB CD1"};
    for (size_t i = 0; i < sizeof bad_calls / sizeof bad_calls[0]; ++i) {
        assert(!bs_frame_build(&corrupt, bad_calls[i], "A"));
    }
    corrupt = f;
    change(&corrupt, 100, 513, 16);
    r = parse(&corrupt);
    assert(!r.verified && r.invalid);
    corrupt = f;
    change(&corrupt, 100, 0, 16);
    r = parse(&corrupt);
    assert(!r.verified && r.invalid);
    unsigned padding_seen = 0;
    for (size_t n = 1; n <= BS_MAX_TEXT; ++n) {
        char text[BS_MAX_TEXT + 1];
        for (size_t i = 0; i < n; ++i) {
            text[i] = bs_alphabet[i % 47];
        }
        text[n] = 0;
        assert(bs_frame_build(&f, "IU2ABC", text));
        size_t padding = (8 - (52 + 6 * n) % 8) % 8;
        padding_seen |= 1u << padding;
        assert(f.count == 132 + 6 * n + padding && f.count <= BS_MAX_BITS);
        r = parse(&f);
        assert(r.verified == 1 && !strcmp(r.call, "IU2ABC") && !strcmp(r.text, text));
        for (unsigned i = 0; i < BS_PROFILE_COUNT; ++i) {
            assert(fabs(bs_frame_duration(&f, bs_bit_rates[i]) * bs_bit_rates[i] - f.count) < 1e-9);
        }
    }
    assert(padding_seen == 0x55);
    assert(!bs_frame_build(&f, "ABC", "A") && !bs_frame_build(&f, "AB C", "A") &&
           !bs_frame_build(&f, "IU2ABC", "") && !bs_frame_build(&f, "IU2ABC", "\033"));
    assert(bs_frame_build(&f, "IU2ABC", "ABC"));
    for (size_t cut = 65; cut < f.count; ++cut) {
        BsParser p;
        memset(&r, 0, sizeof r);
        bs_parser_init(&p, collect, &r);
        for (size_t i = 0; i < cut; ++i) {
            bs_parser_bit(&p, f.bits[i], 1);
        }
        bs_parser_abort(&p, "Truncated test");
        assert(!r.verified && r.interrupted == 1);
        for (size_t i = 0; i < f.count; ++i) {
            bs_parser_bit(&p, f.bits[i], 1);
        }
        assert(r.verified == 1 && !strcmp(r.call, "IU2ABC"));
    }
    puts("PASS protocol: golden vector, all lengths/padding, reserved codes, corruption, "
         "truncation/recovery");
}
static uint32_t random_state = 0x12345678;
static double noise(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return ((double)(random_state >> 8) / 16777216.0) * 2 - 1;
}
static Results channel(const char *text, double rate, double noise_level, double gain,
                       size_t offset, int dropout, unsigned bit_rate) {
    BsFrame f;
    assert(bs_frame_build(&f, "IU2ABC", text));
    unsigned symbol = bs_symbol_samples(bit_rate);
    size_t n = f.count * symbol;
    int16_t *source = malloc(n * sizeof *source);
    assert(source);
    BsTransmitter tx;
    assert(bs_tx_init(&tx, &f, 0.5, bit_rate));
    assert(bs_tx_read(&tx, source, n) == n && bs_tx_read(&tx, source, 1) == 0);
    BsParser p;
    BsReceiver rx;
    Results r = {0};
    bs_parser_init(&p, collect, &r);
    bs_rx_init(&rx, &p);
    int16_t out[BS_BLOCK];
    size_t total = (size_t)(n / rate) + offset + BS_RATE;
    for (size_t at = 0; at < total;) {
        size_t count = total - at < BS_BLOCK ? total - at : BS_BLOCK;
        for (size_t j = 0; j < count; ++j) {
            double position = ((double)(at + j) - (double)offset) * rate, value = 0;
            if (position >= 0 && position < (double)n - 1) {
                size_t k = (size_t)position;
                double fraction = position - k;
                value = (source[k] * (1 - fraction) + source[k + 1] * fraction) * gain;
                if ((dropout == 1 && position > 100 * symbol && position < 111 * symbol) ||
                    (dropout == 2 && position > 100.3 * symbol && position < 100.6 * symbol)) {
                    value = 0;
                }
            }
            value += noise() * noise_level * 32768;
            value = fmax(-32768, fmin(32767, value));
            out[j] = (int16_t)lrint(value);
        }
        bs_rx_read(&rx, out, count);
        at += count;
    }
    free(source);
    return r;
}
static void test_modem(void) {
    struct Case {
        const char *name;
        double rate, noise, gain;
        size_t offset;
    } cases[] = {{"clean", 1, 0, 1, 0},
                 {"offset and noise", 1, 0.08, 1, 1327},
                 {"clipping", 1, 0.01, 4, 47},
                 {"clock +1000 ppm", 1.001, 0.01, 1, 2029},
                 {"clock -1000 ppm", 0.999, 0.01, 1, 369},
                 {"quiet with DC filter", 1, 0, 0.02, 11}};
    for (unsigned profile = 0; profile < BS_PROFILE_COUNT; ++profile) {
        unsigned bit_rate = bs_bit_rates[profile];
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            struct Case c = cases[i];
            Results r = channel(bs_alphabet, c.rate, c.noise, c.gain, c.offset, 0, bit_rate);
            printf("Channel %u bit/s %-24s verified=%u locked=%u invalid=%u crc=%u\n", bit_rate,
                   c.name, r.verified, r.bit_rate, r.invalid, r.crc_error);
            fflush(stdout);
            assert(r.verified == 1 && r.acquired == 1 && r.bit_rate == bit_rate &&
                   !strcmp(r.text, bs_alphabet));
        }
        char long_text[BS_MAX_TEXT + 1];
        memset(long_text, 'A', BS_MAX_TEXT);
        long_text[BS_MAX_TEXT] = 0;
        Results r = channel(long_text, 1, 0.02, 1, 715, 0, bit_rate);
        assert(r.verified == 1 && r.bit_rate == bit_rate);
        for (int sign = -1; sign <= 1; sign += 2) {
            Results drift = channel(long_text, 1 + sign * 0.0001, 0, 1, 715, 0, bit_rate);
            printf("Long constant run %u bit/s clock %+d ppm: verified=%u\n", bit_rate, sign * 100,
                   drift.verified);
            fflush(stdout);
            assert(drift.verified == 1);
        }
        r = channel("BRIEF FADES KEEP THEIR BIT SLOT", 1, 0, 1, 0, 2, bit_rate);
        assert(r.verified == 1 && r.uncertain > 0);
        r = channel("SIGNAL LOSS MUST ABORT", 1, 0, 1, 0, 1, bit_rate);
        assert(!r.verified && r.interrupted);
    }
    BsParser p;
    BsReceiver rx;
    Results r = {0};
    bs_parser_init(&p, collect, &r);
    bs_rx_init(&rx, &p);
    int16_t noise_block[BS_BLOCK];
    for (int k = 0; k < 1000; ++k) {
        for (int i = 0; i < BS_BLOCK; ++i) {
            noise_block[i] = (int16_t)(noise() * 20000);
        }
        bs_rx_read(&rx, noise_block, BS_BLOCK);
    }
    assert(!r.verified && !r.acquired && !bs_parser_active(&p));
    puts("PASS modem: automatic rates, alphabet, noise, clipping, offsets, drift, fades and signal "
         "loss");
}
static void stream_frame(BsReceiver *rx, const BsFrame *frame, unsigned bit_rate) {
    BsTransmitter tx;
    assert(bs_tx_init(&tx, frame, 0.5, bit_rate));
    int16_t block[713];
    size_t n;
    uint64_t total = 0;
    /* Non-window-aligned block sizes exercise streaming state at every boundary. */
    while ((n = bs_tx_read(&tx, block, sizeof block / sizeof block[0]))) {
        bs_rx_read(rx, block, n);
        total += n;
    }
    assert(total == frame->count * (uint64_t)bs_symbol_samples(bit_rate));
}
static void test_rate_changes(void) {
    const unsigned rates[] = {10, 25, 50, 10, 50, 25, 25, 10, 50};
    BsParser p;
    BsReceiver rx;
    Results r = {0};
    BsFrame f;
    bs_parser_init(&p, collect, &r);
    bs_rx_init(&rx, &p);
    assert(bs_frame_build(&f, "IU2ABC", "AUTOMATIC RATE"));
    for (size_t i = 0; i < sizeof rates / sizeof rates[0]; ++i) {
        /* No receiver reset, local rate setting, or silence is needed between frames. */
        stream_frame(&rx, &f, rates[i]);
        printf("Consecutive frame %zu rate=%u verified=%u\n", i, rates[i], r.verified);
        fflush(stdout);
        assert(r.verified == i + 1 && r.bit_rate == rates[i] && !strcmp(r.text, "AUTOMATIC RATE"));
    }
    assert(bs_frame_build(&f, "IU2ABC", "BAD SYNC"));
    change(&f, 32, 0, 32);
    stream_frame(&rx, &f, 50);
    unsigned verified = r.verified;
    assert(r.invalid > 0);
    assert(bs_frame_build(&f, "IU2ABC", "AFTER BAD SYNC"));
    stream_frame(&rx, &f, 25);
    assert(r.verified == verified + 1 && r.bit_rate == 25);
    BsTransmitter tx;
    assert(!bs_tx_init(&tx, &f, 0.5, 0) && !bs_tx_init(&tx, &f, 0.5, 30));
    assert(bs_symbol_samples(30) == 0 && bs_frame_duration(&f, 30) == 0);
    puts("PASS rate changes: mixed consecutive frames, malformed-frame recovery, invalid TX rates");
}
static void test_wav(void) {
    BsFrame f;
    assert(bs_frame_build(&f, "IU2ABC", "WAV TEST"));
    const char *path = "test-roundtrip.wav";
    for (unsigned i = 0; i < BS_PROFILE_COUNT; ++i) {
        assert(bs_wav_write(path, &f, 0.5, bs_bit_rates[i]));
        BsParser p;
        BsReceiver rx;
        Results r = {0};
        char error[256];
        bs_parser_init(&p, collect, &r);
        bs_rx_init(&rx, &p);
        assert(bs_wav_decode(path, &rx, error, sizeof error));
        assert(r.verified == 1 && r.bit_rate == bs_bit_rates[i] && !strcmp(r.text, "WAV TEST"));
        FILE *file = fopen(path, "wb");
        assert(file);
        fputs("RIFF\377\377\377\377WAVEdata", file);
        fclose(file);
        assert(!bs_wav_decode(path, &rx, error, sizeof error));
        remove(path);
    }
    assert(!bs_wav_write(path, &f, 0.5, 30));
    puts(
        "PASS WAV: all three rates auto-detected, invalid rates and malformed containers rejected");
}
int main(void) {
    test_protocol();
    test_modem();
    test_rate_changes();
    test_wav();
    puts("All tests passed.");
    return 0;
}
