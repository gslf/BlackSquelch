#include "fec.h"
#include <string.h>
static unsigned parity(unsigned value) {
    value ^= value >> 4;
    value ^= value >> 2;
    value ^= value >> 1;
    return value & 1;
}
size_t bs_fec_size(size_t bits) {
    return bits <= BS_MAX_BITS ? ((2 * (bits + 6) + 47) / 48) * 48 : 0;
}
bool bs_fec_encode(const uint8_t *bits, size_t count, uint8_t *out, size_t capacity) {
    size_t n = bs_fec_size(count);
    if (!n || capacity < n) {
        return false;
    }
    uint8_t coded[BS_FEC_CODED_MAX] = {0};
    unsigned state = 0;
    for (size_t i = 0; i < count + 6; ++i) {
        state = ((state << 1) | (i < count ? !!bits[i] : 0)) & 127;
        coded[2 * i] = (uint8_t)parity(state & 0171);
        coded[2 * i + 1] = (uint8_t)parity(state & 0133);
    }
    /* Write rows, transmit columns. One burst becomes separated trellis errors. */
    size_t columns = n / 16;
    for (size_t i = 0; i < n; ++i) {
        out[(i % columns) * 16 + i / columns] = coded[i];
    }
    return true;
}
bool bs_fec_decode(const int8_t *input, size_t count, uint8_t *out, size_t capacity) {
    size_t n = bs_fec_size(count);
    if (!n || capacity < count) {
        return false;
    }
    int8_t coded[BS_FEC_CODED_MAX];
    size_t columns = n / 16;
    for (size_t i = 0; i < n; ++i) {
        coded[i] = input[(i % columns) * 16 + i / columns];
    }
    int32_t metrics[64], next[64];
    uint64_t decisions[BS_MAX_BITS + 6];
    for (unsigned state = 0; state < 64; ++state) {
        metrics[state] = state ? -100000000 : 0;
    }
    for (size_t i = 0; i < count + 6; ++i) {
        uint64_t choices = 0;
        for (unsigned state = 0; state < 64; ++state) {
            unsigned p0 = state >> 1, p1 = p0 | 32;
            unsigned r0 = (p0 << 1) | (state & 1), r1 = r0 | 64;
            int a = coded[2 * i], b = coded[2 * i + 1];
            int32_t m0 = metrics[p0] + (parity(r0 & 0171) ? a : -a) + (parity(r0 & 0133) ? b : -b);
            int32_t m1 = metrics[p1] + (parity(r1 & 0171) ? a : -a) + (parity(r1 & 0133) ? b : -b);
            if (m1 > m0) {
                choices |= UINT64_C(1) << state;
                next[state] = m1;
            } else {
                next[state] = m0;
            }
        }
        decisions[i] = choices;
        memcpy(metrics, next, sizeof metrics);
    }
    unsigned state = 0;
    for (size_t i = count + 6; i > 0; --i) {
        if (i <= count) {
            out[i - 1] = (uint8_t)(state & 1);
        }
        state = (state >> 1) | (unsigned)(((decisions[i - 1] >> state) & 1) << 5);
    }
    return state == 0;
}
