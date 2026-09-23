# Architecture

## Modules

| Module | Responsibility |
| --- | --- |
| `protocol.c` | Alphabet, normalization, bit packing, CRC, incremental parser, semantic validation |
| `modem.c` | PCM generation, DC removal, tone correlation, symbol timing, confidence, signal-loss detection |
| `audio.c` | Shared miniaudio device lifecycle, capture/playback and completion accounting |
| `audio_queue.c` | Bounded C11 single-producer/single-consumer PCM ring |
| `audio_driver.c` | Vendored miniaudio implementation and native latency queries |
| `commands.c` | Command descriptions and state-dependent availability |
| `app.c` | Operator state machine, frozen message, receive interlock, deadlines, error recovery |
| `tui.c` | Bounded terminal rendering, compact view, safe display of external strings |
| `log.c` | Event formatting, partial group, circular history, conversation history, explicit export |
| `platform.c` | Console modes, keyboard, monotonic time, signals, config directory |
| `config.c` | Preference validation and file replacement |
| `wav.c` | Portable little-endian streaming RIFF PCM reader/writer |
| `main.c` | Command dispatch and offline event output |

The protocol and modem include no OS APIs. Audio handles are opaque to the
controller. Protocol event pointers are valid for the callback only; consumers
copy what they retain. Static arrays bound every live audio and parser path.

## Transmitter

A transmitter references a frozen frame and emits up to the requested number of
samples. An oscillator retains phase across symbols. Amplitude is limited to
0–1, and the application settings offer 0.05–1.0. Linear ramps apply to the first
and last 240 samples only. The frozen rate selects 4800, 1920, or 960 samples per
symbol for 10, 25, or 50 bit/s. Frame duration, WAV byte counts, transmit deadlines,
and remaining-time displays use this same profile. Invalid rates are rejected
before PCM generation.

The application retains partially accepted output blocks, never regenerating or
skipping samples when a backend returns would-block. It queues short blocks and
services the keyboard between bounded passes. Playback progress comes from the
backend clock. Exhausting the generator transitions into a distinct draining
state, not completion.

Audio device access and conversion use the vendored miniaudio header. A small
native timing bridge is necessary because miniaudio's portable stop operation may
pause queued output and does not promise hardware drain:

- ALSA: `snd_pcm_delay` measures pending frames. Automatic XRUN recovery is disabled
  because silently skipped samples invalidate a modem transmission.
- PulseAudio: asynchronous timing updates feed `pa_stream_get_latency`; the queries
  run on the device's mainloop thread. Non-interpolated stale timing is conservative.
  An underflow after the first modem sample invalidates transmission.
- WASAPI: current padding plus the reported stream latency bounds pending playback.
- Core Audio: reported device latency, safety offset, device buffer size, maximum
  stream latency and Audio Unit latency bound the output pipeline. Hardware-frame
  delays are converted from the device's nominal rate to the stream rate.

The output callback operates in the negotiated native format. A converter created
at device initialization converts the modem's mono PCM16 blocks. Keeping miniaudio's
own output path in passthrough mode avoids multiple application callbacks for a
single hardware submission. Output streams must run at 48 kHz and expose timing;
initialization fails explicitly otherwise.

A callback-owned rendered-frame counter includes startup and trailing silence.
After prefill, the first modem sample fixes the content start offset. At each later
callback, rendered frames minus native pending frames and that offset give a
conservative content playback position, capped at the content actually consumed.
The position advances only after the driver reports progress. After EOF, callbacks
supply silence until the last content sample has left the reported output pipeline.
Unknown initial timing does not imply completion; the controller watchdog remains
active. Playback progress is not evidence that a radio or remote station heard it.

A stopped playback clock for two seconds or expected duration plus five seconds
invalidates transmission. Capture is serviced during output, but does not enter
the decoder. Cancellation and completion recreate both device streams to discard
queued output and acoustic loopback; pausing a stream would retain stale data on
some backends. Unexpected stops, interruptions and route changes fail the stream.
Reconnection is explicit through settings.

The latency bridge depends on native handles in the pinned miniaudio version.
Its isolated implementation and real-device acceptance checks must be reviewed
when updating the vendor header. OS/driver reporting accuracy is a hardware test
concern; the synthetic backend validates accounting, not a physical DAC.

## Receiver and clock recovery

The receiver removes a running DC estimate and evaluates quadrature correlators
for 1200/2200 Hz in overlapping 240-sample (5 ms) windows with a 48-sample (1 ms)
hop. The overlap provides enough timing resolution for 20 ms symbols without
changing the audio backend format or shortening the correlation window. Both the
tone-energy difference and the energy concentrated in the winning tone must pass
thresholds. This prevents a random winner in broadband noise from becoming a
confident bit. RMS and clipping remain separate diagnostics.

