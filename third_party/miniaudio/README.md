# Vendored miniaudio

Upstream: <https://github.com/mackron/miniaudio/tree/0.11.25>

Version: **0.11.25**. The header and license are unmodified upstream files.
BlackSquelch uses the MIT No Attribution option in `LICENSE`.

| File | SHA-256 |
| --- | --- |
| `miniaudio.h` | `ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89` |
| `LICENSE` | `457f1b500e0adf6bc059edddfa78a2f62012e7c3bb43476c20e0bd23b25ba0eb` |

The single header is approximately 4.1 MB of source. It needs no separately installed
application library or build-time download. It calls the operating system's audio
services; Linux dynamically loads the system PulseAudio or ALSA runtime.

`src/audio_driver.h` disables codecs, file encoding, the resource manager, node graph,
engine and signal generators. Only WASAPI, Core Audio, or PulseAudio/ALSA are enabled
for the corresponding platform. The null backend exists exclusively in audio tests.

`src/audio_driver.c` compiles the implementation and contains the hardware-latency
bridge. That bridge deliberately depends on the pinned header's native handles and
types. Review it against upstream changes, run all tests, and validate completion
and cancellation on each platform before updating this pin.
