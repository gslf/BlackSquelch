#ifndef BS_PROTOCOL_H
#define BS_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BS_MAX_TEXT 512
#define BS_CALL_CHARS 6
#define BS_MAX_BITS 3208
#define BS_SYNC UINT32_C(0x1ACFFC1D)
extern const char bs_alphabet[];
typedef struct {
    uint8_t bits[BS_MAX_BITS];
    size_t count;
} BsFrame;
typedef enum {
    BS_PREAMBLE,
    BS_SYNC_FIELD,
    BS_CALL,
    BS_TEXT_LENGTH,
    BS_TEXT,
    BS_PADDING,
    BS_CRC
} BsField;
typedef enum {
    BS_RATE_DETECTED,
    BS_PROGRESS,
    BS_FIELD_DONE,
    BS_VERIFIED,
    BS_BAD_CRC,
    BS_INVALID,
    BS_INTERRUPTED,
    BS_LISTENING
} BsEventKind;
typedef struct {
    BsEventKind kind;
    BsField field;
    size_t position, index, expected;
    unsigned width, received;
    uint32_t value;
    unsigned bit_rate;
    double symbol_ms;
    float confidence;
    bool uncertain, semantic_error;
    uint16_t calculated_crc;
    char bits[33], call[BS_CALL_CHARS + 1], text[BS_MAX_TEXT + 1];
    const char *detail;
} BsEvent;
typedef void (*BsEventFn)(void *, const BsEvent *);
typedef struct {
    BsField field;
    unsigned width, received;
    uint32_t value;
    size_t position, index, text_length, body_bits;
    unsigned bit_rate;
    uint16_t crc;
    uint8_t byte;
    bool invalid, uncertain;
    char bits[33], call[BS_CALL_CHARS + 1], text[BS_MAX_TEXT + 1];
    BsEventFn event;
    void *context;
} BsParser;
int bs_code(unsigned char c);
bool bs_normalize(char *s, bool callsign, size_t *bad_index);
uint16_t bs_crc16(const uint8_t *bytes, size_t count);
bool bs_frame_build(BsFrame *frame, const char *call, const char *text);
void bs_parser_init(BsParser *p, BsEventFn event, void *context);
void bs_parser_bit(BsParser *p, int bit, float confidence);
void bs_parser_abort(BsParser *p, const char *reason);
bool bs_parser_active(const BsParser *p);
const char *bs_field_name(BsField field);
#endif
