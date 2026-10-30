# Tests

## Run

```sh
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Tests use simulated audio. Physical radios are not required.
Optional Linux integration tests require Python 3.

## Coverage

| Suite | Checks |
| --- | --- |
| `core` | Text format, CRC, noise, clipping, clock drift and mode changes |
| `app` | PTT flow, playback completion, history, macros and selective repair |
| `audio` | Queues, device timing, underrun, overflow and cancellation |
| `tui` | Menu groups, screen layout, small windows and safe text rendering |
| `files` | Binary data, size limits, saves, missing blocks and repair reports |
| `mfsk` | Error correction, tone calibration, frequency offsets and interference |
| `macros` | Persistent slots, validation and failed saves |
| `terminal` | Real keyboard input, redraws, macro persistence and terminal restoration |
| `file_cli` | TXT, PNG and binary roundtrips through both profiles |

All nine pass locally with GCC Release and Clang ASan/UBSan.
New code has not been run locally on Windows or macOS.

## Memory checks

```sh
cmake -S . -B build/checks -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBS_SANITIZE=ON
cmake --build build/checks --parallel
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/checks --output-on-failure
```

Leak inspection is disabled in this local setup. Address and undefined-behavior
checks remain active.

## Channel fixtures

Both profiles pass tests with noise, clipping, clock drift of ±1000 ppm,
frequency offsets of ±12 Hz, uneven tone levels and a narrowband interferer.
A 240 ms interruption is recovered in the test fixture. Sustained signal loss
and invalid protected headers are rejected. Noise alone does not acquire a packet.
These are test results, not guaranteed radio limits.

## Radio checks still needed

1. Send short messages in both directions with different TX tone settings.
2. Transfer files and compare their bytes.
3. Drop a block, request it with `C-x m`, then repair it with `C-x r`.
4. Check PTT release timing, cancellation, audio levels and device reconnects.
5. Measure useful bytes per minute, including reports and retries.

Use the same radio pair and conditions when comparing 8 and 16 tones.
