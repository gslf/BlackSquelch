#ifndef BS_PROTOCOL_H
#define BS_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BS_MAX_TEXT 512
#define BS_CALL_CHARS 6
#define BS_MAX_BITS 3208
#define BS_SYNC UINT32_C(0x1ACFFC1D)
#define BS_FILE_LIMIT 1048576
#define BS_FILE_CHUNK 64
#define BS_FILE_NAME 96
#define BS_FILE_HEADER_SIZE 18
#define BS_FILE_MARKER 65535
#define BS_REPAIR_MARKER 65534
#define BS_REPAIR_WINDOW 256
#define BS_REPAIR_BYTES 45
typedef struct {
    char target[BS_CALL_CHARS + 1];
    uint32_t id;
    uint16_t base;
    uint8_t missing[BS_REPAIR_WINDOW / 8];
    bool complete;
} BsRepairRequest;
typedef struct {
    uint32_t id, size, checksum;
    uint16_t index, length;
    char name[BS_FILE_NAME + 1];
    uint8_t data[BS_FILE_CHUNK];
} BsFileChunk;
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
    BS_FILE_HEADER,
    BS_FILE_FILENAME,
    BS_FILE_DATA,
    BS_REPAIR,
    BS_PADDING,
    BS_CRC
} BsField;
typedef enum {
    BS_MODE_DETECTED,
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
    unsigned tones;
    double symbol_ms;
    float confidence;
    bool uncertain, semantic_error;
    bool is_file;
    BsFileChunk file;
    bool is_repair;
    BsRepairRequest repair;
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
    unsigned tones;
    uint16_t crc;
    uint8_t byte;
    bool invalid, uncertain;
    bool is_file;
    BsFileChunk file;
    bool is_repair;
    BsRepairRequest repair;
    uint8_t repair_bytes[BS_REPAIR_BYTES];
    uint8_t file_header[BS_FILE_HEADER_SIZE];
    size_t name_length;
    char bits[33], call[BS_CALL_CHARS + 1], text[BS_MAX_TEXT + 1];
    BsEventFn event;
    void *context;
} BsParser;
int bs_code(unsigned char c);
bool bs_normalize(char *s, bool callsign, size_t *bad_index);
uint16_t bs_crc16(const uint8_t *bytes, size_t count);
uint32_t bs_crc32(const uint8_t *bytes, size_t count);
bool bs_file_name_valid(const char *name);
size_t bs_file_chunk_count(uint32_t size);
bool bs_file_chunk_valid(const BsFileChunk *chunk);
bool bs_frame_build_file(BsFrame *frame, const char *call, const BsFileChunk *chunk);
bool bs_repair_valid(const BsRepairRequest *request);
bool bs_frame_build_repair(BsFrame *frame, const char *call, const BsRepairRequest *request);
bool bs_frame_build(BsFrame *frame, const char *call, const char *text);
void bs_parser_init(BsParser *p, BsEventFn event, void *context);
void bs_parser_bit(BsParser *p, int bit, float confidence);
void bs_parser_abort(BsParser *p, const char *reason);
bool bs_parser_active(const BsParser *p);
const char *bs_field_name(BsField field);
#endif
