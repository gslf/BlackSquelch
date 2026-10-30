#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
static volatile sig_atomic_t interrupted;
static void on_signal(int signal_number) {
    (void)signal_number;
    interrupted = 1;
}
bool bs_quit_requested(void) {
    return interrupted != 0;
}
void bs_timestamp(char *buffer, size_t capacity) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if (local) {
        strftime(buffer, capacity, "%H:%M:%S", local);
    } else {
        snprintf(buffer, capacity, "--:--:--");
    }
}
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <direct.h>
static DWORD input_mode, output_mode;
static bool terminal_active;
double bs_time(void) {
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}
void bs_sleep(unsigned ms) {
    Sleep(ms);
}
bool bs_terminal_open(void) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(in, &input_mode) || !GetConsoleMode(out, &output_mode)) {
        return false;
    }
    if (!SetConsoleMode(out, output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        return false;
    }
    if (!SetConsoleMode(in, input_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT))) {
        SetConsoleMode(out, output_mode);
        return false;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    terminal_active = true;
    printf("\033[?1049h\033[?25l");
    return true;
}
void bs_terminal_close(void) {
    if (!terminal_active) {
        return;
    }
    printf("\033[?25h\033[?1049l");
    fflush(stdout);
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), input_mode);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), output_mode);
    terminal_active = false;
}
int bs_key(void) {
    if (!_kbhit()) {
        return -1;
    }
    int c = _getch();
    if (!c || c == 224) {
        c = _getch();
        switch (c) {
        case 15:
            return BS_KEY_BACK_TAB;
        case 72:
            return BS_KEY_UP;
        case 80:
            return BS_KEY_DOWN;
        case 73:
            return BS_KEY_PAGE_UP;
        case 81:
            return BS_KEY_PAGE_DOWN;
        case 79:
            return BS_KEY_END;
        case 83:
            return BS_KEY_DELETE;
        default:
            return -1;
        }
    }
    return c;
}
void bs_terminal_size(unsigned *cols, unsigned *rows) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    *cols = 80;
    *rows = 24;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        *cols = (unsigned)(info.srWindow.Right - info.srWindow.Left + 1);
        *rows = (unsigned)(info.srWindow.Bottom - info.srWindow.Top + 1);
    }
}
bool bs_config_path(char *buffer, size_t cap) {
    const char *base = getenv("APPDATA");
    if (!base) {
        return false;
    }
    int n = snprintf(buffer, cap, "%s\\BlackSquelch", base);
    if (n < 0 || (size_t)n >= cap) {
        return false;
    }
    _mkdir(buffer);
    n = snprintf(buffer, cap, "%s\\BlackSquelch\\config.txt", base);
    return n > 0 && (size_t)n < cap;
}
#else
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>
static struct termios original;
static bool terminal_active;
double bs_time(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
void bs_sleep(unsigned ms) {
    struct timespec t = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000};
    nanosleep(&t, NULL);
}
bool bs_terminal_open(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || tcgetattr(STDIN_FILENO, &original)) {
        return false;
    }
    struct termios raw = original;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO | IEXTEN);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    /* VMIN/VTIME 0 already make read() non-blocking. O_NONBLOCK would also apply to
       stdout, which shares the terminal's open file description, and macOS's small
       pty buffer then rejects screen repaints with EAGAIN. */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)) {
        return false;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    terminal_active = true;
    printf("\033[?1049h\033[?25l");
    return true;
}
void bs_terminal_close(void) {
    if (!terminal_active) {
        return;
    }
    printf("\033[?25h\033[?1049l");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
    terminal_active = false;
}
int bs_key(void) {
    static unsigned state;
    static int number;
    static double escape_time;
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) {
        if (state && bs_time() - escape_time > 0.03) {
            state = 0;
            return 27;
        }
        return -1;
    }
    if (!state) {
        if (c == 27) {
            state = 1;
            escape_time = bs_time();
            return -1;
        }
        return c;
    }
    if (state == 1) {
        if (c == '[' || c == 'O') {
            state = 2;
            number = 0;
            return -1;
        }
        state = 0;
        return 27;
    }
    if (c >= '0' && c <= '9') {
        number = number * 10 + c - '0';
        if (number > 999) {
            state = 0;
        }
        return -1;
    }
    state = 0;
    if (c == 'A') {
        return BS_KEY_UP;
    }
    if (c == 'B') {
        return BS_KEY_DOWN;
    }
    if (c == 'F') {
        return BS_KEY_END;
    }
    if (c == 'Z') {
        return BS_KEY_BACK_TAB;
    }
    if (c == '~') {
        if (number == 5) {
            return BS_KEY_PAGE_UP;
        }
        if (number == 6) {
            return BS_KEY_PAGE_DOWN;
        }
        if (number == 4) {
            return BS_KEY_END;
        }
        if (number == 3) {
            return BS_KEY_DELETE;
        }
    }
    return -1;
}
void bs_terminal_size(unsigned *cols, unsigned *rows) {
    struct winsize size;
    *cols = 80;
    *rows = 24;
    if (!ioctl(STDOUT_FILENO, TIOCGWINSZ, &size)) {
        if (size.ws_col) {
            *cols = size.ws_col;
        }
        if (size.ws_row) {
            *rows = size.ws_row;
        }
    }
}
static bool make_directories(char *path) {
    for (char *p = path + 1; *p; ++p) {
        if (*p == '/') {
            *p = 0;
            int r = mkdir(path, 0700);
            *p = '/';
            if (r && errno != EEXIST) {
                return false;
            }
        }
    }
    return true;
}
bool bs_config_path(char *buffer, size_t cap) {
    const char *home = getenv("HOME");
    if (!home) {
        return false;
    }
    int n;
#ifdef __APPLE__
    n = snprintf(buffer, cap, "%s/Library/Application Support/BlackSquelch/config.txt", home);
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] == '/') {
        n = snprintf(buffer, cap, "%s/blacksquelch/config.txt", xdg);
    } else {
        n = snprintf(buffer, cap, "%s/.config/blacksquelch/config.txt", home);
    }
#endif
    return n > 0 && (size_t)n < cap && make_directories(buffer);
}
#endif
