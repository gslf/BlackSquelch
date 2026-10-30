# Architecture

| Files | Role |
| --- | --- |
| `protocol.c` | Text, file and repair frames; CRC checks |
| `fec.c`, `mfsk.c`, `modem.c` | Error correction, tones, audio decoding and timing |
| `files.c` | File snapshots, block assembly, saves and repair reports |
| `app.c`, `commands.c` | Operator controls and transmission state |
| `macros.c`, `config.c` | Saved shortcuts and audio settings |
| `audio.c`, `audio_queue.c`, `audio_driver.c` | Native audio and playback completion |
| `tui.c`, `log.c`, `platform.c` | Screen, session log and OS support |
| `main.c`, `wav.c` | Offline commands and audio fixtures |

## Sending

Preparation freezes the content, callsign, tone count and block list.
Text, file blocks and reports share the protected MFSK packet format.
The duration includes training, error correction, padding and timing pilots.

Frames are generated into a fixed buffer. Partial audio writes retain pending
samples. Completion waits for the final sample to leave the device, not just the
software queue. Native callbacks do not allocate memory or write files.

## Receiving

The receiver removes DC, filters audio and uses 8 kHz internal processing.
Training detects 8 or 16 tones and measures tone levels and frequency offset.
Pilot pairs track timing and frequency. Soft Viterbi decoding corrects errors;
CRC and format checks decide whether data is accepted.

Physical reception blocks local sending even before decoded fields are available.
A lost signal, cancellation or audio reset clears the decoder. Verified file
blocks remain available unless explicitly discarded.

## Files

Files are limited to 1 MiB and split into 64-byte blocks. Incoming blocks may
arrive out of order. A complete file must pass CRC-32 before exclusive creation
in `received-files/`. Failed saves retain the verified data for retry.

Repair reports carry missing-block bitmaps. The sender checks the transfer ID,
callsign and bounds before queuing blocks. PTT remains manual. Successful repair
playback clears sent requests; interruption retains them. Transfer state is not
saved on exit. Checksums do not authenticate a station.

## Text and macros

Successful text playback clears the composer and stores the text in a 32-entry
session history. Browsing copies entries and preserves a separate draft.
Files, reports and interrupted sends do not change text history.

The macro manager edits a separate copy of nine shortcuts. Save commits it through
`macros.txt`; cancellation leaves saved macros unchanged. Macros insert text only.
Audio service continues while menus, settings or the macro manager are open.

## Preferences and screen

`config.txt` uses schema 2: callsign, input, output, amplitude and tones.
`macros.txt` uses schema 1: all nine slots, with empty values disabling shortcuts.
Both files use temporary-file replacement. Unknown, duplicate or missing fields
are rejected. Only 8 and 16 tones are supported.

The command table drives both menu entries and keyboard actions. The screen
redraws changed rows only, sanitizes external text and adapts to small terminals.
The technical log is hidden by default. Normal exits restore terminal settings.
