#include "app.h"
#include "wav.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void usage(void) {
    puts("BlackSquelch - analog-radio text and file modem\n"
         "Usage:\n"
         "  blacksquelch                         Interactive terminal\n"
         "  blacksquelch --devices               List native audio device IDs\n"
         "  blacksquelch [--tones 8|16] --frame CALL TEXT\n"
         "  blacksquelch [--tones 8|16] --encode CALL TEXT FILE\n"
         "  blacksquelch [--tones 8|16] --encode-file CALL INPUT OUTPUT.wav\n"
         "  blacksquelch --decode FILE           Decode WAV; detect tones automatically\n"
         "  blacksquelch --decode FILE --output-dir DIRECTORY\n"
         "  MFSK8 and MFSK16 use 25 baud, FEC and interleaving; default is 8 tones.\n"
         "  blacksquelch --help\n"
         "Callsigns contain exactly 6 ASCII letters/digits. TX defaults to 8 tones.\n"
         "RX always detects 8 or 16 tones from training. Quote text containing spaces. Text is "
         "1-512 characters from the fixed alphabet. Files: any format, max 1 MiB.\n"
         "Interactive: C-x t selects a file. Received files go to received-files/.");
}
typedef struct {
    unsigned verified;
    unsigned files;
    bool save_error, transfer_error;
    const char *directory;
    BsFileReception reception;
} DecodeResult;
typedef struct {
    const BsFile *file;
    const char *call;
} FileSource;
static bool file_frame(void *context, size_t index, BsFrame *frame) {
    const FileSource *source = context;
    return bs_file_frame(source->file, source->call, index, frame);
}
static void print_event(void *context, const BsEvent *e) {
    DecodeResult *result = context;
    char line[512], time[16];
    BsFileResult status = bs_file_accept(&result->reception, e);
    if (status == BS_FILE_COMPLETE) {
        char path[2048];
        if (bs_file_save(&result->reception, result->directory, path, sizeof path)) {
            result->reception.saved = true;
            ++result->files;
            printf("FILE RECEIVED: %s (%u bytes)\n", path, (unsigned)e->file.size);
        } else {
            fputs("File verified but could not be saved.\n", stderr);
            result->save_error = true;
        }
    } else if (status == BS_FILE_BUSY || status == BS_FILE_ERROR) {
        result->transfer_error = true;
        fputs("File transfer rejected: conflicting metadata, checksum or incomplete transfer.\n",
              stderr);
    }
    bs_timestamp(time, sizeof time);
    if (e->kind == BS_MODE_DETECTED) {
        printf("[%s] RX MFSK%u LOCKED: 25 baud, FEC + interleaving\n", time, e->tones);
    } else if (e->kind == BS_PROGRESS || e->kind == BS_FIELD_DONE) {
        bs_event_format(e, line, sizeof line);
        printf("[%s] %s\n", time, line);
    } else if (e->kind != BS_LISTENING) {
        if (e->is_repair) {
            printf("[%s] %s | file report from %s for %s | transfer=%08X | base=%u%s\n", time,
                   e->detail, e->call, e->repair.target, (unsigned)e->repair.id,
                   (unsigned)e->repair.base, e->repair.complete ? " COMPLETE" : "");
        } else if (e->is_file) {
            printf("[%s] %s | callsign=%s | file=%s | block=%u/%zu\n", time, e->detail, e->call,
                   e->file.name, (unsigned)e->file.index + 1, bs_file_chunk_count(e->file.size));
        } else {
            printf("[%s] %s | claimed callsign=%s | text=%s | last group=%s\n", time, e->detail,
                   e->call, e->text, e->bits);
        }
        if (e->kind == BS_VERIFIED) {
            ++result->verified;
        }
    }
}
int main(int argc, char **argv) {
    unsigned tones = BS_DEFAULT_TONES;
    bool tx_options = false;
    while (argc > 1 && !strcmp(argv[1], "--tones")) {
        if (argc < 4 || (strcmp(argv[2], "8") && strcmp(argv[2], "16"))) {
            fputs("Use --tones 8 or 16 before a TX command.\n", stderr);
            return 2;
        }
        tones = (unsigned)strtoul(argv[2], NULL, 10);
        tx_options = true;
        argv += 2;
        argc -= 2;
    }
    if (tx_options && strcmp(argv[1], "--frame") && strcmp(argv[1], "--encode") &&
        strcmp(argv[1], "--encode-file")) {
        fputs("TX options apply to --frame, --encode or --encode-file; RX is automatic.\n", stderr);
        return 2;
    }
    if (argc == 1) {
        return bs_app_run();
    }
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        usage();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--devices")) {
        BsDevice devices[BS_DEVICE_LIMIT];
        for (int input = 1; input >= 0; --input) {
            size_t n = bs_audio_devices(input != 0, devices, BS_DEVICE_LIMIT);
            printf("%s devices (%zu):\n", input ? "Input" : "Output", n);
            for (size_t i = 0; i < n; ++i) {
                printf("  %s  %s\n", devices[i].id, devices[i].name);
            }
        }
        return 0;
    }
    if ((argc == 4 && !strcmp(argv[1], "--frame")) || (argc == 5 && !strcmp(argv[1], "--encode"))) {
        BsFrame frame;
        if (!bs_frame_build(&frame, argv[2], argv[3])) {
            fputs("Invalid callsign or text. Unsupported bytes are never replaced.\n", stderr);
            return 2;
        }
        printf("%zu frame bits; %.2f seconds; %u tones%s\n", frame.count,
               bs_frame_duration(&frame, tones), tones, " / 25 baud / FEC / interleaving");
        if (argc == 5) {
            if (!bs_wav_write(argv[4], &frame, 0.5, tones)) {
                fputs("Could not write WAV file\n", stderr);
                return 1;
            }
        } else {
            for (size_t i = 0; i < frame.count; i += 8) {
                unsigned byte = 0;
                for (size_t j = 0; j < 8; ++j) {
                    byte = (byte << 1) | frame.bits[i + j];
                }
                printf("%02X%s", byte, i + 8 == frame.count ? "\n" : " ");
            }
        }
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--encode-file")) {
        BsFile *file = malloc(sizeof *file);
        if (!file) {
            return 1;
        }
        char error[256];
        BsFrame frame;
        if (!bs_file_load(file, argv[3], error, sizeof error)) {
            fprintf(stderr, "%s\n", error);
            free(file);
            return 1;
        }
        if (!bs_file_frame(file, argv[2], 0, &frame)) {
            fputs("Invalid callsign or file metadata.\n", stderr);
            free(file);
            return 2;
        }
        uint64_t samples = 0;
        for (size_t i = 0; i < bs_file_chunk_count(file->info.size); ++i) {
            if (!bs_file_frame(file, argv[2], i, &frame)) {
                free(file);
                return 2;
            }
            samples += bs_frame_samples(&frame, tones);
        }
        if (samples * 2 > UINT32_MAX - 36) {
            fputs("Offline WAV exceeds the RIFF size limit; use a smaller file.\n", stderr);
            free(file);
            return 1;
        }
        printf("File %s: %u bytes, %zu blocks; %.2f seconds; %u tones\n", file->info.name,
               (unsigned)file->info.size, bs_file_chunk_count(file->info.size),
               (double)samples / BS_RATE, tones);
        FileSource source = {file, argv[2]};
        bool ok = bs_wav_write_frames(argv[4], bs_file_chunk_count(file->info.size), file_frame,
                                      &source, 0.5, tones);
        free(file);
        if (!ok) {
            fputs("Could not write WAV file.\n", stderr);
        }
        return ok ? 0 : 1;
    }
    if ((argc == 3 || (argc == 5 && !strcmp(argv[3], "--output-dir"))) &&
        !strcmp(argv[1], "--decode")) {
        BsParser parser;
        BsReceiver receiver;
        DecodeResult *result = calloc(1, sizeof *result);
        if (!result) {
            return 1;
        }
        result->directory = argc == 5 ? argv[4] : "received-files";
        char error[256];
        bs_parser_init(&parser, print_event, result);
        bs_rx_init(&receiver, &parser);
        if (!bs_wav_decode(argv[2], &receiver, error, sizeof error)) {
            fprintf(stderr, "%s\n", error);
            free(result);
            return 1;
        }
        bool incomplete = result->reception.active && !result->reception.complete;
        printf("Verified frames: %u; saved files: %u%s\n", result->verified, result->files,
               incomplete ? "; INCOMPLETE FILE (not saved)" : "");
        int code = result->save_error                                           ? 1
                   : result->verified && !incomplete && !result->transfer_error ? 0
                                                                                : 3;
        free(result);
        return code;
    }
    usage();
    return 2;
}
