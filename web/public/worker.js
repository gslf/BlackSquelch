import createBlackSquelch from './modem.mjs';

let module, audioPort, timer, pcm, generation = -1, running = false;
let lastSnapshot = '', lastFiles = '', previousState, syncPending = Promise.resolve();
let durable = true, lastPost = 0;
let audioSession = 0;
let pendingWrites = 0;
const call = (name, result, types = [], args = []) => module.ccall(name, result, types, args);
const notify = message => postMessage({type: 'notice', message});

function persist() {
  ++pendingWrites;
  const operation = syncPending.catch(() => {}).then(() => new Promise((resolve, reject) => {
    if (!durable) { reject(new Error('Browser storage is unavailable. Download received files before closing.')); return; }
    module.FS.syncfs(false, error => error ? reject(error) : resolve());
  }));
  syncPending = operation;
  return operation.catch(error => {
    module._bs_web_storage_failed();
    notify(`Changes could not be saved in browser storage. Download received files before closing. ${error.message || error}`);
  }).finally(() => { --pendingWrites; snapshot(true); });
}

function files() {
  try { return module.FS.readdir('/data/received-files').filter(name => name !== '.' && name !== '..').sort(); }
  catch { return []; }
}

function snapshot(force = false) {
  const now = performance.now();
  if (!force && now - lastPost < 100) return;
  lastPost = now;
  const value = JSON.parse(module.UTF8ToString(module._bs_web_snapshot()));
  const names = files();
  const key = JSON.stringify(names);
  if (key !== lastFiles) { lastFiles = key; if (names.length) void persist(); }
  if (previousState !== undefined && previousState !== value.state && value.state === 0) void persist();
  previousState = value.state;
  value.files = names;
  value.storage = durable;
  value.storageSaving = pendingWrites > 0;
  value.audioSession = audioSession;
  const serialized = JSON.stringify(value);
  if (force || serialized !== lastSnapshot) {
    lastSnapshot = serialized;
    postMessage({type: 'snapshot', value});
  }
}

function pump() {
  if (!module) return;
  module._bs_web_tick();
  if (audioPort) {
    const epoch = module._bs_web_generation();
    const active = !!module._bs_web_transmitting();
    if (epoch !== generation || active !== running) {
      generation = epoch;
      running = active;
      audioPort.postMessage({type: 'reset', generation, active});
    }
    if (active) {
      const count = module._bs_web_output(pcm, 5760);
      if (count) {
        const samples = module.HEAP16.slice(pcm >> 1, (pcm >> 1) + count);
        audioPort.postMessage({type: 'play', generation, samples}, [samples.buffer]);
      }
      if (module._bs_web_ending()) audioPort.postMessage({type: 'end', generation, total: module._bs_web_submitted()});
    }
  }
  snapshot();
}

function attachAudio(port, latency, session) {
  audioPort?.close();
  audioPort = port;
  generation = -1;
  audioSession = session;
  module._bs_web_audio_ready(1, latency);
  audioPort.onmessage = ({data}) => {
    try {
      if (data.type === 'clock') {
        if (data.samples?.length) {
          if (data.samples.length > 8192) throw new Error('Capture packet exceeds buffer.');
          module.HEAP16.set(data.samples, pcm >> 1);
          module._bs_web_capture(pcm, data.samples.length);
        }
        module._bs_web_ack(data.generation, data.played);
      } else if (data.type === 'fault') {
        call('bs_web_audio_fault', null, ['string'], [data.message]);
      }
      pump();
    } catch (error) { notify(error.message); disconnect('Audio processing failed. RELEASE THE RADIO PTT.'); }
  };
  audioPort.start();
}

function disconnect(reason) {
  call('bs_web_disconnect', null, ['string'], [reason]);
  pump();
  audioPort?.postMessage({type: 'reset', generation: module._bs_web_generation(), active: false});
  audioPort?.close();
  audioPort = undefined;
  snapshot(true);
}

