# Testing and field validation

## Automated suites

Build and run on every platform with CMake and CTest:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Tests require no physical audio devices. Audio integration tests explicitly select
a miniaudio null backend that is not compiled into the production application.

`test_core` covers:

- CRC-16/CCITT-FALSE's ASCII check vector and a literal, independently calculated
  `IU2ABC / CIAO` frame including its `0xAC84` CRC.
- Every valid alphabet code; every reserved text code; all disallowed callsign codes.
- An exactly six-character callsign crossed with every text length 1–512; rejection
  of shorter/longer callsigns; exact durations at all three bit rates.
- Every possible padding length, zero-padding validation, invalid lengths, invalid
  callsigns, unsupported bytes, empty text, and exact bit counts/durations.
- CRC corruption, nonzero padding with a repaired CRC, invalid codes with a repaired
  CRC, truncation at every acquired position of a sample frame, and parser reuse.
- Streaming synthesis/demodulation and automatic rate detection through deterministic
  simulated channels at 10, 25, and 50 bit/s.
- Consecutive frames that change rate without silence or receiver resets, plus
  malformed-frame recovery and rejection of unsupported transmit rates.
- A full 512-character constant-symbol-heavy message, loss of signal, noise alone,
  WAV roundtrips at all three rates, and malformed RIFF containers.

`test_app` substitutes a deterministic native-audio interface. It covers freezing
text and rate, silent countdown, submission versus actual drain, cancellation, output-start
failure, local-RX interlock, reception during countdown, invalid text, and manual
repeat. In particular, it queues the whole waveform while holding the playback
clock at zero and verifies that completion is not displayed. It also checks rate
selection in settings and through C-x b, strict complete configuration schemas,
exact generated sample counts/deadlines for each rate, and rejection of invalid
callsigns or rates. Modal tests cover availability, consumed letters, dismissal,
explicit RX cancellation, and removal of standalone command shortcuts.

`test_audio` covers queue boundaries, full/empty behavior, data integrity across
unsigned cursor wrap, prefill, delayed hardware completion, capture overflow,
playback starvation, null-device capture/playback, repeated reset and cancellation.
A deterministic callback fixture holds output in a simulated pipeline after the
last sample is consumed and verifies that completion remains false.

`test_tui` checks the header, panels, partial bits, context-dependent modal entries,
PTT visibility, a single command-prefix hint, persistent receive groups between
fields, clipping across small terminal sizes, and escape sanitization.
It writes ANSI snapshots into the build directory for visual inspection.

On Linux, the optional standard-library Python test checks the real application
through a pseudo-terminal:

```sh
python3 tests/test_terminal.py ./build/blacksquelch
```

It exercises first-run settings, callsign normalization, all TX rate choices,
the RX automatic-mode indicator, C-x menu opening/dismissal, zero output while idle,
composition updates that leave the waiting line untouched, resize to a small window,
exit, and restoration of terminal modes. It uses a temporary XDG configuration
directory and does not modify normal user preferences.

## Sanitizers and compiler checks

```sh
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBS_SANITIZE=ON
cmake --build build/asan --parallel
ctest --test-dir build/asan --output-on-failure
```

This enables AddressSanitizer and UndefinedBehaviorSanitizer. If a sandbox blocks
LeakSanitizer process inspection, run CTest with `ASAN_OPTIONS=detect_leaks=0`;
address and undefined-behavior checks remain enabled. Tests explicitly retain
assertions in Release builds.

The CI matrix uses native Linux/macOS builds and MSYS2 UCRT64 MinGW-w64 GCC on
Windows. Local cross-compilation cannot replace Windows/macOS runtime checks or
real audio/radio trials.

Locally verified: GCC Release and Clang ASan/UBSan pass all five suites; LLVM-MinGW
with UCRT builds the Windows application and four C test executables. Windows
execution, macOS compilation/runtime, and physical audio/radio tests are not claimed.

## Simulated channel results

