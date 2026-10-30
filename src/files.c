#include "files.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
static bool load_file(BsFile *file, const char *path, char *error, size_t capacity) {
    FILE *input = fopen(path, "rb");
    if (!input) {
        snprintf(error, capacity, "Cannot open file: %s", strerror(errno));
        return false;
    }
    size_t n = fread(file->bytes, 1, sizeof file->bytes, input);
    int extra = fgetc(input);
    bool ok = !ferror(input) && extra == EOF;
    if (fclose(input)) {
        ok = false;
    }
    if (!ok) {
        snprintf(error, capacity, "Cannot read file, or file exceeds 1 MiB (1048576 bytes).");
        return false;
    }
    memset(file->bytes + n, 0, sizeof file->bytes - n);
    const char *base = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    if (!*base || strlen(base) > BS_FILE_NAME) {
        snprintf(error, capacity, "File name must contain 1-96 bytes.");
        return false;
    }
    memset(&file->info, 0, sizeof file->info);
    for (size_t i = 0; base[i]; ++i) {
        unsigned char c = (unsigned char)base[i];
        file->info.name[i] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '_' || (i && (c == '.' || c == '-')))
                                 ? (char)c
                                 : '_';
    }
    file->info.size = (uint32_t)n;
    file->info.checksum = bs_crc32(file->bytes, n);
    static uint32_t sequence;
    static bool seeded;
    if (!seeded) {
        struct timespec now = {0};
        timespec_get(&now, TIME_UTC);
        sequence = (uint32_t)now.tv_sec ^ (uint32_t)now.tv_nsec;
        seeded = true;
    }
    file->info.id = sequence++;
    return true;
}
bool bs_file_load(BsFile *file, const char *path, char *error, size_t capacity) {
    BsFile *candidate = malloc(sizeof *candidate);
    if (!candidate) {
        snprintf(error, capacity, "Not enough memory to prepare file.");
        return false;
    }
    bool ok = load_file(candidate, path, error, capacity);
    if (ok) {
        *file = *candidate;
    }
    free(candidate);
    return ok;
}
bool bs_file_frame(const BsFile *file, const char *call, size_t index, BsFrame *frame) {
    if (file->info.size > BS_FILE_LIMIT || index >= bs_file_chunk_count(file->info.size)) {
        return false;
    }
    BsFileChunk chunk = file->info;
    chunk.index = (uint16_t)index;
    size_t remaining = file->info.size - index * BS_FILE_CHUNK;
    chunk.length = (uint16_t)(remaining > BS_FILE_CHUNK ? BS_FILE_CHUNK : remaining);
    memcpy(chunk.data, file->bytes + index * BS_FILE_CHUNK, chunk.length);
    return bs_frame_build_file(frame, call, &chunk);
}
size_t bs_file_bits(const BsFile *file) {
    if (file->info.size > BS_FILE_LIMIT || !bs_file_name_valid(file->info.name)) {
        return 0;
    }
    /* Every frame has the same metadata and four padding bits. */
    return bs_file_chunk_count(file->info.size) *
               (64 + 36 + 16 + BS_FILE_HEADER_SIZE * 8 + strlen(file->info.name) * 8 + 4 + 16) +
           file->info.size * 8;
}
BsFileResult bs_file_accept(BsFileReception *rx, const BsEvent *event) {
    if (event->kind != BS_VERIFIED || !event->is_file) {
        return BS_FILE_IGNORED;
    }
    const BsFileChunk *chunk = &event->file;
    if (!bs_file_chunk_valid(chunk)) {
        return BS_FILE_ERROR;
    }
    bool same = rx->active && rx->file.info.id == chunk->id && !strcmp(rx->call, event->call);
    if (rx->active && !same && (!rx->complete || !rx->saved)) {
        return BS_FILE_BUSY;
    }
    if (!same) {
        memset(rx, 0, sizeof *rx);
        rx->active = true;
        rx->file.info = *chunk;
        memcpy(rx->call, event->call, sizeof rx->call);
    } else if (rx->file.info.size != chunk->size || rx->file.info.checksum != chunk->checksum ||
               strcmp(rx->file.info.name, chunk->name)) {
        return BS_FILE_ERROR;
    }
    size_t offset = chunk->index * (size_t)BS_FILE_CHUNK;
    if (rx->present[chunk->index]) {
        if (memcmp(rx->file.bytes + offset, chunk->data, chunk->length)) {
            return BS_FILE_ERROR;
        }
        return rx->complete && !rx->saved ? BS_FILE_COMPLETE : BS_FILE_DUPLICATE;
    }
    memcpy(rx->file.bytes + offset, chunk->data, chunk->length);
    rx->present[chunk->index] = 1;
    ++rx->received;
    if (rx->received != bs_file_chunk_count(chunk->size)) {
        return BS_FILE_PROGRESS;
    }
    if (bs_crc32(rx->file.bytes, chunk->size) != chunk->checksum) {
        memset(rx, 0, sizeof *rx);
        return BS_FILE_ERROR;
    }
    rx->complete = true;
    return BS_FILE_COMPLETE;
}
bool bs_file_save(const BsFileReception *rx, const char *directory, char *path, size_t capacity) {
    if (!rx->complete || !bs_file_name_valid(rx->file.info.name) || !*directory) {
        return false;
    }
#ifdef _WIN32
    int result = _mkdir(directory);
#else
    int result = mkdir(directory, 0700);
#endif
    if (result && errno != EEXIST) {
        return false;
    }
    for (unsigned suffix = 0; suffix < 10000; ++suffix) {
        int n = snprintf(path, capacity, "%s/%08X-%u-%s", directory, (unsigned)rx->file.info.id,
                         suffix, rx->file.info.name);
        if (n < 0 || (size_t)n >= capacity) {
            return false;
        }
        FILE *output = fopen(path, "wbx");
        if (!output) {
            if (errno == EEXIST) {
                continue;
            }
            return false;
        }
        bool ok = fwrite(rx->file.bytes, 1, rx->file.info.size, output) == rx->file.info.size;
        if (fclose(output)) {
            ok = false;
        }
        if (!ok) {
            remove(path);
        }
        return ok;
    }
    return false;
}
bool bs_file_report(const BsFileReception *rx, size_t page, BsRepairRequest *report) {
    size_t count = bs_file_chunk_count(rx->file.info.size);
    if (!rx->active || page >= (count + BS_REPAIR_WINDOW - 1) / BS_REPAIR_WINDOW ||
        (rx->complete && page)) {
        return false;
    }
    memset(report, 0, sizeof *report);
    memcpy(report->target, rx->call, sizeof report->target);
    report->id = rx->file.info.id;
    report->base = (uint16_t)(page * BS_REPAIR_WINDOW);
    report->complete = rx->complete;
    if (!report->complete) {
        for (size_t i = 0; i < BS_REPAIR_WINDOW && report->base + i < count; ++i) {
            if (!rx->present[report->base + i]) {
                report->missing[i / 8] |= (uint8_t)(1u << (7 - i % 8));
            }
        }
    }
    return bs_repair_valid(report);
}
bool bs_file_apply_report(const BsFile *file, const char *call, const BsEvent *event,
                          uint8_t requested[BS_FILE_BLOCKS]) {
    const BsRepairRequest *r = &event->repair;
    if (event->kind != BS_VERIFIED || !event->is_repair || !bs_repair_valid(r) ||
        r->id != file->info.id || strcmp(r->target, call)) {
        return false;
    }
    size_t count = bs_file_chunk_count(file->info.size);
    for (size_t i = 0; i < BS_REPAIR_WINDOW; ++i) {
        if ((r->missing[i / 8] & (1u << (7 - i % 8))) && r->base + i >= count) {
            return false;
        }
    }
    if (r->complete) {
        memset(requested, 0, BS_FILE_BLOCKS);
    } else {
        for (size_t i = 0; i < BS_REPAIR_WINDOW && r->base + i < count; ++i) {
            requested[r->base + i] |= (r->missing[i / 8] >> (7 - i % 8)) & 1;
        }
    }
    return true;
}
