# BlackSquelch

```text
+++++++++++++++++
\--- BlackSquelch
:#/ GSLF
+++++++++++++++++
```

BlackSquelch is a C11 terminal application that sends short text messages as slow
AFSK tones through an analog handheld radio. Connect the computer microphone and
speaker acoustically to the radio, select the audio devices, and operate the radio's
PTT manually.

## Build and run

CMake is the build system on every platform. Requirements: GCC or Clang with C11
support, CMake 3.20 or newer, and Ninja (or another CMake generator). Python 3 is
optional and enables the Linux terminal integration test. Audio sources are already
vendored; configuring and building do not download dependencies.

### Linux and macOS

With the compiler, CMake and Ninja installed:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/blacksquelch
```

On macOS, Apple's command-line tools provide Clang and the system SDK. Allow
microphone access for the terminal that launches BlackSquelch. Linux uses the
running PulseAudio service (including PipeWire's PulseAudio service), or ALSA when
PulseAudio cannot initialize. The OS audio runtime must be installed; no audio
library development package is required.

### Windows: MinGW-w64

Install [MSYS2](https://www.msys2.org/) and open its **UCRT64** terminal. Install
these open-source build tools:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

From the project directory in that same terminal:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/blacksquelch.exe
```

Use the UCRT64 tools, not MSYS-targeted `/usr/bin/gcc`. The executable uses WASAPI
and an ANSI-capable console, such as Windows Terminal. The Windows CI job builds
and tests with MinGW-w64 GCC. No Visual Studio tooling is used.

### Audio library

