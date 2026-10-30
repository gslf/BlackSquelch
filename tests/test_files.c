#include "files.h"
#include "modem.h"
#include "wav.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define remove_directory _rmdir
#else
#include <unistd.h>
#define remove_directory rmdir
#endif

typedef struct {
    BsFileReception rx;
    unsigned verified, bad_crc, invalid, complete;
    BsFileResult last;
} Result;
static void collect(void *context, const BsEvent *event) {
    Result *r = context;
    if (event->kind == BS_VERIFIED) {
        ++r->verified;
        r->last = bs_file_accept(&r->rx, event);
        if (r->last == BS_FILE_COMPLETE) {
            ++r->complete;
        }
    } else if (event->kind == BS_BAD_CRC) {
        ++r->bad_crc;
    } else if (event->kind == BS_INVALID) {
        ++r->invalid;
    }
}
static void feed(BsParser *parser, const BsFrame *frame) {
    for (size_t i = 0; i < frame->count; ++i) {
        bs_parser_bit(parser, frame->bits[i], 1);
    }
}
static BsFile *sample(uint32_t size, const char *name) {
    BsFile *file = calloc(1, sizeof *file);
    assert(file && size <= BS_FILE_LIMIT);
    file->info.id = 0x12345678;
    file->info.size = size;
    strcpy(file->info.name, name);
    for (size_t i = 0; i < size; ++i) {
        file->bytes[i] = (uint8_t)i;
    }
    file->info.checksum = bs_crc32(file->bytes, size);
    return file;
}
static void change(BsFrame *frame, size_t position, uint32_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) {
        frame->bits[position + i] = (uint8_t)((value >> (width - i - 1)) & 1);
    }
}
static void repair_crc(BsFrame *frame) {
    uint8_t bytes[BS_MAX_BITS / 8] = {0};
    size_t n = (frame->count - 64 - 16) / 8;
    for (size_t i = 0; i < n * 8; ++i) {
        bytes[i / 8] = (uint8_t)((bytes[i / 8] << 1) | frame->bits[64 + i]);
    }
    change(frame, frame->count - 16, bs_crc16(bytes, n), 16);
}
static void test_protocol(void) {
    assert(bs_crc32((const uint8_t *)"123456789", 9) == UINT32_C(0xCBF43926));
    assert(bs_crc32(NULL, 0) == 0);
    BsFile *file = sample(129, "Image.png");
    Result *r = calloc(1, sizeof *r);
    assert(r);
    BsParser parser;
    bs_parser_init(&parser, collect, r);
    BsFrame frame;
    assert(bs_file_frame(file, "iu2abc", 2, &frame));
    feed(&parser, &frame); /* Last chunk first, no implicit completion. */
    assert(r->verified == 1 && !r->rx.complete && r->rx.received == 1);
    feed(&parser, &frame);
    assert(r->last == BS_FILE_DUPLICATE && r->rx.received == 1);
    assert(bs_file_frame(file, "IU2ABC", 0, &frame));
    BsFrame corrupt = frame;
    corrupt.bits[frame.count - 21] ^= 1;
    feed(&parser, &corrupt);
    assert(r->bad_crc == 1 && r->rx.received == 1);
    feed(&parser, &frame);
    assert(r->rx.received == 2 && !r->rx.complete);
    assert(bs_file_frame(file, "IU2ABC", 1, &frame));
    for (size_t i = 0; i < frame.count / 2; ++i) {
        bs_parser_bit(&parser, frame.bits[i], 1);
    }
    bs_parser_abort(&parser, "Truncated chunk");
    assert(!r->rx.complete && r->rx.received == 2);
    feed(&parser, &frame);
    assert(r->rx.complete && r->complete == 1 && !strcmp(r->rx.call, "IU2ABC"));
    assert(!memcmp(r->rx.file.bytes, file->bytes, file->info.size));
    r->rx.saved = true;
    feed(&parser, &frame);
    assert(r->last == BS_FILE_DUPLICATE && r->complete == 1);
    memset(r, 0, sizeof *r);
    file->info.checksum ^= 1; /* Frame CRC remains valid, full-file CRC must reject it. */
    for (size_t i = 0; i < 3; ++i) {
        assert(bs_file_frame(file, "IU2ABC", i, &frame));
        feed(&parser, &frame);
    }
    assert(r->last == BS_FILE_ERROR && !r->rx.complete && !r->rx.active);
    file->info.checksum ^= 1;
    /* Malformed binary lengths, version, index and filename must reject before indexing. */
    assert(bs_file_frame(file, "IU2ABC", 0, &frame));
    const struct {
        size_t position;
        uint32_t value;
        unsigned width;
    } cases[] = {
        {116, 3, 8},
        {116 + 5 * 8, BS_FILE_LIMIT + 1, 32},
        {116 + 13 * 8, 65535, 16},
        {116 + 15 * 8, 0, 8},
        {116 + 15 * 8, BS_FILE_NAME + 1, 8},
        {116 + 16 * 8, 257, 16},
        {116 + 18 * 8, '/', 8},
        {116 + 18 * 8, 0, 8},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        unsigned before = r->invalid;
        corrupt = frame;
        change(&corrupt, cases[i].position, cases[i].value, cases[i].width);
        repair_crc(&corrupt);
        feed(&parser, &corrupt);
        assert(r->invalid > before && !r->rx.complete);
        bs_parser_abort(&parser, "Next test");
    }
    assert(!bs_file_frame(file, "IU2ABC", 3, &frame));
    const char *unsafe[] = {"", "../evil", "/tmp/evil", "a\\evil", ".hidden", "bad\033name", "A:B"};
    for (size_t i = 0; i < sizeof unsafe / sizeof unsafe[0]; ++i) {
        strcpy(file->info.name, unsafe[i]);
        assert(!bs_file_frame(file, "IU2ABC", 0, &frame));
    }
    /* Empty files omit the data field; maximum names still need a terminator. */
    memset(file->info.name, 'N', BS_FILE_NAME);
    file->info.name[BS_FILE_NAME] = 0;
    file->info.size = 0;
    file->info.checksum = 0;
    memset(r, 0, sizeof *r);
    assert(bs_file_frame(file, "IU2ABC", 0, &frame));
    feed(&parser, &frame);
    assert(r->rx.complete && r->complete == 1 && r->rx.file.info.size == 0);
    assert(strlen(r->rx.file.info.name) == BS_FILE_NAME);
    free(file);
    free(r);
    puts("PASS file protocol: binary bytes, reordered/duplicate/lost/corrupt chunks, full CRC, "
         "malformed fields");
}
static BsEvent event_from(const BsFile *file, size_t index) {
    BsEvent event = {.kind = BS_VERIFIED, .is_file = true, .call = "IU2ABC"};
    event.file = file->info;
    event.file.index = (uint16_t)index;
    size_t remaining = file->info.size - index * BS_FILE_CHUNK;
    event.file.length = (uint16_t)(remaining > BS_FILE_CHUNK ? BS_FILE_CHUNK : remaining);
    memcpy(event.file.data, file->bytes + index * BS_FILE_CHUNK, event.file.length);
    return event;
}
static void test_assembly(void) {
    const uint32_t sizes[] = {0, 1, 255, 256, 257, BS_FILE_LIMIT};
    BsFileReception *rx = calloc(1, sizeof *rx);
    assert(rx);
    for (size_t j = 0; j < sizeof sizes / sizeof sizes[0]; ++j) {
        BsFile *file = sample(sizes[j], "binary.bin");
        memset(rx, 0, sizeof *rx);
        size_t bits = 0;
        for (size_t i = bs_file_chunk_count(sizes[j]); i > 0; --i) {
            BsFrame frame;
            assert(bs_file_frame(file, "IU2ABC", i - 1, &frame));
            bits += frame.count;
            BsEvent event = event_from(file, i - 1);
            assert(bs_file_accept(rx, &event) == (i == 1 ? BS_FILE_COMPLETE : BS_FILE_PROGRESS));
        }
        assert(bits == bs_file_bits(file) && rx->complete);
        assert(!memcmp(rx->file.bytes, file->bytes, sizes[j]));
        free(file);
    }
    memset(rx, 0, sizeof *rx);
    BsFile *file = sample(100, "a.txt");
    BsEvent event = event_from(file, 0);
    assert(bs_file_accept(rx, &event) == BS_FILE_PROGRESS);
    event.kind = BS_BAD_CRC;
    assert(bs_file_accept(rx, &event) == BS_FILE_IGNORED);
    event.kind = BS_VERIFIED;
    event.file.id++;
    assert(bs_file_accept(rx, &event) == BS_FILE_BUSY);
    event.file.id--;
    event.call[0] = 'A';
    assert(bs_file_accept(rx, &event) == BS_FILE_BUSY);
    event.call[0] = 'I';
    event.file.size++;
    assert(bs_file_accept(rx, &event) == BS_FILE_ERROR);
    event.file.size--;
    event.file.data[0] ^= 1;
    assert(bs_file_accept(rx, &event) == BS_FILE_ERROR);
    event = event_from(file, 1);
    assert(bs_file_accept(rx, &event) == BS_FILE_COMPLETE);
    /* Unwritten verified data remains available until saved or explicitly discarded. */
    event.file.id++;
    assert(bs_file_accept(rx, &event) == BS_FILE_BUSY);
    rx->saved = true;
    assert(bs_file_accept(rx, &event) == BS_FILE_PROGRESS);
    free(file);
    free(rx);
    puts("PASS file assembly: empty/boundary/max sizes, bounded storage, transfer isolation and "
         "exact duration");
}
static void test_disk(void) {
    const char *input = "test-Mixed Case.txt";
    const uint8_t bytes[] = {0, 255, 13, 10, 'a', 'Z', 0xC3, 0xA8};
    FILE *f = fopen(input, "wb");
    assert(f && fwrite(bytes, 1, sizeof bytes, f) == sizeof bytes);
    assert(!fclose(f));
    BsFile *file = calloc(1, sizeof *file);
    BsFileReception *rx = calloc(1, sizeof *rx);
    assert(file && rx);
    char error[256], path[512], other[512];
    assert(bs_file_load(file, input, error, sizeof error));
    assert(file->info.size == sizeof bytes && !strcmp(file->info.name, "test-Mixed_Case.txt"));
    assert(!memcmp(file->bytes, bytes, sizeof bytes));
    uint32_t id = file->info.id;
    assert(!bs_file_load(file, "no-such-file", error, sizeof error));
    assert(file->info.id == id && !memcmp(file->bytes, bytes, sizeof bytes));
    assert(!bs_file_save(rx, "test-file-reception", path, sizeof path));
    BsEvent event = event_from(file, 0);
    assert(bs_file_accept(rx, &event) == BS_FILE_COMPLETE);
    assert(!bs_file_save(rx, input, other, sizeof other));
    assert(rx->complete && !rx->saved && !memcmp(rx->file.bytes, bytes, sizeof bytes));
    assert(bs_file_save(rx, "test-file-reception", path, sizeof path));
    assert(bs_file_save(rx, "test-file-reception", other, sizeof other));
    assert(strcmp(path, other)); /* Existing files are never overwritten. */
    assert(!bs_file_save(rx, input, other, sizeof other)); /* Not a directory. */
    assert(!bs_file_save(rx, "test-file-reception", other, 4));
    f = fopen(path, "rb");
    uint8_t received[sizeof bytes];
    assert(f && fread(received, 1, sizeof received, f) == sizeof received && fgetc(f) == EOF);
    assert(!fclose(f) && !memcmp(bytes, received, sizeof bytes));
    remove(path);
    snprintf(other, sizeof other, "test-file-reception/%08X-1-%s", (unsigned)id, file->info.name);
    remove(other);
    assert(!remove_directory("test-file-reception"));
    f = fopen(input, "wb");
    assert(f);
    for (size_t i = 0; i <= BS_FILE_LIMIT; ++i) {
        assert(fputc(0, f) != EOF);
    }
    assert(!fclose(f));
    assert(!bs_file_load(file, input, error, sizeof error) && file->info.id == id);
    remove(input);
    free(file);
    free(rx);
    puts("PASS file storage: arbitrary binary content, safe names, size limit, failed reads and "
         "exclusive saves");
}
static void stream(BsReceiver *rx, const BsFrame *frame, unsigned rate) {
    BsTransmitter tx;
    assert(bs_tx_init(&tx, frame, 0.5, rate));
    int16_t block[BS_BLOCK];
    size_t n;
    while ((n = bs_tx_read(&tx, block, BS_BLOCK))) {
        bs_rx_read(rx, block, n);
    }
}
static bool source(void *context, size_t index, BsFrame *frame) {
    return bs_file_frame(context, "IU2ABC", index, frame);
}
static void test_audio(void) {
    BsFile *file = sample(257, "Image.png");
    /* A real PNG header followed by all possible byte values, crossing chunk boundaries. */
    const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    memcpy(file->bytes, signature, sizeof signature);
    file->info.checksum = bs_crc32(file->bytes, file->info.size);
    Result *r = calloc(1, sizeof *r);
    assert(r);
    for (unsigned i = 0; i < 2; ++i) {
        memset(r, 0, sizeof *r);
        BsParser parser;
        BsReceiver receiver;
        bs_parser_init(&parser, collect, r);
        bs_rx_init(&receiver, &parser);
        for (size_t index = 0; index < bs_file_chunk_count(file->info.size); ++index) {
            BsFrame frame;
            assert(bs_file_frame(file, "IU2ABC", index, &frame));
            stream(&receiver, &frame, (i ? 16 : 8));
        }
        assert(r->complete == 1 && r->rx.complete &&
               r->verified == bs_file_chunk_count(file->info.size));
        assert(!memcmp(r->rx.file.bytes, file->bytes, file->info.size));
        BsFrame text;
        assert(bs_frame_build(&text, "IU2XYZ", "TEXT STILL WORKS"));
        stream(&receiver, &text, (i ? 16 : 8));
        assert(r->verified == bs_file_chunk_count(file->info.size) + 1 && r->rx.complete);
    }
    const char *path = "test-file-roundtrip.wav";
    assert(bs_wav_write_frames(path, bs_file_chunk_count(file->info.size), source, file, 0.5, 16));
    memset(r, 0, sizeof *r);
    BsParser parser;
    BsReceiver receiver;
    bs_parser_init(&parser, collect, r);
    bs_rx_init(&receiver, &parser);
    char error[256];
    assert(bs_wav_decode(path, &receiver, error, sizeof error));
    assert(r->complete == 1 && !memcmp(r->rx.file.bytes, file->bytes, file->info.size));
    remove(path);
    /* RIFF overflow is checked before creating a giant WAV. */
    file->info.size = BS_FILE_LIMIT;
    assert(!bs_wav_write_frames(path, bs_file_chunk_count(file->info.size), source, file, 0.5, 8));
    free(file);
    free(r);
    puts("PASS file audio: consecutive binary chunks at both MFSK profiles, text coexistence and "
         "offline "
         "roundtrip");
}
typedef struct {
    BsEvent event;
    unsigned verified, bad_crc;
} ReportResult;
static void report_event(void *context, const BsEvent *event) {
    ReportResult *result = context;
    if (event->kind == BS_VERIFIED && event->is_repair) {
        result->event = *event;
        ++result->verified;
    } else if (event->kind == BS_BAD_CRC) {
        ++result->bad_crc;
    }
}
static void test_repair_reports(void) {
    BsFile *file = sample(300 * BS_FILE_CHUNK + 1, "repair.bin");
    BsFileReception *rx = calloc(1, sizeof *rx);
    assert(rx);
    uint8_t requested[BS_FILE_BLOCKS] = {0};
    BsEvent event = event_from(file, 1);
    assert(bs_file_accept(rx, &event) == BS_FILE_PROGRESS);
    BsRepairRequest request;
    assert(bs_file_report(rx, 0, &request));
    assert(request.base == 0 && request.missing[0] == 0xBF && !request.complete);
    BsFrame frame;
    assert(bs_frame_build_repair(&frame, "IU2XYZ", &request) && frame.count == 496);
    BsParser parser;
    ReportResult report = {0};
    bs_parser_init(&parser, report_event, &report);
    feed(&parser, &frame);
    assert(report.verified == 1 && !strcmp(report.event.call, "IU2XYZ"));
    assert(bs_file_apply_report(file, "IU2ABC", &report.event, requested));
    assert(requested[0] && !requested[1] && requested[255] && !requested[256]);
    assert(!bs_file_apply_report(file, "IU2DEF", &report.event, requested));
    report.event.repair.id++;
    assert(!bs_file_apply_report(file, "IU2ABC", &report.event, requested));
    report.event.repair.id--;
    assert(bs_file_report(rx, 1, &request) && request.base == 256);
    assert(bs_frame_build_repair(&frame, "IU2XYZ", &request));
    feed(&parser, &frame);
    assert(bs_file_apply_report(file, "IU2ABC", &report.event, requested));
    assert(requested[300] && !requested[301]);
    report.event.repair.missing[31] |= 1; /* Requests a block beyond this file. */
    memset(requested, 0, sizeof requested);
    assert(!bs_file_apply_report(file, "IU2ABC", &report.event, requested));
    assert(!requested[256]); /* Reject transactionally, without partial changes. */
    unsigned before = report.verified;
    frame.bits[frame.count - 1] ^= 1;
    feed(&parser, &frame);
    assert(report.verified == before && report.bad_crc == 1);
    for (size_t i = 0; i < bs_file_chunk_count(file->info.size); ++i) {
        event = event_from(file, i);
        bs_file_accept(rx, &event);
    }
    assert(rx->complete && bs_file_report(rx, 0, &request) && request.complete);
    assert(!bs_file_report(rx, 1, &request));
    assert(bs_frame_build_repair(&frame, "IU2XYZ", &request));
    feed(&parser, &frame);
    requested[1] = 1;
    assert(bs_file_apply_report(file, "IU2ABC", &report.event, requested) && !requested[1]);
    request.complete = false;
    assert(!bs_frame_build_repair(&frame, "IU2XYZ", &request)); /* Empty request. */
    free(file);
    free(rx);
    puts("PASS selective reports: multiple bitmap pages, CRC, target/ID, bounds, transactional "
         "rejection and receipt");
}
int main(void) {
    test_protocol();
    test_repair_reports();
    test_assembly();
    test_disk();
    test_audio();
    return 0;
}
