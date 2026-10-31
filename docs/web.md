# Browser app

BlackSquelch also runs as an installable, offline PWA. The C modem, protocol,
file transfers and session controller are shared with the terminal version.
No framework, CDN, account or radio server is required. Audio stays on your device.
One tab or installed window owns the active session, preventing conflicting audio
and browser-storage writes.

## Open

Download `blacksquelch-web.zip` from a release and extract it. Serve its folder
with any static web server, for example:

```sh
python3 -m http.server 8080 --directory blacksquelch-web
```

Open `http://localhost:8080`. For another computer or a phone, use an HTTPS host.
Opening `index.html` directly, or using plain HTTP on a LAN address, does not enable
microphone access, installation or offline support.

## Use

1. Open Settings, enter a six-character callsign and choose audio devices.
2. Select 8 or 16 TX tones and an output level, then tap Save & start audio.
3. Type text or attach any file up to 1 MiB. Tap Prepare.
4. Hold the radio PTT, confirm Start audio, and keep holding until RELEASE PTT appears.

Reception detects both profiles. File blocks, error correction, interleaving and
selective repair use the native protocol: browser and terminal stations interoperate.
After a file transfer, the receiver can request missing blocks; the sender chooses
Repeat / send requested blocks under All actions. Only verified, complete files can
be downloaded. Retry save and Abort reception are available when needed.

Completed texts clear automatically. Up/Down, or the arrow buttons, recalls the
last 32 sent texts and restores the draft. Interrupted transmissions keep their text.
Message macros provides nine saved slots; Ctrl+X then 1–9 inserts them. All actions
includes repeat, reception controls, clear, macro assignment and session shutdown.
Technical log includes older/newer entries, live follow and text export.

Settings, macros and verified files use IndexedDB on this browser and origin.
History and pending transfers last for the session. Download files you want to keep;
clearing browser data removes local storage. Storage failures are shown explicitly.

## Install

Install app appears only where it can install. In Chromium browsers (Chrome, Edge,
Samsung Internet and others, on desktop and Android) it opens the browser's own
installation prompt, once the browser offers one. Safari has no installation API:
on iPhone and iPad the button shows Share → Add to Home Screen, on a Mac (Safari 17+)
File → Add to Dock. Browsers that cannot install, and the installed app, hide it.

After the first complete load, the app opens offline. Updates wait until you stop
audio and choose Update; an update never interrupts an active transmission.

## Audio

Use a current browser with WebAssembly, AudioWorklet and microphone permission.
Speaker selection depends on the browser; otherwise choose the output in your
system settings. Capture disables echo cancellation, noise suppression and automatic
gain where supported. The audio adapter handles 44.1, 48 and 96 kHz device clocks.

Keep the app visible and the device awake. Leaving the foreground stops audio and
cancels transmission. Mobile operating systems can suspend browser audio; reconnect
audio after an interruption. PTT is always manual. Completion confirms local playback,
not remote receipt. Real-radio checks are still required for each device and interface.

## Build

Install the official [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html),
version `4.0.20`, and activate its environment. CMake and Python 3 are also required.
No JavaScript package installation is needed.

```sh
emcmake cmake -S . -B build/wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm --parallel
python3 -m http.server 8080 --directory build/wasm/web
```

Open `http://localhost:8080`. To package the build:

```sh
python3 scripts/package-web.py build/wasm/web
```

Upload the generated folder to any HTTPS static host, including a subdirectory.
The server must serve `.wasm` as `application/wasm` and JavaScript, including `.mjs`,
as JavaScript. No special cross-origin isolation headers are required.
The manifest id is `blacksquelch`, so the installed app stays separate from other
installable apps on the same origin.

Regular pushes produce a downloadable artifact. Releases also include the static
PWA ZIP and its checksum. These workflows do not deploy a website.