[miniaudio 0.11.25](https://github.com/mackron/miniaudio/tree/0.11.25) provides device
enumeration, capture, playback and format conversion on all three platforms. It is
vendored under the MIT No Attribution license and needs no other application
library. It still uses OS audio services and the C runtime. The single header is
about 4.1 MB of source; codecs, the engine, resource manager, node graph and signal
generators are disabled. See [vendor provenance and checksums](third_party/miniaudio/README.md).

The modem consumes 48 kHz mono signed 16-bit samples. Capture conversion is handled
by miniaudio. Playback accepts native channel/sample formats, but requires a 48 kHz
stream and readable backend timing. The app converts mono into that native format
without allocating in its audio callbacks. System latency is accounted for before
the release-PTT instruction; a queued final sample is not completion.

Use `./build/blacksquelch --devices` to list device names and stable backend-scoped
identifiers. `default` explicitly selects the system default. A missing stored
device requires selection in settings; the app does not silently substitute one.

## Terminal interface

The screen separates recent messages, the live receive log, the text composer,
and PTT instructions. Only changed rows are redrawn; idle receive text stays in place
while meters or composition update. The C-x hint appears once, in the footer.
Muted diagnostics stay below the message flow. The insertion
end remains visible when text wraps. `NO_COLOR=1` disables colors; all state is also
represented in text. Small terminals use a compact layout.

**Press Ctrl+X, release it, then press a letter.** The notation below is `C-x`.
It opens a modal listing each currently available command and its description.
Unavailable actions are omitted. Press Esc, C-g (Ctrl+G), or C-x again to close it.
Up/Down scroll the list when it cannot fit. Reception, audio service and countdowns
continue while the menu is open. Menu letters never enter the composition.

| Sequence | Action |
| --- | --- |
| C-x s | Prepare the composed message |
| C-x r | Prepare the last message again |
| C-x b | Cycle TX rate: 10, 25, 50 bit/s |
| C-x o | Open settings |
| C-x a | Explicitly abort the current local reception |
| C-x e | Export the retained session log |
| C-x u | Clear composition |
| C-x p / C-x n | Scroll log back / forward |
| C-x f | Follow the live log |
| C-x d | Refresh audio devices while in settings |
| C-x c | Cancel preparation, countdown, playback or settings |
| C-x q | Quit and stop audio |

Enter prepares text directly. Space starts the countdown after preparation.
Outside the command modal, Esc or C-g cancels the current operation; during
playback it interrupts transmission. Ctrl+C also exits. Backspace removes the last
byte. These controls and the C-x menu are the complete keyboard interface.

## First run and settings

Settings open when configuration is absent or invalid, or audio cannot open.
Type an exactly six-character ASCII alphanumeric callsign. Tab selects callsign,
input, output, amplitude or TX rate. Up/Down changes devices and numeric values.
Enter opens audio and saves the complete configuration. C-x d refreshes the device
list; check the selections before saving. Default amplitude is 0.50 and TX rate is
10 bit/s.

Choose 10, 25 or 50 bit/s in settings or with C-x b while composing. RX always
detects all three rates from the received preamble, independently of the local TX
setting. The header shows selected TX and detected RX rates. Preparing freezes the
text and rate; cancel preparation before changing either.

Preferences are plain text, saved through a temporary file and replacement:

- Linux: `$XDG_CONFIG_HOME/blacksquelch/config.txt`, or
  `$HOME/.config/blacksquelch/config.txt` when XDG_CONFIG_HOME is unset.
- macOS: `$HOME/Library/Application Support/BlackSquelch/config.txt`.
- Windows: `%APPDATA%\BlackSquelch\config.txt`.

```text
schema=1
callsign=IU2ABC
input=default
output=default
amplitude=0.500000
bit_rate=10
```

Every listed field is required exactly once. Unknown fields, unsupported schema
values, duplicates and incomplete files are rejected. The settings screen creates
a complete valid file. There are no configuration migrations, legacy loaders or
shortcut aliases. Callsigns are never padded or truncated. If saving fails, the
screen reports it and the current audio settings remain usable for that session.

## Operator workflow

1. Compose a short message. Lowercase ASCII becomes uppercase. Unsupported bytes
   are reported by position and value and remain editable.
2. Press Enter or C-x s to freeze the frame and rate and inspect its duration.
3. **Hold the radio PTT, then press Space.** A silent visual countdown lasts two seconds.
4. **Keep PTT pressed** throughout playback and draining.
5. Release PTT when the app says **Transmission completed — RELEASE THE RADIO PTT**,
   or immediately when an interruption/error requests release.

C-x r repeats using the same PTT sequence. “TX PLAYED” marks completed local audio
playback.
Reception continues while composing or waiting. A received frame blocks sending
until it ends or C-x a explicitly abandons it. A newly acquired frame also cancels
the countdown. After local playback, queued capture and decoder state are reset.

The log includes partial bit groups, field interpretations, provisional callsign
and text, and verification results. Provisional text is separate from verified
conversation entries. Input level, clipping and tone confidence are diagnostics;
confidence is not a calibrated SNR measurement. History stays in memory unless
C-x e exports it to a timestamped file in the working directory.

## Offline commands

```sh
./build/blacksquelch --frame IU2ABC 'CIAO'
./build/blacksquelch --rate 50 --encode IU2ABC 'CIAO' message.wav
./build/blacksquelch --decode message.wav
```

`--rate` accepts only 10, 25 or 50 and applies to `--frame`/`--encode`; its default
is 10. Decode always detects the rate. WAV files use 48 kHz mono PCM16. Decode exits
0 if at least one frame verified, 3 if none verified, 2 for invalid arguments, and
1 for file/container errors. A verified frame does not validate every frame in a file.

## Protocol

All profiles use 1200 Hz for 0, 2200 Hz for 1, continuous transmit phase, and 5 ms
edge ramps. Characters use a fixed six-bit alphabet. Callsigns have exactly six
characters and no length field; messages contain 1–512 characters.

The 32-bit alternating preamble uses the selected rate. RX compares three timing
hypotheses, locks to the preamble, and checks the exact 32-bit sync word. There is
no rate field or negotiation. Each subsequent frame can use a different rate.

`IU2ABC / CIAO` produces 160 bits with body CRC `0xAC84`:

| TX rate | Preamble | CIAO audio | 100-character audio | 512-character audio |
| --- | ---: | ---: | ---: | ---: |
| 10 bit/s | 3.20 s | 16.00 s | 73.60 s | 320.80 s |
| 25 bit/s | 1.28 s | 6.40 s | 29.44 s | 128.32 s |
| 50 bit/s | 0.64 s | 3.20 s | 14.72 s | 64.16 s |

The two-second PTT countdown and audio buffering are additional. These durations
do not guarantee reception under every channel condition. CRC detects errors but
does not correct them or authenticate a callsign. ACK, FEC, compression, addressing,
encryption, VOX and automatic PTT are not implemented.

- [Wire specification and golden vector](docs/protocol.md)
- [Architecture, audio timing and bounded buffers](docs/architecture.md)
- [Automated tests and real-radio validation](docs/testing.md)

## Validation status

Local checks pass with GCC Release and Clang AddressSanitizer/UndefinedBehaviorSanitizer:
protocol/modem, controller/configuration, audio queues/null-device integration,
TUI rendering and the real Linux pseudo-terminal flow (five suites). Leak checking
was disabled in the sandbox. Windows executable and tests cross-compile with
LLVM-MinGW/UCRT; native Windows execution and macOS compilation remain unverified
locally. Real audio-device timing and two-radio trials are still pending.
