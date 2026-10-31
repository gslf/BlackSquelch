/* A complete app shell: no CDN or network dependency after the first installation. */
const VERSION = '__BUILD__';
const PREFIX = `blacksquelch:${self.registration.scope}:`;
const CACHE = PREFIX + VERSION;
const ASSETS = ['./', './index.html', './style.css', './app.js', './audio.js', './audio-worklet.js', './worker.js', './modem.mjs', './modem.wasm', './manifest.webmanifest', './icons/icon.svg', './icons/icon-32.png', './icons/icon-192.png', './icons/icon-512.png', './icons/maskable-512.png', './icons/apple-touch-icon.png'];
self.addEventListener('install', event => {
  event.waitUntil(caches.open(CACHE).then(cache => cache.addAll(ASSETS.map(path => new Request(path, {cache: 'reload'})))));
});
self.addEventListener('activate', event => {
  event.waitUntil((async () => {
    for (const key of await caches.keys()) if (key.startsWith(PREFIX) && key !== CACHE) await caches.delete(key);
    await self.clients.claim();
  })());
});
self.addEventListener('message', event => { if (event.data === 'ACTIVATE') void self.skipWaiting(); });
self.addEventListener('fetch', event => {
  const request = event.request;
  const url = new URL(request.url);
  if (request.method !== 'GET' || url.origin !== self.location.origin || !url.href.startsWith(self.registration.scope)) return;
  event.respondWith((async () => {
    const cache = await caches.open(CACHE);
    const cached = await cache.match(request);
    if (cached) return cached;
    if (request.mode === 'navigate') return (await cache.match('./index.html')) || fetch(request);
    return fetch(request);
  })());
});
