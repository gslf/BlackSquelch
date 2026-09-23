#include "wav.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
static uint32_t le32(const unsigned char *b) {
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static unsigned le16(const unsigned char *b) {
    return b[0] | (unsigned)b[1] << 8;
}
static bool write_le(FILE *f, uint32_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) {
        if (fputc((int)((value >> (8 * i)) & 255), f) == EOF) {
            return false;
        }
    }
    return true;
}
bool bs_wav_write(const char *path, const BsFrame *frame, double amplitude, unsigned bit_rate) {
    if (!bs_bit_rate_valid(bit_rate)) {
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    uint32_t bytes = (uint32_t)(frame->count * bs_symbol_samples(bit_rate) * 2);
    bool ok = fwrite("RIFF", 1, 4, f) == 4 && write_le(f, bytes + 36, 4) &&
              fwrite("WAVEfmt ", 1, 8, f) == 8 && write_le(f, 16, 4) && write_le(f, 1, 2) &&
              write_le(f, 1, 2) && write_le(f, BS_RATE, 4) && write_le(f, BS_RATE * 2, 4) &&
              write_le(f, 2, 2) && write_le(f, 16, 2) && fwrite("data", 1, 4, f) == 4 &&
              write_le(f, bytes, 4);
    BsTransmitter tx;
    bs_tx_init(&tx, frame, amplitude, bit_rate);
    int16_t block[BS_BLOCK];
    size_t n;
    unsigned char packed[BS_BLOCK * 2];
    while (ok && (n = bs_tx_read(&tx, block, BS_BLOCK))) {
        for (size_t i = 0; i < n; ++i) {
            packed[i * 2] = (unsigned char)block[i];
            packed[i * 2 + 1] = (unsigned char)((uint16_t)block[i] >> 8);
        }
        ok = fwrite(packed, 2, n, f) == n;
    }
    if (fclose(f)) {
        ok = false;
    }
    return ok;
}
bool bs_wav_decode(const char *path, BsReceiver *rx, char *error, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(error, cap, "Cannot open WAV input");
        return false;
    }
    const char *reason = "Malformed or truncated RIFF/WAVE file";
    unsigned char header[12], chunk[8], format[16];
    bool fmt = false, data = false, ok = false;
    if (fread(header, 1, 12, f) != 12 || memcmp(header, "RIFF", 4) ||
        memcmp(header + 8, "WAVE", 4)) {
        goto done;
    }
    uint64_t remaining = le32(header + 4);
    if (remaining < 4) {
        goto done;
    }
    remaining -= 4;
    while (remaining >= 8) {
        if (fread(chunk, 1, 8, f) != 8) {
            goto done;
        }
        remaining -= 8;
        uint32_t size = le32(chunk + 4);
        uint64_t padded = (uint64_t)size + (size & 1);
        if (padded > remaining) {
            goto done;
        }
        if (!memcmp(chunk, "fmt ", 4)) {
            if (size < 16 || fread(format, 1, 16, f) != 16) {
                goto done;
            }
            if (le16(format) != 1 || le16(format + 2) != 1 || le32(format + 4) != BS_RATE ||
                le32(format + 8) != BS_RATE * 2 || le16(format + 12) != 2 ||
                le16(format + 14) != 16) {
                reason = "WAV must be PCM, mono, signed 16-bit, 48000 Hz";
                goto done;
            }
            fmt = true;
            for (uint32_t i = 16; i < size; ++i) {
                if (fgetc(f) == EOF) {
                    goto done;
                }
            }
        } else if (!memcmp(chunk, "data", 4)) {
            if (!fmt || size % 2 || data) {
                goto done;
            }
            data = true;
            unsigned char bytes[BS_BLOCK * 2];
            int16_t samples[BS_BLOCK];
            for (uint32_t left = size; left;) {
                size_t n = left < sizeof bytes ? left : sizeof bytes;
                if (fread(bytes, 1, n, f) != n) {
                    goto done;
                }
                for (size_t i = 0; i < n / 2; ++i) {
                    unsigned word = le16(bytes + i * 2);
                    samples[i] = (int16_t)(word < 32768 ? (int)word : (int)word - 65536);
                }
                bs_rx_read(rx, samples, n / 2);
                left -= (uint32_t)n;
            }
        } else {
            /* Consume unknown chunks in bounded storage and detect truncated skipped data. */
            unsigned char discard[4096];
            for (uint32_t left = size; left;) {
                size_t n = left < sizeof discard ? left : sizeof discard;
                if (fread(discard, 1, n, f) != n) {
                    goto done;
                }
                left -= (uint32_t)n;
            }
        }
        if ((size & 1) && fgetc(f) == EOF) {
            goto done;
        }
        remaining -= padded;
    }
    ok = fmt && data && remaining == 0;
done:
    fclose(f);
    if (rx->parser->position) {
        bs_parser_abort(rx->parser, "End of WAV input; incomplete frame");
    }
    if (!ok) {
        snprintf(error, cap, "%s", reason);
    }
    return ok;
}
