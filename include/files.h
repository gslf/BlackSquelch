#ifndef BS_FILES_H
#define BS_FILES_H
#include "protocol.h"
#define BS_FILE_BLOCKS (BS_FILE_LIMIT / BS_FILE_CHUNK)
typedef struct {
    BsFileChunk info;
    uint8_t bytes[BS_FILE_LIMIT];
} BsFile;
typedef struct {
    BsFile file;
    char call[BS_CALL_CHARS + 1];
    uint8_t present[BS_FILE_BLOCKS];
    size_t received;
    bool active, complete, saved;
} BsFileReception;
typedef enum {
    BS_FILE_IGNORED,
    BS_FILE_PROGRESS,
    BS_FILE_DUPLICATE,
    BS_FILE_COMPLETE,
    BS_FILE_BUSY,
    BS_FILE_ERROR
} BsFileResult;
bool bs_file_load(BsFile *file, const char *path, char *error, size_t capacity);
bool bs_file_frame(const BsFile *file, const char *call, size_t index, BsFrame *frame);
size_t bs_file_bits(const BsFile *file);
BsFileResult bs_file_accept(BsFileReception *rx, const BsEvent *event);
/* Creates the destination directory if necessary; exclusively creates a new file. */
bool bs_file_save(const BsFileReception *rx, const char *directory, char *path, size_t capacity);
/* One report covers 256 blocks; complete receipts use page zero and an empty bitmap. */
bool bs_file_report(const BsFileReception *rx, size_t page, BsRepairRequest *report);
bool bs_file_apply_report(const BsFile *file, const char *call, const BsEvent *event,
                          uint8_t requested[BS_FILE_BLOCKS]);
#endif
