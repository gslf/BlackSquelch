#include "protocol.h"
#include <string.h>
const char bs_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,?!:;-/()";
int bs_code(unsigned char c) {
    if (c >= 'a' && c <= 'z') {
        c = (unsigned char)(c - 'a' + 'A');
    }
    const char *found = c ? strchr(bs_alphabet, c) : NULL;
    return found ? (int)(found - bs_alphabet) : -1;
}
bool bs_normalize(char *s, bool callsign, size_t *bad_index) {
    size_t n = strlen(s);
    bool valid = callsign ? n == BS_CALL_CHARS : n >= 1 && n <= BS_MAX_TEXT;
    if (bad_index) {
        *bad_index = n;
    }
    for (size_t i = 0; i < n; ++i) {
        if (s[i] >= 'a' && s[i] <= 'z') {
            s[i] = (char)(s[i] - 'a' + 'A');
        }
        int code = bs_code((unsigned char)s[i]);
        if (code < 0 || code > (callsign ? 35 : 46)) {
            if (bad_index && *bad_index == n) {
                *bad_index = i;
            }
            valid = false;
        }
    }
    return valid;
}
static uint16_t crc_byte(uint16_t crc, uint8_t byte) {
    crc ^= (uint16_t)((unsigned)byte << 8);
    for (int i = 0; i < 8; ++i) {
        crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}
uint16_t bs_crc16(const uint8_t *bytes, size_t count) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < count; ++i) {
        crc = crc_byte(crc, bytes[i]);
    }
    return crc;
}
static void put(BsFrame *f, uint32_t value, unsigned width) {
    while (width) {
        f->bits[f->count++] = (uint8_t)((value >> --width) & 1);
    }
}
bool bs_frame_build(BsFrame *f, const char *call, const char *text) {
    char c[BS_CALL_CHARS + 1], t[BS_MAX_TEXT + 1];
    f->count = 0;
    if (strlen(call) > BS_CALL_CHARS || strlen(text) > BS_MAX_TEXT) {
        return false;
    }
    strcpy(c, call);
    strcpy(t, text);
    if (!bs_normalize(c, true, NULL) || !bs_normalize(t, false, NULL)) {
        return false;
    }
    put(f, 0xaaaaaaaa, 32);
    put(f, BS_SYNC, 32);
    for (size_t i = 0; c[i]; ++i) {
        put(f, (uint32_t)bs_code((unsigned char)c[i]), 6);
    }
    put(f, (uint32_t)strlen(t), 16);
    for (size_t i = 0; t[i]; ++i) {
        put(f, (uint32_t)bs_code((unsigned char)t[i]), 6);
    }
    while (f->count % 8) {
        put(f, 0, 1);
    }
    uint16_t crc = 0xffff;
    for (size_t i = 64; i < f->count; i += 8) {
        uint8_t byte = 0;
        for (unsigned j = 0; j < 8; ++j) {
            byte = (uint8_t)((byte << 1) | f->bits[i + j]);
        }
        crc = crc_byte(crc, byte);
    }
    put(f, crc, 16);
    return true;
}
const char *bs_field_name(BsField f) {
    static const char *names[] = {"PREAMBLE", "SYNC",    "CALL", "TEXT LENGTH",
                                  "TEXT",     "PADDING", "CRC"};
    return names[f];
}
static void emit(BsParser *p, BsEventKind kind, float confidence, const char *detail) {
    if (!p->event) {
        return;
    }
    BsEvent e = {0};
    e.kind = kind;
    e.field = p->field;
    e.position = p->position;
    e.index = p->index;
    e.expected = p->field == BS_CALL ? BS_CALL_CHARS : p->text_length;
    e.width = p->width;
    e.received = p->received;
    e.value = p->value;
    e.bit_rate = p->bit_rate;
    e.confidence = confidence;
    e.uncertain = p->uncertain;
    e.semantic_error = p->invalid;
    e.calculated_crc = p->crc;
    e.detail = detail;
    memcpy(e.bits, p->bits, sizeof e.bits);
    memcpy(e.call, p->call, sizeof e.call);
    memcpy(e.text, p->text, sizeof e.text);
    p->event(p->context, &e);
}
static void field(BsParser *p, BsField f, unsigned width) {
    p->field = f;
    p->width = width;
    p->received = 0;
    p->value = 0;
    p->uncertain = false;
    memset(p->bits, '-', width);
    p->bits[width] = 0;
}
void bs_parser_init(BsParser *p, BsEventFn fn, void *context) {
    memset(p, 0, sizeof *p);
    p->event = fn;
    p->context = context;
    p->crc = 0xffff;
    field(p, BS_PREAMBLE, 32);
}
static void reset(BsParser *p) {
    bs_parser_init(p, p->event, p->context);
    emit(p, BS_LISTENING, 0, "Ready for a new preamble");
}
bool bs_parser_active(const BsParser *p) {
    return p->field != BS_PREAMBLE;
}
void bs_parser_abort(BsParser *p, const char *reason) {
    if (p->position || p->received) {
        emit(p, BS_INTERRUPTED, 0, reason);
    }
    reset(p);
}
static void invalid(BsParser *p, const char *reason) {
    emit(p, BS_INVALID, 0, reason);
    reset(p);
}
void bs_parser_bit(BsParser *p, int bit, float confidence) {
    bit = !!bit;
    if (p->field == BS_PREAMBLE) {
        /* A fresh candidate must begin with 1; repeated tones cannot acquire a frame. */
        if (bit != (p->received % 2 == 0)) {
            field(p, BS_PREAMBLE, 32);
            p->position = 0;
            if (!bit) {
                return;
            }
        }
    }
    p->bits[p->received++] = (char)('0' + bit);
    p->value = (p->value << 1) | (unsigned)bit;
    ++p->position;
    p->uncertain |= confidence < 0.5f;
    if (p->field >= BS_CALL && p->field <= BS_PADDING) {
        p->byte = (uint8_t)((p->byte << 1) | bit);
        if (++p->body_bits % 8 == 0) {
            p->crc = crc_byte(p->crc, p->byte);
        }
    }
    emit(p, BS_PROGRESS, confidence,
         p->field == BS_PREAMBLE ? "Unsynchronized candidate" : "Provisional");
    if (p->received != p->width) {
        return;
    }
    /* Update previews only after a complete character. Reserved codes never index the alphabet. */
    if (p->field == BS_CALL || p->field == BS_TEXT) {
        unsigned limit = p->field == BS_CALL ? 35 : 46;
        if (p->value > limit) {
            p->invalid = true;
        } else if (p->field == BS_CALL) {
            p->call[p->index] = bs_alphabet[p->value];
        } else {
            p->text[p->index] = bs_alphabet[p->value];
        }
        if (p->value > limit && p->field == BS_TEXT) {
            p->text[p->index] = '#';
        }
    }
    emit(p, BS_FIELD_DONE, confidence, "Complete field; provisional until CRC");
    switch (p->field) {
    case BS_PREAMBLE:
        field(p, BS_SYNC_FIELD, 32);
        break;
    case BS_SYNC_FIELD:
        if (p->value != BS_SYNC) {
            invalid(p, "Synchronization mismatch");
            break;
        }
        p->index = 0;
        field(p, BS_CALL, 6);
        break;
    case BS_CALL:
        if (p->invalid) {
            invalid(p, "INVALID CODE in callsign");
            break;
        }
        if (++p->index == BS_CALL_CHARS) {
            p->index = 0;
            field(p, BS_TEXT_LENGTH, 16);
        } else {
            field(p, BS_CALL, 6);
        }
        break;
    case BS_TEXT_LENGTH:
        if (!p->value || p->value > BS_MAX_TEXT) {
            invalid(p, "Invalid text length");
            break;
        }
        p->text_length = p->value;
        field(p, BS_TEXT, 6);
        break;
    case BS_TEXT:
        if (++p->index == p->text_length) {
            unsigned padding = (unsigned)((8 - p->body_bits % 8) % 8);
            field(p, padding ? BS_PADDING : BS_CRC, padding ? padding : 16);
        } else {
            field(p, BS_TEXT, 6);
        }
        break;
    case BS_PADDING:
        if (p->value) {
            p->invalid = true;
        }
        field(p, BS_CRC, 16);
        break;
    case BS_CRC:
        emit(p,
             p->invalid           ? BS_INVALID
             : p->value != p->crc ? BS_BAD_CRC
                                  : BS_VERIFIED,
             confidence,
             p->invalid           ? "Invalid character or nonzero padding"
             : p->value != p->crc ? "CRC mismatch; unverified"
                                  : "Verified");
        reset(p);
        break;
    }
}
