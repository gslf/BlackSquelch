#include "protocol.h"
#include <string.h>
_Static_assert(64 + 36 + 16 + BS_FILE_HEADER_SIZE * 8 + BS_FILE_NAME * 8 + BS_FILE_CHUNK * 8 + 4 +
                       16 <=
                   BS_MAX_BITS,
               "Binary frames must fit in BsFrame");
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
uint32_t bs_crc32(const uint8_t *bytes, size_t count) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (unsigned j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xEDB88320) : 0);
        }
    }
    return crc ^ UINT32_MAX;
}
bool bs_file_name_valid(const char *name) {
    size_t n = 0;
    for (; n <= BS_FILE_NAME && name[n]; ++n) {
        unsigned char c = (unsigned char)name[n];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || (n && (c == '-' || c == '.')))) {
            return false;
        }
    }
    return n > 0 && n <= BS_FILE_NAME;
}
size_t bs_file_chunk_count(uint32_t size) {
    return size ? (size - 1) / BS_FILE_CHUNK + 1 : 1;
}
static bool chunk_dimensions_valid(const BsFileChunk *c) {
    if (c->size > BS_FILE_LIMIT || c->index >= bs_file_chunk_count(c->size)) {
        return false;
    }
    uint32_t remaining = c->size - c->index * (uint32_t)BS_FILE_CHUNK;
    return c->length == (remaining > BS_FILE_CHUNK ? BS_FILE_CHUNK : remaining);
}
bool bs_file_chunk_valid(const BsFileChunk *c) {
    return chunk_dimensions_valid(c) && bs_file_name_valid(c->name);
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
bool bs_frame_build_file(BsFrame *f, const char *call, const BsFileChunk *c) {
    char normalized[BS_CALL_CHARS + 1];
    f->count = 0;
    if (strlen(call) != BS_CALL_CHARS || !bs_file_chunk_valid(c)) {
        return false;
    }
    strcpy(normalized, call);
    if (!bs_normalize(normalized, true, NULL)) {
        return false;
    }
    put(f, 0xaaaaaaaa, 32);
    put(f, BS_SYNC, 32);
    for (size_t i = 0; i < BS_CALL_CHARS; ++i) {
        put(f, (uint32_t)bs_code((unsigned char)normalized[i]), 6);
    }
    put(f, BS_FILE_MARKER, 16);
    put(f, 2, 8); /* 64-byte binary extension version. */
    put(f, c->id, 32);
    put(f, c->size, 32);
    put(f, c->checksum, 32);
    put(f, c->index, 16);
    put(f, (uint32_t)strlen(c->name), 8);
    put(f, c->length, 16);
    for (size_t i = 0; c->name[i]; ++i) {
        put(f, (uint8_t)c->name[i], 8);
    }
    for (size_t i = 0; i < c->length; ++i) {
        put(f, c->data[i], 8);
    }
    while (f->count % 8) {
        put(f, 0, 1);
    }
    uint8_t body[BS_MAX_BITS / 8];
    size_t n = 0;
    for (size_t i = 64; i < f->count; i += 8) {
        uint8_t byte = 0;
        for (unsigned j = 0; j < 8; ++j) {
            byte = (uint8_t)((byte << 1) | f->bits[i + j]);
        }
        body[n++] = byte;
    }
    put(f, bs_crc16(body, n), 16);
    return true;
}
bool bs_repair_valid(const BsRepairRequest *r) {
    char target[BS_CALL_CHARS + 1];
    memcpy(target, r->target, sizeof target);
    if (target[BS_CALL_CHARS] || !bs_normalize(target, true, NULL) || strcmp(target, r->target) ||
        r->base % BS_REPAIR_WINDOW || r->base >= BS_FILE_LIMIT / BS_FILE_CHUNK) {
        return false;
    }
    bool any = false;
    for (size_t i = 0; i < sizeof r->missing; ++i) {
        any |= r->missing[i] != 0;
    }
    return r->complete ? !any && !r->base : any;
}
bool bs_frame_build_repair(BsFrame *f, const char *call, const BsRepairRequest *r) {
    char normalized[BS_CALL_CHARS + 1];
    f->count = 0;
    if (strlen(call) != BS_CALL_CHARS || !bs_repair_valid(r)) {
        return false;
    }
    strcpy(normalized, call);
    if (!bs_normalize(normalized, true, NULL)) {
        return false;
    }
    put(f, 0xaaaaaaaa, 32);
    put(f, BS_SYNC, 32);
    for (size_t i = 0; i < BS_CALL_CHARS; ++i) {
        put(f, (uint32_t)bs_code((unsigned char)normalized[i]), 6);
    }
    put(f, BS_REPAIR_MARKER, 16);
    put(f, r->complete ? 2 : 1, 8);
    for (size_t i = 0; i < BS_CALL_CHARS; ++i) {
        put(f, (uint8_t)r->target[i], 8);
    }
    put(f, r->id, 32);
    put(f, r->base, 16);
    for (size_t i = 0; i < sizeof r->missing; ++i) {
        put(f, r->missing[i], 8);
    }
    while (f->count % 8) {
        put(f, 0, 1);
    }
    uint8_t body[BS_MAX_BITS / 8] = {0};
    for (size_t i = 64; i < f->count; ++i) {
        size_t offset = i - 64;
        body[offset / 8] = (uint8_t)((body[offset / 8] << 1) | f->bits[i]);
    }
    put(f, bs_crc16(body, (f->count - 64) / 8), 16);
    return true;
}
const char *bs_field_name(BsField f) {
    static const char *names[] = {"PREAMBLE", "SYNC",        "CALL",      "TEXT LENGTH",
                                  "TEXT",     "FILE HEADER", "FILE NAME", "FILE DATA",
                                  "REPAIR",   "PADDING",     "CRC"};
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
    e.tones = p->tones ? p->tones : 8;
    e.confidence = confidence;
    e.uncertain = p->uncertain;
    e.semantic_error = p->invalid;
    e.is_file = p->is_file;
    e.file = p->file;
    e.is_repair = p->is_repair;
    e.repair = p->repair;
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
static uint32_t read_be(const uint8_t *bytes, unsigned n) {
    uint32_t value = 0;
    for (unsigned i = 0; i < n; ++i) {
        value = (value << 8) | bytes[i];
    }
    return value;
}
static void finish_payload(BsParser *p) {
    unsigned padding = (unsigned)((8 - p->body_bits % 8) % 8);
    field(p, padding ? BS_PADDING : BS_CRC, padding ? padding : 16);
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
        if (p->value == BS_REPAIR_MARKER) {
            p->is_repair = true;
            p->index = 0;
            field(p, BS_REPAIR, 8);
            break;
        }
        if (p->value == BS_FILE_MARKER) {
            p->is_file = true;
            p->index = 0;
            field(p, BS_FILE_HEADER, 8);
            break;
        }
        if (!p->value || p->value > BS_MAX_TEXT) {
            invalid(p, "Invalid text length");
            break;
        }
        p->text_length = p->value;
        field(p, BS_TEXT, 6);
        break;
    case BS_TEXT:
        if (++p->index == p->text_length) {
            finish_payload(p);
        } else {
            field(p, BS_TEXT, 6);
        }
        break;
    case BS_FILE_HEADER:
        p->file_header[p->index++] = (uint8_t)p->value;
        if (p->index == BS_FILE_HEADER_SIZE) {
            const uint8_t *h = p->file_header;
            p->file.id = read_be(h + 1, 4);
            p->file.size = read_be(h + 5, 4);
            p->file.checksum = read_be(h + 9, 4);
            p->file.index = (uint16_t)read_be(h + 13, 2);
            p->name_length = h[15];
            p->file.length = (uint16_t)read_be(h + 16, 2);
            /* Header is 18 bytes: version, ID, size, checksum, index, name length, length. */
            if (h[0] != 2 || !p->name_length || p->name_length > BS_FILE_NAME ||
                !chunk_dimensions_valid(&p->file)) {
                invalid(p, "Invalid binary file header");
                break;
            }
            p->index = 0;
            field(p, BS_FILE_FILENAME, 8);
        } else {
            field(p, BS_FILE_HEADER, 8);
        }
        break;
    case BS_FILE_FILENAME:
        if (!p->value) {
            invalid(p, "Invalid file name byte");
            break;
        }
        p->file.name[p->index++] = (char)p->value;
        if (p->index == p->name_length) {
            if (!bs_file_name_valid(p->file.name)) {
                invalid(p, "Unsafe file name");
                break;
            }
            p->index = 0;
            if (p->file.length) {
                field(p, BS_FILE_DATA, 8);
            } else {
                finish_payload(p);
            }
        } else {
            field(p, BS_FILE_FILENAME, 8);
        }
        break;
    case BS_FILE_DATA:
        p->file.data[p->index++] = (uint8_t)p->value;
        if (p->index == p->file.length) {
            finish_payload(p);
        } else {
            field(p, BS_FILE_DATA, 8);
        }
        break;
    case BS_REPAIR:
        p->repair_bytes[p->index++] = (uint8_t)p->value;
        if (p->index == BS_REPAIR_BYTES) {
            const uint8_t *h = p->repair_bytes;
            p->repair.complete = h[0] == 2;
            memcpy(p->repair.target, h + 1, BS_CALL_CHARS);
            p->repair.id = read_be(h + 7, 4);
            p->repair.base = (uint16_t)read_be(h + 11, 2);
            memcpy(p->repair.missing, h + 13, sizeof p->repair.missing);
            if ((h[0] != 1 && h[0] != 2) || !bs_repair_valid(&p->repair)) {
                invalid(p, "Invalid file repair report");
                break;
            }
            finish_payload(p);
        } else {
            field(p, BS_REPAIR, 8);
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