The following deterministic cases passed locally. Values describe these fixtures,
not guaranteed operating limits. Uniform noise amplitude is relative to PCM full
scale; transmit peak amplitude is 0.5 before applying gain. Resampling uses linear
interpolation, so clock changes also shift the received tones slightly. Every
waveform case below is exercised at 10, 25, and 50 bit/s without supplying the
receiver a rate. The acquired rate is checked against the transmitter.

| Payload / condition | Result |
| --- | --- |
| All 47 characters, clean channel | Verified |
| All 47 characters, 1327-sample offset, uniform noise ±0.08 | Verified |
| All 47 characters, gain ×4 with hard clipping, noise ±0.01 | Verified |
| All 47 characters, +1000 ppm clock, noise ±0.01, arbitrary offset | Verified |
| All 47 characters, −1000 ppm clock, noise ±0.01, arbitrary offset | Verified |
| All 47 characters, gain ×0.02 | Verified |
| 512 `A` characters, noise ±0.02, 715-sample offset | Verified |
| 512 `A` characters, ±100 ppm clock, 715-sample offset | Verified |
| Fade over 30% of one symbol (30/12/6 ms) | Verified, with explicit uncertainty marker |
| Eleven-symbol dropout inside a frame | Interrupted, never verified |
| Ten seconds of broadband noise | No verified frame |

The long-run drift test is intentionally separate from ordinary-text drift. A
maximum-length sequence with no useful transitions can accumulate substantially
more timing error than ordinary text. This receiver does not promise tolerance
outside the measured fixtures. A failed clock/noise condition should be recorded
as a failed reception, not hidden by adjusting parser lengths or omitting bits.

## Native audio acceptance checklist

These checks require real devices and remain pending on Linux, Windows, and macOS.
Native runtime checks must run on the respective operating systems.

1. Build and run all test suites on each platform. Compare `--frame` output and
   decode the same externally transferred WAV fixture.
2. Enumerate/select each input/output. Verify actual requested sample format;
   unsupported devices must fail with a useful message.
3. Confirm microphone permissions, exclusive-device contention, unplug/replug, and
   explicit reconnect from settings.
4. Record the output with an independent capture device. Compare the captured
   waveform end with the release-PTT instruction, including backend latency.
5. Cancel during countdown, first tone, middle, and drain. Each started transmission
   must stop promptly and show the interrupted/release instruction.
6. Force capture overflow/playback underrun using scheduler load. No affected frame
   may be presented as successfully transmitted or verified without an error path.
7. Exercise slow terminals, resizing, colorless terminals, log scrolling, and export.
   Measure bit-decision-to-screen latency; the target is at most 100 ms under normal
   load, but this has not been instrumented on real hardware.

## Two-station radio trial

Use two independent computers and radios, with an operator at each station. Begin
with short text and conservative speaker/microphone levels. Disable processing that
removes steady tones if the audio device exposes such settings. Set squelch and
physical placement so the preamble arrives without clipping or acoustic feedback.

1. Send the golden `CIAO` message in both directions at each of 10, 25, and 50 bit/s;
   record the detected rates and both RX logs. Deliberately use different local TX
   settings on the two computers to verify independent RX acquisition.
2. Send the whole alphabet and representative short messages.
3. Repeat at several distances and levels, then test long constant-character runs.
4. Test speech/noise interference, brief fades, early PTT release, and loss of input.
5. Compare each received text with the original; count verified, CRC-failed,
   semantically invalid, interrupted, and missed messages separately.
6. Record radio models, audio devices, OS/build revision, distance/environment,
   squelch, levels, selected/detected rates, message lengths, and observed timing/confidence.

Suggested results table:

| Trial | Station pair | Text length | Sent | Verified | CRC failed | Interrupted/missed | Conditions |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| | | | | | | | |

Changing TX rate among 10, 25, and 50 bit/s requires no receiver setting. Both
stations must use the specified fixed-six-character frame format. Other tone/rate changes
would require coordinated software changes and fresh validation. No hardware
results are claimed by this repository.
