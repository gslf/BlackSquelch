# BlackSquelch

Send text and files through an analog radio using a computer microphone and speaker.
PTT is manual. Choose 8 or 16 transmit tones; reception detects both automatically.

## Install

Use the [latest release](https://github.com/gslf/BlackSquelch/releases/latest).
No compiler is needed. Packages cover Linux and macOS (Intel or ARM64), and Windows x64.

Linux / macOS:

```sh
curl -fL https://github.com/gslf/BlackSquelch/releases/latest/download/install-release.sh -o install-release.sh
sh install-release.sh
~/.local/bin/blacksquelch
```

Windows, in PowerShell:

```powershell
Invoke-WebRequest https://github.com/gslf/BlackSquelch/releases/latest/download/install-release.ps1 -OutFile install-release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\install-release.ps1
& "$env:LOCALAPPDATA\Programs\BlackSquelch\bin\blacksquelch.exe"
```

The scripts select the right package and verify its SHA-256 checksum. No administrator
access is needed. Run the same commands to update; close the app first.
Use `--version v1.0.0` on Linux/macOS or `-Version v1.0.0` on Windows to select a release.
Custom locations use `--prefix DIRECTORY` or `-InstallDir DIRECTORY`.

For offline installation, download and extract the matching archive from the release,
then run `sh blacksquelch/install.sh` or
`powershell -NoProfile -ExecutionPolicy Bypass -File .\blacksquelch\install.ps1`.
You can also run the executable directly from its `bin` folder.

Linux needs glibc 2.35+ and a working PulseAudio or ALSA setup. macOS needs version
13+; packages are not notarized, so macOS may ask you to allow them in Privacy & Security.
Windows needs Windows 10/11 x64; runtime DLLs are included.

To uninstall:

```sh
sh ~/.local/share/blacksquelch/uninstall.sh
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\Programs\BlackSquelch\uninstall.ps1"
```

For a custom location, run its installed uninstall script. Preferences, macros and
received files are kept.

## Browser app

The same modem is also available as a responsive, installable PWA, with text,
files, selective repair, history, macros and logs. It works offline after the first
complete load and interoperates with the terminal version. No account or external
runtime dependency is needed.

Extract `blacksquelch-web.zip` from the release, then run:

```sh
python3 -m http.server 8080 --directory blacksquelch-web
```

Open `http://localhost:8080`, or host the folder over HTTPS for phones and other
computers. Tap Settings to start audio. Install app appears in browser mode and is
hidden in the installed app. Keep the app visible while using radio audio.

[Browser setup, building and hosting](docs/web.md).

## Build

Requires GCC or Clang, CMake 3.20+, and Make. Audio dependencies are included.
Python 3 enables the optional Linux integration tests.

On Linux, install your distribution's C build tools. On macOS, install Apple's
command-line tools and CMake. Then run:

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/blacksquelch
```

On Windows, use [MSYS2 UCRT64](https://www.msys2.org/) with CMake on PATH:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc make
cmake -S . -B build -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/blacksquelch.exe
```

Restart the app after rebuilding. With CMake 3.24+, add `--fresh` to the configure
command if an old cache points to missing tools. Run the executable in the build
directory you just updated.

## First run

Enter a six-character callsign, select the microphone and speaker, and choose
8 or 16 TX tones. Tab moves between fields; Shift+Tab moves back. Up/Down changes
values. Enter saves. Allow microphone access if the OS asks.

The default is 8 tones and 50% output level. RX needs no tone setting.
Use `--devices` to list audio devices.

## Send a message

1. Type the message and press Enter to prepare it.
2. Check the displayed duration.
3. Hold the radio PTT, then press Space.
4. Keep PTT held until the app tells you to release it.

Esc cancels. Ctrl+C quits. Completion means local audio finished, not remote receipt.

Text accepts 1–512 characters: letters, digits, spaces, and `.,?!:;-/()`.
Lowercase becomes uppercase.

After a successful text transmission, the composer clears. Up/Down recalls the
last 32 sent texts from this session. Down past the newest restores your draft.
Interrupted transmissions keep their text. History is not saved on exit.

## Menu

Press Ctrl+X, release it, then choose an action. This is written as `C-x` below.
Use Up/Down and Enter, or press the action's key. Esc closes the menu.

The main screen shows conversation, composition and PTT instructions.
`C-x o` opens settings. `C-x l` shows or hides the technical log.
Page Up/Page Down scroll the log; End follows live output. `C-x e` exports it.
`NO_COLOR=1` disables colors.

## Files

Use `C-x t`, enter a path without quotes, then follow the same Enter/PTT/Space steps.
Files of any format are sent unchanged, including TXT, PNG and archives.
The limit is 1 MiB per file, with a filename of up to 96 bytes.

Files use 64-byte blocks. Only a complete, verified file is saved in
`received-files/`, relative to the working directory. Names include a transfer ID
and counter. Existing files are never overwritten. Unsupported filename characters
become underscores; file contents stay unchanged.

If blocks are missing:

1. The receiver uses `C-x m` to prepare a missing-block report, then sends it using PTT.
2. After receiving the report, the sender uses `C-x r` to send only those blocks.
3. Repeat as needed. Once saved, `C-x m` can report complete receipt.

Without a repair request, `C-x r` repeats the full retained transmission.
The source is a snapshot: later disk edits do not change a repeat or repair.

`C-x v` retries a failed save. `C-x a` discards an unsaved incoming file.
One incoming file and the latest outgoing file are retained in memory.
Pending transfers are lost on exit. Both stations need this version.

## Macros

Nine saved messages use `C-x 1` through `C-x 9`.
They insert text into the composer; sending still uses Enter and PTT.

1. Open `C-x k` and choose a slot with Up/Down.
2. Press Enter to edit. Ctrl+U clears the message; empty means disabled.
3. Press Ctrl+S to save all changes and return.

Enter keeps an edit inside the manager. Esc cancels that edit, or discards unsaved
manager changes. `C-x w` starts an edit from the current composition.
Insertion adds a space when needed and refuses messages longer than 512 characters.

Macros are saved in `macros.txt`, beside `config.txt`, and loaded on restart.
Audio settings use the same folder:

- Linux: `$XDG_CONFIG_HOME/blacksquelch/` or `$HOME/.config/blacksquelch/`.
- macOS: `$HOME/Library/Application Support/BlackSquelch/`.
- Windows: `%APPDATA%\BlackSquelch\`.

## Speed

Both profiles use 25 symbols/s, error correction and interleaving.

| TX tones | After error correction, before packet overhead | Useful file speed |
| --- | ---: | ---: |
| 8 | 37.5 bit/s | 2.36 bytes/s |
| 16 | 50 bit/s | 3.10 bytes/s |

File speeds assume full 64-byte blocks, an 8-character filename and no errors.
They include packet overhead. Longer filenames reduce them.

| Content | 8 tones | 16 tones |
| --- | ---: | ---: |
| 100-character message | 23.52 s | 18 s |
| 1 KiB file | 7 min 14 s | 5 min 30 s |
| 10 KiB file | 72 min 19 s | 55 min 2 s |

These are calculated audio times. Add the two-second countdown, audio delays,
PTT turns and any repairs. Text uses six-bit character codes; files use eight-bit
bytes. Real-radio throughput is not yet measured. Fewer lost blocks can make
8 tones faster overall than 16.

## Offline tools

Optional audio fixtures for testing:

```sh
./build/blacksquelch --tones 8 --frame IU2ABC 'CIAO'
./build/blacksquelch --tones 16 --encode IU2ABC 'CIAO' message.wav
./build/blacksquelch --tones 8 --encode-file IU2ABC image.png transmission.wav
./build/blacksquelch --decode transmission.wav --output-dir received-files
```

WAV files are not needed for live transfers. TX accepts only 8 or 16 tones;
the old two-tone mode and `--rate` option are removed.

[Protocol](docs/protocol.md) · [Architecture](docs/architecture.md) ·
[Tests](docs/testing.md) · [Releases](docs/releases.md) ·
[Audio library](third_party/miniaudio/README.md)