During acquisition, three independent clocks and bounded parser candidates share
those tone energies. Each tests a different nominal rate: 10, 25, or 50 bit/s. The
log displays a provisional timing hypothesis after four alternating decisions;
these are explicitly unsynchronized candidates. All 32 alternating bits must be
observed before a clock is selected. The selected parser state is transferred to
the application's event sink, and `BS_RATE_DETECTED` reports the rate, measured
symbol period, and confidence. Exact synchronization and CRC are still required.
The parser's `bit_rate` metadata is diagnostic and is not serialized into the body.

Only the selected clock receives subsequent windows, so payload transitions cannot
switch profiles. Success, invalid framing, or ten absent symbols release the
selection and initialize all three candidates. Consecutive frames can change rate
without changing local settings or restarting the receiver.

The middle 60% of each symbol contributes tone energy. A bit decision occurs near
80% of the symbol period plus the analysis-window delay. Mixed-window energies
interpolate transition positions; near-boundary transitions gently correct phase.
The period estimate averages over accepted transition intervals instead of relying
on one short interval. This limits accumulated clock error over long identical-bit
runs. Such runs continue on the estimated clock and do not invent transitions.

No-signal windows contribute neither false zero votes nor invented transitions.
A low-confidence symbol keeps its clock slot and carries an uncertainty marker.
With no integrated tone evidence, the previous decision is retained with zero
confidence. Ten absent symbols mean approximately 1.0, 0.4, or 0.2 seconds for the
three rates. Short uncertainty can corrupt a frame, which CRC usually detects,
but cannot silently shorten or extend it.

This is a small timing loop, not a radio-qualified adaptive DSP stack. Faster
profiles have less time to acquire and integrate each symbol; real radio tests
are still required. Simulation tolerances are reported separately and are not
universal operating guarantees.

## Threading and bounded work

The controller polls approximately every 5 ms and renders at most every 50 ms.
Each audio-service pass handles at most 32 modem blocks. Capture and playback each
use a 32768-sample ring (about 683 ms at 48 kHz). C11 acquire/release ordering
publishes only fully written samples. Each ring has exactly one producer and one
consumer; cursors are reset only after callbacks have stopped.

Output begins after eight 480-sample blocks are buffered, or an earlier EOF for
short synthetic streams. Startup silence is excluded from content progress. After
the first content sample, queue starvation before EOF is an error. Capture ring
overflow also invalidates audio. Backend callbacks never render, format messages,
write files or allocate application buffers. The application preallocates output
conversion state; the underlying system audio implementation manages its own work.
A capture stream that supplies no samples for 1.5 seconds is timed out.

The decoder receives one bit per symbol with bounded field widths. A body cannot
stall indefinitely when PCM arrives: all field lengths are bounded and the
symbol clock keeps advancing. When PCM stops, the controller's monotonic watchdog
aborts it. Offline end-of-file explicitly interrupts incomplete groups.

## UI, storage, and cleanup

Log history holds 2048 completed rows and one live partial row; conversation
history holds 16 entries. The display shows the most recent two conversation
entries and a scrollable log. Export writes only on C-x e. Unsupported terminal
bytes in device/configuration names are replaced for display; protocol data cannot
encode terminal controls in the first place.

TX rate changes in settings or through C-x b are persisted in configuration.
Preparing a message freezes its rate as well as its bits; later rate changes
require cancelling that preparation. RX always searches all profiles independently.
Configuration requires schema 1 and all five preference fields, exactly once.
Unknown, duplicate or missing fields fail validation without modifying the caller.
A callsign must contain exactly six alphanumeric characters; no automatic padding
or truncation is performed.

C-x opens an overlay driven by the same command table and availability checks as
the controller. It consumes command letters, and Esc/C-g closes it without editing
the composer or cancelling playback. Esc/C-g outside the modal cancels the current
operation. Modal input never enters a separate blocking event loop. The controller
continues reception, countdowns and transmission while it is open.

The renderer compares bounded character/style grids with the last successfully
written screen and emits only changed rows in one ANSI write. Rows are overwritten
before their unused tails are cleared, avoiding a blank interval. Terminal resize
or color changes invalidate the cache. Completed receive groups remain visible
until the next bit arrives; only returning to listening clears the active group.
The command prefix is advertised once in the footer. The display uses ASCII borders,
restrained colors, explicit state labels and separate panels;
`NO_COLOR` and `TERM=dumb` disable colors. Resizing clips every drawing operation.
The full view shows provisional previews separately from verified conversation
entries. Compact mode prioritizes PTT instructions, the active group, and the
composer. Input, output, and redraw run in the controller; live device timing still
requires a reasonably responsive terminal. Avoid redirecting an interactive
session into a slow terminal recorder.

The POSIX terminal restores its original flags on normal exit, Ctrl+C, SIGTERM,
and SIGHUP. Windows restores original input/output console modes. Fatal process
termination such as SIGKILL cannot run cleanup. `atexit` is a fallback, and ordinary
error paths restore the terminal explicitly.
