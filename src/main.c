#include "app.h"
#include "wav.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
static void usage(void) {
    puts("BlackSquelch - analog-radio text modem\n"
         "Usage:\n"
         "  blacksquelch                         Interactive terminal\n"
         "  blacksquelch --devices               List native audio device IDs\n"
         "  blacksquelch [--rate 10|25|50] --frame CALL TEXT\n"
         "  blacksquelch [--rate 10|25|50] --encode CALL TEXT FILE\n"
         "  blacksquelch --decode FILE           Decode WAV; log each bit immediately\n"
         "  blacksquelch --help\n"
         "Callsigns contain exactly 6 ASCII letters/digits. TX defaults to 10 bit/s.\n"
         "RX always detects the rate from the preamble. Quote text containing spaces. Text is "
         "1-512 characters from the fixed alphabet.");
}
typedef struct {
    unsigned verified;
} DecodeResult;
static void print_event(void *context, const BsEvent *e) {
    DecodeResult *result = context;
    char line[512], time[16];
    bs_timestamp(time, sizeof time);
    if (e->kind == BS_RATE_DETECTED) {
        printf("[%s] RX RATE LOCKED: %u bit/s; measured symbol %.3f ms\n", time, e->bit_rate,
               e->symbol_ms);
    } else if (e->kind == BS_PROGRESS || e->kind == BS_FIELD_DONE) {
        bs_event_format(e, line, sizeof line);
        printf("[%s] %s\n", time, line);
    } else if (e->kind != BS_LISTENING) {
        printf("[%s] %s | claimed callsign=%s | text=%s | last group=%s\n", time, e->detail,
               e->call, e->text, e->bits);
        if (e->kind == BS_VERIFIED) {
            ++result->verified;
        }
    }
}
int main(int argc, char **argv) {
    unsigned bit_rate = BS_DEFAULT_BIT_RATE;
    if (argc > 1 && !strcmp(argv[1], "--rate")) {
        if (argc < 4 || (strcmp(argv[2], "10") && strcmp(argv[2], "25") && strcmp(argv[2], "50")) ||
            (strcmp(argv[3], "--encode") && strcmp(argv[3], "--frame"))) {
            fputs("Use --rate 10, 25 or 50 before --encode or --frame. RX is automatic.\n", stderr);
            return 2;
        }
        bit_rate = !strcmp(argv[2], "10") ? 10 : !strcmp(argv[2], "25") ? 25 : 50;
        argv += 2;
        argc -= 2;
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
        printf("%zu bits; %.2f seconds of audio at %u bit/s\n", frame.count,
               bs_frame_duration(&frame, bit_rate), bit_rate);
        if (argc == 5) {
            if (!bs_wav_write(argv[4], &frame, 0.5, bit_rate)) {
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
    if (argc == 3 && !strcmp(argv[1], "--decode")) {
        BsParser parser;
        BsReceiver receiver;
        DecodeResult result = {0};
        char error[256];
        bs_parser_init(&parser, print_event, &result);
        bs_rx_init(&receiver, &parser);
        if (!bs_wav_decode(argv[2], &receiver, error, sizeof error)) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        printf("Verified frames: %u\n", result.verified);
        return result.verified ? 0 : 3;
    }
    usage();
    return 2;
}
