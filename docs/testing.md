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

GitHub Actions runs native builds on Linux and macOS (x86_64 and ARM64), and Windows x64.
Release builds must pass before publication. Physical radio checks remain separate.

## Release packages

```sh
python3 tests/test_release.py
python3 scripts/package-release.py --build-dir build --platform linux --arch x86_64
python3 tests/test_install.py build/dist/blacksquelch-linux-x86_64.tar.gz
```

Use the matching platform and architecture when packaging. Windows also needs
`--runtime-dir` pointing to the MinGW `bin` folder, then runs `tests/test_install.ps1`
with `-Archive` pointing to the ZIP.

Tests cover version validation, architecture detection, complete release assets,
runtime DLL dependencies, installation, updates and removal. They also test
automatic downloads and checksum rejection without a network. Windows checks rollback
after a failed update and runs the installed executable without MinGW on PATH.

## Browser app

Build the native and WebAssembly targets first. Node 24+ runs the web tests without
installing packages. The browser test also needs Chromium or Google Chrome.

```sh
node tests/test_web_audio.mjs
node tests/test_web_wasm.mjs build/wasm/web build/blacksquelch
node tests/test_web_browser.mjs build/wasm/web
```

Set `CHROMIUM` if the browser executable has another name. Tests cover sample-rate
conversion, buffer faults, both native/browser directions, 64-byte binary transfers,
selective repair, real AudioWorklet playback with simulated microphone input, history,
macro persistence, mobile layouts, offline reopening and installed-mode visibility.
Screenshots are saved in `build/web-tests/`. Real iOS/Android hardware and radio
interfaces still need operator checks.

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
