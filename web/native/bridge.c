/* Small UI boundary; all radio protocol and session transitions remain in src/. */
#include "app.h"
#include "platform.h"
#include "web_audio.h"
#include <emscripten/emscripten.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static BsApp *app;
static char json[131072];
static size_t offset;
static void append(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int count = vsnprintf(json + offset, sizeof json - offset, format, args);
    va_end(args);
    if (count > 0 && (size_t)count < sizeof json - offset) offset += (size_t)count;
}
static void string(const char *value) {
    append("\"");
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '\\') append("\\%c", *p);
        else if (*p < 32) append("\\u%04x", *p);
        else append("%c", *p);
    }
    append("\"");
}
EMSCRIPTEN_KEEPALIVE int bs_web_init(void) {
    if (app) { bs_audio_close(app->audio); free(app); }
    app = calloc(1, sizeof *app);
    if (!app) return 0;
    bs_app_init(app);
    return 1;
}
EMSCRIPTEN_KEEPALIVE void bs_web_tick(void) { if (app && !app->quit) bs_app_step(app, bs_time()); }
EMSCRIPTEN_KEEPALIVE void bs_web_key(int key) { if (app) bs_app_key(app, key); }
EMSCRIPTEN_KEEPALIVE int bs_web_command(int key) { return app && bs_app_command(app, (char)key); }
EMSCRIPTEN_KEEPALIVE int bs_web_compose(const char *text) {
    if (!app || app->state != BS_COMPOSE || strlen(text) > BS_MAX_TEXT) return 0;
    strcpy(app->compose, text);
    app->history_offset = 0;
    return 1;
}
EMSCRIPTEN_KEEPALIVE void bs_web_disconnect(const char *reason) {
    if (app) bs_app_audio_failed(app, reason);
    bs_web_audio_ready(0, 0);
}
EMSCRIPTEN_KEEPALIVE int bs_web_configure(const char *call, unsigned tones, double amplitude) {
    if (!app || strlen(call) != BS_CALL_CHARS || !bs_tones_valid(tones) || !isfinite(amplitude) ||
        amplitude < 0.05 || amplitude > 1) return 0;
    char normalized[BS_CALL_CHARS + 1];
    strcpy(normalized, call);
    if (!bs_normalize(normalized, true, NULL)) return 0;
    if (app->state != BS_SETTINGS) {
        if (!bs_app_command(app, 'o')) return 0;
    }
    strcpy(app->editing.call, normalized);
    app->editing.tones = tones;
    app->editing.amplitude = amplitude;
    app->quit = false;
    bs_app_key(app, 13);
    return app->state == BS_COMPOSE;
}
EMSCRIPTEN_KEEPALIVE int bs_web_file(const char *path) {
    if (!app || strlen(path) >= sizeof app->file_path || !bs_app_command(app, 't')) return 0;
    strcpy(app->file_path, path);
    bs_app_key(app, 13);
    return app->state == BS_WAIT;
}
EMSCRIPTEN_KEEPALIVE int bs_web_macro(unsigned slot, const char *text) {
    if (!app || app->state != BS_MACROS || slot >= BS_MACRO_COUNT || strlen(text) > BS_MAX_TEXT) return 0;
    char normalized[BS_MAX_TEXT + 1];
    strcpy(normalized, text);
    if (*normalized && !bs_normalize(normalized, false, NULL)) return 0;
    strcpy(app->editing_macros.text[slot], normalized);
    app->macro_editing = false;
    return 1;
}
EMSCRIPTEN_KEEPALIVE void bs_web_storage_failed(void) {
    if (app && app->incoming_file.complete) app->incoming_file.saved = false;
}
EMSCRIPTEN_KEEPALIVE const char *bs_web_snapshot(void) {
    if (!app) return "{}";
    offset = 0;
    append("{\"version\":\"" BS_VERSION "\",\"state\":%u,\"audio\":%s,\"quit\":%s,\"receiving\":%s,\"tones\":%u,\"level\":%.5f,\"confidence\":%.5f,\"duration\":%.3f,\"played\":%.3f,\"call\":",
           app->state, app->audio ? "true" : "false", app->quit ? "true" : "false",
           bs_rx_active(&app->receiver) ? "true" : "false", app->config.tones,
           (double)app->receiver.level, (double)app->receiver.confidence,
           (double)app->total_samples / BS_RATE, (double)app->played / BS_RATE);
    string(app->config.call);
    append(",\"amplitude\":%.3f,\"compose\":", app->config.amplitude); string(app->compose);
    append(",\"instruction\":"); string(app->instruction);
    append(",\"fileStatus\":"); string(app->file_status);
    append(",\"current\":"); string(app->log.current);
    append(",\"frozen\":"); string(app->frozen);
    append(",\"showLog\":%s,\"canInsertMacro\":%s,\"scroll\":%zu,\"macroSlot\":%u,\"macroEditing\":%s,\"macroText\":",
           app->show_log ? "true" : "false", app->macro_return_state == BS_COMPOSE ? "true" : "false", app->log.scroll, app->macro_slot,
           app->macro_editing ? "true" : "false"); string(app->macro_text);
    append(",\"macros\":[");
    for (unsigned i = 0; i < BS_MACRO_COUNT; ++i) {
        if (i) append(",");
        string((app->state == BS_MACROS ? &app->editing_macros : &app->macros)->text[i]);
    }
    append("],\"conversation\":[");
    size_t count = app->log.messages < 16 ? app->log.messages : 16;
    for (size_t i = 0; i < count; ++i) {
        if (i) append(",");
        string(app->log.conversation[(app->log.messages - count + i) % 16]);
    }
    append("],\"log\":[");
    size_t end = app->log.count > app->log.scroll ? app->log.count - app->log.scroll : 0;
    size_t start = end > 60 ? end - 60 : 0;
    for (size_t i = start; i < end; ++i) {
        if (i != start) append(",");
        string(app->log.rows[(app->log.head + BS_LOG_ROWS - app->log.count + i) % BS_LOG_ROWS]);
    }
    append("],\"commands\":[");
    for (size_t i = 0; i < bs_command_count; ++i) {
        if (i) append(",");
        append("{\"key\":\"%c\",\"enabled\":%s,\"label\":", bs_commands[i].key,
               bs_command_available(app, bs_commands[i].key) ? "true" : "false");
        string(bs_commands[i].description); append(",\"group\":"); string(bs_commands[i].group); append("}");
    }
    append("],\"incoming\":{\"active\":%s,\"complete\":%s,\"saved\":%s,\"received\":%zu,\"blocks\":%zu,\"name\":",
           app->incoming_file.active ? "true" : "false", app->incoming_file.complete ? "true" : "false",
           app->incoming_file.saved ? "true" : "false", app->incoming_file.received,
           bs_file_chunk_count(app->incoming_file.file.info.size));
    string(app->incoming_file.file.info.name); append("}}");
    return json;
}
