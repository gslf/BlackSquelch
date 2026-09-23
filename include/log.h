#ifndef BS_LOG_H
#define BS_LOG_H
#include "protocol.h"
#define BS_LOG_ROWS 2048
#define BS_LOG_WIDTH 256
typedef struct {
    char rows[BS_LOG_ROWS][BS_LOG_WIDTH], current[BS_LOG_WIDTH];
    size_t count, head, scroll;
    char call[BS_CALL_CHARS + 1], text[BS_MAX_TEXT + 1];
    char conversation[16][BS_MAX_TEXT + 96];
    size_t messages;
} BsLog;
void bs_log_add(BsLog *log, const char *message);
void bs_log_message(BsLog *log, const char *status, const char *call, const char *text);
void bs_log_event(void *context, const BsEvent *event);
void bs_event_format(const BsEvent *event, char *buffer, size_t capacity);
bool bs_log_export(const BsLog *log, const char *path);
#endif