async function initialize() {
  module = await createBlackSquelch({printErr: message => console.warn(message)});
  module.FS.mkdirTree('/data');
  module.FS.mkdirTree('/outgoing');
  try {
    module.FS.mount(module.IDBFS, {}, '/data');
    await new Promise((resolve, reject) => module.FS.syncfs(true, error => error ? reject(error) : resolve()));
  } catch {
    durable = false;
    notify('Browser storage is unavailable. Settings are temporary; download received files before closing.');
  }
  module.FS.chdir('/data');
  if (!module._bs_web_init()) throw new Error('Not enough memory to start the modem.');
  pcm = module._malloc(8192 * 2);
  if (!pcm) throw new Error('Not enough memory for audio buffers.');
  timer = setInterval(pump, 20);
  snapshot(true);
  postMessage({type: 'ready'});
}

self.onmessage = async ({data}) => {
  if (!module) return;
  try {
    switch (data.type) {
      case 'audio-connect': attachAudio(data.port, data.latency, data.session); break;
      case 'audio-disconnect': disconnect(data.reason || 'Audio stopped. RELEASE PTT IF HELD.'); break;
      case 'configure':
        if (!call('bs_web_configure', 'number', ['string', 'number', 'number'], [data.call, data.tones, data.amplitude]))
          throw new Error('Use a six-character callsign and available audio devices. Finish or cancel the current operation first.');
        await persist();
        break;
      case 'compose':
        if (!call('bs_web_compose', 'number', ['string'], [data.text])) throw new Error('Text is too long or the composer is locked.');
        break;
      case 'key': module._bs_web_key(data.key); break;
      case 'command':
        if (!module._bs_web_command(data.key.charCodeAt(0))) throw new Error('This action is unavailable during the current operation.');
        if (data.key === 'e') {
          const name = module.FS.readdir('/data').filter(name => /^blacksquelch-log-.*\.txt$/.test(name)).sort().at(-1);
          if (!name) throw new Error('Log export failed.');
          sendFile(`/data/${name}`, name);
          module.FS.unlink(`/data/${name}`);
        }
        if (data.key === 'q') disconnect('Session closed. RELEASE THE RADIO PTT.');
        break;
      case 'file': {
        if (!(data.bytes instanceof ArrayBuffer) || data.bytes.byteLength > 1048576) throw new Error('Files must be at most 1 MiB.');
        if (!data.name || new TextEncoder().encode(data.name).length > 96 || /[/\\\x00]/.test(data.name)) throw new Error('File names must contain 1–96 bytes without path separators.');
        for (const old of module.FS.readdir('/outgoing')) if (old !== '.' && old !== '..') module.FS.unlink(`/outgoing/${old}`);
        const path = `/outgoing/${data.name}`;
        module.FS.writeFile(path, new Uint8Array(data.bytes));
        const prepared = call('bs_web_file', 'number', ['string'], [path]);
        module.FS.unlink(path); // The C controller retains an immutable snapshot.
        if (!prepared) throw new Error(JSON.parse(module.UTF8ToString(module._bs_web_snapshot())).instruction);
        break;
      }
      case 'macros':
        if (!Array.isArray(data.messages) || data.messages.length !== 9) throw new Error('Nine macro slots are required.');
        for (let i = 0; i < 9; i++)
          if (!call('bs_web_macro', 'number', ['number', 'string'], [i, data.messages[i]])) throw new Error(`Macro ${i + 1} contains unsupported text.`);
        module._bs_web_key(19);
        await persist();
        if (data.insert && !module._bs_web_command(String(data.insert).charCodeAt(0)))
          throw new Error('Macro saved. Close settings before inserting it into a message.');
        break;
      case 'download':
        if (!files().includes(data.name)) throw new Error('Received file not found.');
        sendFile(`/data/received-files/${data.name}`, data.name);
        break;
      default: throw new Error('Unknown worker request.');
    }
    pump();
    snapshot(true);
  } catch (error) { notify(error.message || String(error)); snapshot(true); }
};

function sendFile(path, name) {
  const bytes = module.FS.readFile(path);
  postMessage({type: 'download', name, bytes}, [bytes.buffer]);
}

function fatal(error) {
  clearInterval(timer);
  postMessage({type: 'fatal', message: `The modem could not start: ${error.message}. Reload to retry.`});
}
if (navigator.locks?.request) {
  navigator.locks.request('blacksquelch-radio-session', {mode: 'exclusive', ifAvailable: true}, async lock => {
    if (!lock) throw new Error('BlackSquelch is already open in another tab or app window. Close that session, then reload');
    await initialize();
    await new Promise(() => {}); // A dedicated worker owns the session until its page closes.
  }).catch(fatal);
} else initialize().catch(fatal);
