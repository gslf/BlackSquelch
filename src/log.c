#include "log.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
void bs_log_add(BsLog *l, const char *message) {
    char timestamp[16];
    bs_timestamp(timestamp, sizeof timestamp);
    snprintf(l->rows[l->head], BS_LOG_WIDTH, "[%.8s] %.*s", timestamp, BS_LOG_WIDTH - 12, message);
    l->head = (l->head + 1) % BS_LOG_ROWS;
    if (l->count < BS_LOG_ROWS) {
        ++l->count;
    }
    if (l->scroll && l->scroll < l->count) {
        ++l->scroll;
    }
}
void bs_log_message(BsLog *l, const char *status, const char *call, const char *text) {
    char timestamp[16];
    bs_timestamp(timestamp, sizeof timestamp);
    snprintf(l->conversation[l->messages++ % 16], sizeof l->conversation[0], "[%s] %s %s: %s",
             timestamp, status, call, text);
}
void bs_event_format(const BsEvent *e, char *out, size_t cap) {
    char decoded[100] = "";
    if (e->kind == BS_FIELD_DONE) {
        if (e->field == BS_CALL || e->field == BS_TEXT) {
            if (e->value > (e->field == BS_CALL ? 35u : 46u)) {
                snprintf(decoded, sizeof decoded, " -> INVALID CODE");
            } else if (e->value == 36) {
                snprintf(decoded, sizeof decoded, " -> SPACE");
            } else {
                snprintf(decoded, sizeof decoded, " -> %c", bs_alphabet[e->value]);
            }
        } else if (e->field == BS_CRC) {
            snprintf(decoded, sizeof decoded, " -> received %04X calculated %04X",
                     (unsigned)e->value, e->calculated_crc);
        } else if (e->field == BS_SYNC_FIELD) {
            snprintf(decoded, sizeof decoded, " -> %08X %s", (unsigned)e->value,
                     e->value == BS_SYNC ? "MATCH" : "MISMATCH");
        } else if (e->field == BS_PREAMBLE) {
            snprintf(decoded, sizeof decoded, " -> RECOGNIZED");
        } else if (e->field == BS_PADDING) {
            snprintf(decoded, sizeof decoded, " -> %u bits %s", e->width,
                     e->value ? "NONZERO / INVALID" : "ZERO / VALID");
        } else {
            snprintf(decoded, sizeof decoded, " -> %u", (unsigned)e->value);
        }
    }
    char index[48] = "";
    if (e->field == BS_CALL || e->field == BS_TEXT) {
        snprintf(index, sizeof index, " [%zu/%zu]", e->index + 1, e->expected);
    }
    snprintf(out, cap, "RX MFSK%u bit=%zu %s%s %s%s%s q=%.2f %s", e->tones, e->position,
             bs_field_name(e->field), index, e->bits, decoded, e->uncertain ? " ? UNCERTAIN" : "",
             e->confidence,
             e->field == BS_PREAMBLE && e->kind == BS_PROGRESS ? "UNSYNCHRONIZED" : "PROVISIONAL");
}
void bs_log_event(void *context, const BsEvent *e) {
    BsLog *l = context;
    char line[BS_LOG_WIDTH];
    memcpy(l->call, e->call, sizeof l->call);
    memcpy(l->text, e->text, sizeof l->text);
    if (e->kind == BS_MODE_DETECTED) {
        snprintf(line, sizeof line,
                 "RX MFSK%u LOCKED: 25 symbols/s; FEC, interleaving, calibrated tones", e->tones);
        bs_log_add(l, line);
        snprintf(l->current, sizeof l->current,
                 "RX MFSK%u: receiving protected packet; correcting before delivery...", e->tones);
    } else if (e->kind == BS_PROGRESS) {
        char formatted[BS_LOG_WIDTH], timestamp[16];
        bs_event_format(e, formatted, sizeof formatted);
        bs_timestamp(timestamp, sizeof timestamp);
        snprintf(l->current, sizeof l->current, "[%s] %.230s", timestamp, formatted);
    } else if (e->kind == BS_FIELD_DONE) {
        bs_event_format(e, line, sizeof line);
        bs_log_add(l, line);
        /* Keep the completed group visible until the next bit arrives; a field
           boundary is still reception, not a return to waiting for a preamble. */
        char timestamp[16];
        bs_timestamp(timestamp, sizeof timestamp);
        snprintf(l->current, sizeof l->current, "[%.8s] %.*s", timestamp, BS_LOG_WIDTH - 12, line);
    } else if (e->kind == BS_LISTENING) {
        l->current[0] = 0;
        bs_log_add(l, "RX LISTENING: ready for a new preamble");
    } else {
        if (!e->position && (e->kind == BS_INVALID || e->kind == BS_INTERRUPTED)) {
            snprintf(line, sizeof line, "RX MFSK%u: %s", e->tones, e->detail);
            bs_log_add(l, line);
            l->current[0] = 0;
            return;
        }
        if (e->is_repair) {
            snprintf(line, sizeof line, "RX FILE REPORT %s: %s -> %s, transfer %08X, base %u%s",
                     e->detail, e->call, e->repair.target, (unsigned)e->repair.id,
                     (unsigned)e->repair.base,
                     e->repair.complete ? " COMPLETE" : " MISSING BLOCKS");
            bs_log_add(l, line);
            l->current[0] = 0;
            return;
        }
        if (e->is_file) {
            snprintf(line, sizeof line, "RX FILE %s: %s | block %u/%zu | %s", e->detail,
                     e->file.name, (unsigned)e->file.index + 1, bs_file_chunk_count(e->file.size),
                     e->call);
            bs_log_add(l, line);
            l->current[0] = 0;
            return;
        }
        snprintf(line, sizeof line, "RX %s; last group %s; complete text characters=%zu", e->detail,
                 e->bits, strlen(e->text));
        bs_log_add(l, line);
        l->current[0] = 0;
        bs_log_message(l,
                       e->kind == BS_VERIFIED ? "VERIFIED RX" : "UNVERIFIED RX / claimed callsign",
                       e->call, e->text);
    }
}
bool bs_log_export(const BsLog *l, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < l->count; ++i) {
        if (fprintf(f, "%s\n", l->rows[(l->head + BS_LOG_ROWS - l->count + i) % BS_LOG_ROWS]) < 0) {
            ok = false;
        }
    }
    if (*l->current && fprintf(f, "%s\n", l->current) < 0) {
        ok = false;
    }
    if (fclose(f)) {
        ok = false;
    }
    return ok;
}
