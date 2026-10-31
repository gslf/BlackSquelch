import {BrowserAudio} from './audio.js';

const $ = id => document.getElementById(id);
const worker = new Worker(new URL('./worker.js', import.meta.url), {type: 'module'});
let state, ready = false, attachment, pendingDraft = null, prefix = false, deferredInstall, registration;
let previousConversation = '', previousLog = '', previousFiles = '', previousState, lastFocus;
let changingAudio = false;
const audio = new BrowserAudio(worker, notice);
const send = value => worker.postMessage(value);
const command = key => send({type: 'command', key});
const enabled = key => !!state?.commands?.find(item => item.key === key)?.enabled;
const states = ['Listening', 'Prepared', 'Countdown', 'Transmitting', 'Draining audio', 'Release PTT', 'Setup', 'Choose file', 'Macros'];
// BS_MACROS is 8; keep numeric state values aligned with include/app.h.
const MACROS = 8;

function notice(message) {
  $('notice-text').textContent = message;
  $('notice').hidden = false;
  for (const old of document.querySelectorAll('.dialog-error')) old.remove();
  const dialog = document.querySelector('dialog[open]');
  if (dialog) {
    const error = document.createElement('p'); error.className = 'dialog-note dialog-error'; error.setAttribute('role', 'alert'); error.textContent = message;
    const actions = dialog.querySelector('.dialog-actions');
    (actions?.parentNode || dialog).insertBefore(error, actions || null);
    error.scrollIntoView({block: 'nearest'});
  }
}
function storedDevices() {
  try { return JSON.parse(localStorage.getItem('blacksquelch-devices') || '{}'); } catch { return {}; }
}
function showDialog(id) {
  const dialog = $(id);
  if (dialog.open) return;
  for (const other of document.querySelectorAll('dialog[open]')) other.close();
  lastFocus = document.activeElement;
  dialog.showModal();
}
function closeDialog(id, cancel = true) {
  const dialog = $(id);
  if (!dialog.open) return;
  dialog.close();
  if (cancel && id === 'settings-dialog' && changingAudio) void audio.stop(true, 'Audio setup cancelled.');
  if (cancel && id === 'settings-dialog' && state?.state === 6) command('c');
  if (cancel && id === 'macros-dialog' && state?.state === MACROS) command('c');
  lastFocus?.focus?.();
}
function openSettings() {
  if (!state || (!enabled('o') && state.state !== 6)) return;
  $('callsign').value = state.call;
  $('tones').value = String(state.tones || 8);
  $('output-level').value = Math.round((state.amplitude || 0.5) * 100);
  $('output-value').textContent = `${$('output-level').value}%`;
  showDialog('settings-dialog');
  void devices();
}
async function devices() {
  if (!navigator.mediaDevices?.enumerateDevices) return;
  try {
    const list = await navigator.mediaDevices.enumerateDevices();
    const saved = storedDevices();
    for (const [id, kind, fallback] of [['input-device', 'audioinput', 'Default microphone'], ['output-device', 'audiooutput', 'Default speaker']]) {
      const select = $(id);
      const selected = select.value === 'default' ? saved[id] || 'default' : select.value;
      select.replaceChildren(new Option(fallback, 'default'));
      const canChoose = kind === 'audioinput' || !!(window.AudioContext || window.webkitAudioContext)?.prototype.setSinkId;
      if (canChoose) for (const item of list.filter(item => item.kind === kind && item.deviceId && item.deviceId !== 'default'))
        select.add(new Option(item.label || `${kind === 'audioinput' ? 'Microphone' : 'Speaker'} ${select.length}`, item.deviceId));
      select.value = [...select.options].some(option => option.value === selected) ? selected : 'default';
    }
  } catch (error) { notice(`Could not list audio devices: ${error.message}`); }
}

async function startAudio(config, selectedDevices) {
  if (changingAudio) return;
  changingAudio = true;
  $('save-settings').disabled = $('audio-toggle').disabled = true;
  try {
    const input = selectedDevices?.['input-device'] || $('input-device').value || 'default';
    const output = selectedDevices?.['output-device'] || $('output-device').value || 'default';
    if (state?.audio) send({type: 'audio-disconnect', reason: 'Changing audio devices. RELEASE PTT IF HELD.'});
    const rate = await audio.start(input, output);
    send({type: 'configure', ...config});
    try { localStorage.setItem('blacksquelch-devices', JSON.stringify({'input-device': input, 'output-device': output})); }
    catch { notice('Audio is ready, but device selections could not be saved.'); }
    $('station-description').textContent = `Audio ready · ${Math.round(rate / 100) / 10} kHz`;
    closeDialog('settings-dialog', false);
    await devices();
    if (navigator.storage?.persist) void navigator.storage.persist().catch(() => {});
  } catch (error) { notice(`Could not start audio: ${error.message}`); await audio.stop(true); }
  finally { changingAudio = false; $('save-settings').disabled = false; if (state) render(state); }
}

function render(value) {
  state = value;
  $('app-version').textContent = `WebAssembly · v${value.version}`;
  const tx = value.state >= 1 && value.state <= 5;
  const idle = value.state === 0;
  $('connection-status').textContent = value.audio ? (value.receiving ? 'Receiving' : states[value.state] || 'Ready') : 'Audio offline';
  $('connection-status').classList.toggle('live', value.audio);
  $('station-call').textContent = value.call || '------';
  if (!value.audio) $('station-description').textContent = value.call ? 'Audio is stopped' : 'Set your callsign to get started';
  $('tx-profile').textContent = `${value.tones || 8} tones`;
  const level = Math.min(1, Math.max(0, value.level || 0));
  $('input-level').value = level;
  $('input-level-value').textContent = `${Math.round(level * 100)}%`;
  $('rx-dot').classList.toggle('active', value.audio);
  $('rx-status').textContent = value.receiving ? `Receiving MFSK · quality ${Math.round(value.confidence * 100)}%` : value.audio ? 'Listening for incoming messages' : ready ? 'Start audio to listen' : 'Starting WebAssembly…';
  $('message').disabled = !ready || !idle;
  if (pendingDraft === value.compose) pendingDraft = null;
  if (pendingDraft === null && $('message').value !== value.compose) $('message').value = value.compose;
  $('character-count').textContent = `${$('message').value.length} / 512`;
  $('prepare').disabled = !ready || !value.audio || !idle || value.receiving || (!attachment && !$('message').value);
  $('prepare').firstChild.textContent = attachment ? 'Prepare file ' : 'Prepare message ';
  $('attach-file').disabled = !ready || !enabled('t');
  $('clear').disabled = !idle || (!value.compose && !attachment);
  $('history-up').disabled = $('history-down').disabled = !ready || !(idle || value.state === 5);
  $('settings-open').disabled = !ready || (!enabled('o') && value.state !== 6);
  $('macros-open').disabled = !ready || !enabled('k');
  $('commands-open').disabled = !ready;
  $('log-open').disabled = !ready || !enabled('l');
  $('audio-toggle').disabled = !ready || changingAudio || (!value.audio && !idle && value.state !== 6);
  $('audio-toggle').querySelector('span').textContent = value.audio ? 'Stop audio' : 'Start audio';
  $('tx-panel').hidden = !tx;
  $('tx-instruction').textContent = value.instruction;
  $('operator-status').textContent = tx ? '' : value.instruction;
  $('tx-content').textContent = value.frozen;
  $('tx-label').textContent = value.state === 1 ? `PREPARED · ${value.duration.toFixed(1)} SECONDS` : value.state === 5 ? 'RELEASE THE RADIO PTT' : 'KEEP THE RADIO PTT HELD';
  $('transmit').hidden = value.state !== 1;
  $('transmit').disabled = !value.audio || value.receiving;
  $('cancel-tx').hidden = value.state === 5;
  $('tx-progress').value = value.state === 5 ? 1 : value.duration ? Math.min(1, value.played / value.duration) : 0;
  $('report').disabled = !enabled('m');
  $('report').textContent = value.incoming.complete ? 'Report complete receipt' : 'Request missing blocks';
  $('retry-save').disabled = !enabled('v');
  $('abort-rx').disabled = !enabled('a');
  $('file-status').textContent = value.fileStatus;
  $('incoming').hidden = !value.incoming.active;
  $('incoming-name').textContent = value.incoming.name;
  $('incoming-progress').value = value.incoming.blocks ? value.incoming.received / value.incoming.blocks : 0;
  $('incoming-status').textContent = value.incoming.complete ? value.storageSaving ? 'Verified · saving to browser storage…' : value.incoming.saved ? 'Complete · checksum verified' : 'Verified · save pending' : `${value.incoming.received} / ${value.incoming.blocks} blocks. Request only the missing blocks when the sender finishes.`;
  $('log-panel').hidden = !value.showLog;
  $('export-log').disabled = !enabled('e');
  for (const button of document.querySelectorAll('[data-command]')) button.disabled = !enabled(button.dataset.command);
  const conversation = JSON.stringify(value.conversation);
  if (conversation !== previousConversation && value.conversation.length) {
    previousConversation = conversation;
    const entries = value.conversation.map(message => {
      const entry = document.createElement('article');
      const parts = message.match(/^\[([^\]]+)\] (TX PLAYED|TX INTERRUPTED|VERIFIED RX|UNVERIFIED RX \/ claimed callsign|FILE RECEIVED|REMOTE RECEIPT|REPAIR REQUEST) ([A-Z0-9]{6}): ([\s\S]*)$/);
      if (!parts) { entry.className = 'message-entry'; entry.textContent = message; return entry; }
      const [, time, kind, call, text] = parts;
      entry.className = `message-entry${kind.startsWith('TX ') ? ' tx' : kind.startsWith('UNVERIFIED') ? ' unverified' : ''}`;
      const heading = document.createElement('div'); heading.className = 'message-meta';
      const station = document.createElement('strong'); station.textContent = call;
      const status = document.createElement('span');
      status.textContent = {'TX PLAYED': 'Played locally', 'TX INTERRUPTED': 'Interrupted', 'VERIFIED RX': 'Verified message', 'FILE RECEIVED': 'Verified file', 'REMOTE RECEIPT': 'Receipt confirmed', 'REPAIR REQUEST': 'Repair requested'}[kind] || 'Unverified · claimed callsign';
      const timestamp = document.createElement('time'); timestamp.textContent = time;
      heading.append(station, status, timestamp);
      const body = document.createElement('p'); body.textContent = kind === 'FILE RECEIVED' ? text.replace(/^FILE RECEIVED: received-files\//, '') : text;
      entry.append(heading, body);
      return entry;
    });
    $('conversation').replaceChildren(...entries);
    $('conversation').scrollTop = $('conversation').scrollHeight;
  }
  const log = value.log.concat(value.current ? [value.current] : []).join('\n');
  if (log !== previousLog) {
    previousLog = log;
    $('technical-log').textContent = log || 'No log entries yet.';
    if (!value.scroll) $('technical-log').scrollTop = $('technical-log').scrollHeight;
  }
  const files = JSON.stringify(value.files);
  if (files !== previousFiles && value.files.length) {
    previousFiles = files;
    $('received-files').replaceChildren(...value.files.map(name => {
      const button = document.createElement('button');
      button.className = 'file-download';
      const icon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
      const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
      use.setAttribute('href', '#download-icon'); icon.append(use);
      const label = document.createElement('span'); label.textContent = name;
      button.append(icon, label);
      button.addEventListener('click', () => send({type: 'download', name}));
      return button;
    }));
  }
  if (value.state === MACROS && previousState !== MACROS) openMacros(value);
  if (value.state === 1 && previousState !== 1) $('tx-panel').scrollIntoView({block: 'nearest', behavior: 'instant'});
  if (value.state !== MACROS && $('macros-dialog').open) closeDialog('macros-dialog', false);
  previousState = value.state;
  if ($('commands-dialog').open) renderCommands();
  $('update').disabled = !!value.audio || tx || value.storageSaving;
}

function openMacros(value) {
  const messages = [...value.macros];
  if (value.macroEditing) messages[value.macroSlot] = value.macroText;
  $('macro-fields').replaceChildren(...messages.map((text, index) => {
    const row = document.createElement('div'); row.className = 'macro-row';
    const label = document.createElement('label'); label.htmlFor = `macro-${index}`; label.textContent = `Macro ${index + 1} · Ctrl+X, ${index + 1}`;
    const input = document.createElement('textarea'); input.id = label.htmlFor; input.maxLength = 512; input.rows = 2; input.value = text; input.spellcheck = false;
    const insert = document.createElement('button'); insert.type = 'button'; insert.className = 'button quiet'; insert.textContent = 'Insert';
    insert.disabled = !value.canInsertMacro || value.quit;
    insert.addEventListener('click', () => {
      // Use the shared controller's insertion and length validation after saving.
      saveMacros(index + 1);
    });
    row.append(label, input, insert); return row;
  }));
  showDialog('macros-dialog');
}
function saveMacros(insert) {
  send({type: 'macros', messages: Array.from({length: 9}, (_, i) => $(`macro-${i}`).value), insert});
}
function renderCommands() {
  let group = '';
  const children = [];
  for (const item of state.commands) {
    if (item.group !== group) {
      group = item.group;
      const heading = document.createElement('h3'); heading.textContent = group; children.push(heading);
    }
    const button = document.createElement('button'); button.className = 'command'; button.disabled = !item.enabled;
    const label = document.createElement('span'); label.textContent = item.label;
    const key = document.createElement('kbd'); key.textContent = `C-x ${item.key}`;
    button.append(label, key);
    button.addEventListener('click', () => { closeDialog('commands-dialog', false); act(item.key); });
    children.push(button);
  }
  $('commands').replaceChildren(...children);
}
function act(key) {
  if (key === 'o') return openSettings();
  if (key === 'd') return void devices();
  if (key === 't') return $('file-picker').click();
  if (key === 's') return void prepare();
  if (key === 'q') void audio.stop(true, 'Session closed. RELEASE THE RADIO PTT.');
  if (key === 'u') { attachment = undefined; updateAttachment(); pendingDraft = null; }
  command(key);
}
async function prepare() {
  if (!state?.audio || state.state !== 0 || state.receiving) return;
  if (attachment) {
    const file = attachment;
    $('prepare').disabled = true;
    try {
      const bytes = await file.arrayBuffer();
      worker.postMessage({type: 'file', name: file.name, bytes}, [bytes]);
      attachment = undefined; updateAttachment();
    } catch (error) { notice(`Could not read file: ${error.message}`); }
  } else command('s');
}
function updateAttachment() {
  $('attachment').hidden = !attachment;
  $('attachment-name').textContent = attachment ? `${attachment.name} · ${attachment.size.toLocaleString()} bytes` : '';
  if (state) render(state);
}

worker.onmessage = ({data}) => {
  if (data.type === 'snapshot') {
    render(data.value);
    if (data.value.audio && data.value.audioSession === audio.session) audio.confirmed = true;
    if (!data.value.audio && data.value.audioSession === audio.session && audio.confirmed && audio.context && !changingAudio) void audio.stop(false);
  } else if (data.type === 'ready') { ready = true; if (state) render(state); }
  else if (data.type === 'notice') notice(data.message);
  else if (data.type === 'fatal') { ready = false; notice(data.message); void audio.stop(false); if (state) render(state); }
  else if (data.type === 'download') {
    const url = URL.createObjectURL(new Blob([data.bytes], {type: 'application/octet-stream'}));
    const link = document.createElement('a'); link.href = url; link.download = data.name; document.body.append(link); link.click(); link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
  }
};
worker.onerror = event => { ready = false; notice(`The modem worker stopped: ${event.message}. RELEASE THE RADIO PTT and reload.`); void audio.stop(false); if (state) render(state); };

$('message').addEventListener('input', () => {
  pendingDraft = $('message').value;
  send({type: 'compose', text: pendingDraft});
  $('character-count').textContent = `${pendingDraft.length} / 512`;
  $('prepare').disabled = !state?.audio || !!state?.receiving || (!attachment && !pendingDraft);
});
$('message').addEventListener('keydown', event => {
  if (event.isComposing) return;
  if (event.key === 'ArrowUp' || event.key === 'ArrowDown') { event.preventDefault(); send({type: 'key', key: event.key === 'ArrowUp' ? 256 : 257}); }
  if (event.key === 'Enter' && !event.shiftKey) { event.preventDefault(); void prepare(); }
});
$('history-up').onclick = () => send({type: 'key', key: 256});
$('history-down').onclick = () => send({type: 'key', key: 257});
$('prepare').onclick = prepare;
$('transmit').onclick = () => { $('transmit').disabled = true; send({type: 'key', key: 32}); };
$('cancel-tx').onclick = () => send({type: 'key', key: 27});
$('attach-file').onclick = () => $('file-picker').click();
$('file-picker').onchange = () => {
  const file = $('file-picker').files[0];
  $('file-picker').value = '';
  if (!file) return;
  if (file.size > 1048576) return notice('Files must be at most 1 MiB (1048576 bytes).');
  if (new TextEncoder().encode(file.name).length > 96) return notice('File names must contain at most 96 bytes. Rename the file and try again.');
  attachment = file; updateAttachment();
};
$('attachment-remove').onclick = () => { attachment = undefined; updateAttachment(); };
$('clear').onclick = () => { attachment = undefined; pendingDraft = null; updateAttachment(); if (enabled('u')) command('u'); };
$('settings-open').onclick = openSettings;
$('settings-form').onsubmit = event => {
  event.preventDefault();
  void startAudio({call: $('callsign').value.toUpperCase(), tones: Number($('tones').value), amplitude: Number($('output-level').value) / 100});
};
$('output-level').oninput = () => { $('output-value').textContent = `${$('output-level').value}%`; };
$('refresh-devices').onclick = devices;
navigator.mediaDevices?.addEventListener('devicechange', devices);
$('audio-toggle').onclick = () => {
  if (state?.audio) return void audio.stop(true);
  if (!state?.call) return openSettings();
  const saved = storedDevices();
  return void startAudio({call: state.call, tones: state.tones, amplitude: state.amplitude}, saved);
};
$('macros-open').onclick = () => command('k');
$('macros-form').onsubmit = event => { event.preventDefault(); saveMacros(); };
$('commands-open').onclick = () => { renderCommands(); showDialog('commands-dialog'); };
$('log-open').onclick = () => command('l');
$('log-close').onclick = () => command('l');
$('export-log').onclick = () => command('e');
$('report').onclick = () => command('m');
$('retry-save').onclick = () => command('v');
$('abort-rx').onclick = () => command('a');
$('notice-close').onclick = () => { $('notice').hidden = true; };
for (const button of document.querySelectorAll('[data-command]')) button.onclick = () => command(button.dataset.command);
for (const button of document.querySelectorAll('[data-close]')) button.onclick = () => closeDialog(button.dataset.close);
for (const dialog of document.querySelectorAll('dialog')) dialog.addEventListener('cancel', event => { event.preventDefault(); closeDialog(dialog.id); });

document.addEventListener('keydown', event => {
  if (!ready || event.isComposing) return;
  if (event.ctrlKey && event.key.toLowerCase() === 'x') { event.preventDefault(); prefix = true; return; }
  if (prefix) {
    prefix = false;
    event.preventDefault();
    const key = event.key.toLowerCase();
    if (enabled(key)) act(key);
    return;
  }
  if (event.key === 'Escape' && !document.querySelector('dialog[open]')) { event.preventDefault(); send({type: 'key', key: 27}); }
  if (event.ctrlKey && event.key.toLowerCase() === 's' && $('macros-dialog').open) { event.preventDefault(); saveMacros(); }
  if (event.code === 'Space' && state?.state === 1 && !document.querySelector('dialog[open]') && !['INPUT', 'TEXTAREA', 'BUTTON', 'SELECT'].includes(event.target.tagName)) {
    event.preventDefault(); send({type: 'key', key: 32});
  }
});
window.addEventListener('blur', () => { prefix = false; });
window.addEventListener('pagehide', () => { void audio.stop(true, 'App left the foreground. RELEASE THE RADIO PTT.'); });
document.addEventListener('visibilitychange', () => {
  if (document.hidden && audio.context) void audio.stop(true, 'App left the foreground. RELEASE THE RADIO PTT, then restart audio.');
});
window.addEventListener('beforeunload', event => {
  if ((state?.state >= 1 && state.state <= 4) || state?.storageSaving) { event.preventDefault(); event.returnValue = ''; }
});

const displayModes = ['standalone', 'minimal-ui', 'fullscreen'].map(mode => matchMedia(`(display-mode: ${mode})`));
function installed() { return navigator.standalone === true || displayModes.some(query => query.matches); }
// Chromium browsers, desktop and Android, start installation from their own prompt (beforeinstallprompt).
// Safari has no such API: on iPhone, iPad and Mac the button can only show the steps.
const ios = /iPad|iPhone|iPod/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
const safariMac = !ios && /Macintosh/.test(navigator.userAgent) && Number(navigator.userAgent.match(/Version\/(\d+).*Safari/)?.[1]) >= 17;
const installSteps = ios ? 'Tap Share, choose Add to Home Screen, then tap Add. Open BlackSquelch from its new Home Screen icon.'
  : safariMac ? 'In Safari, choose File, then Add to Dock. Open BlackSquelch from the Dock or Launchpad.' : null;
// Shown only when a click can install: no prompt and no steps means this browser cannot.
function installVisibility() { $('install').hidden = installed() || !registration || !(deferredInstall || installSteps); }
for (const mode of displayModes) mode.addEventListener('change', installVisibility);
window.addEventListener('beforeinstallprompt', event => { event.preventDefault(); deferredInstall = event; installVisibility(); });
window.addEventListener('appinstalled', () => { deferredInstall = undefined; installVisibility(); });
$('install').onclick = async () => {
  if (installed()) return;
  if (deferredInstall) {
    // A prompt works once; the browser announces a new one if it offers installation again.
    const prompt = deferredInstall; deferredInstall = undefined; installVisibility();
    try { await prompt.prompt(); await prompt.userChoice; } catch (error) { notice(`Installation could not start: ${error.message}`); }
  } else if (installSteps) {
    $('install-help').textContent = installSteps;
    showDialog('install-dialog');
  }
};
function offlineStatus() { $('offline').hidden = navigator.onLine; }
window.addEventListener('online', offlineStatus); window.addEventListener('offline', offlineStatus); offlineStatus();
let refreshing = false;
if ('serviceWorker' in navigator && isSecureContext) {
  navigator.serviceWorker.register('./sw.js', {scope: './'}).then(result => {
    registration = result;
    installVisibility();
    function updateAvailable() { if (result.waiting && navigator.serviceWorker.controller) $('update-notice').hidden = false; }
    updateAvailable();
    result.addEventListener('updatefound', () => result.installing?.addEventListener('statechange', updateAvailable));
    $('update').onclick = () => { if (!state?.audio && !state?.storageSaving && !(state?.state >= 1 && state.state <= 5)) result.waiting?.postMessage('ACTIVATE'); };
  }).catch(error => notice(`Offline installation is unavailable: ${error.message}`));
  navigator.serviceWorker.addEventListener('controllerchange', () => {
    if (!refreshing && registration?.active && navigator.serviceWorker.controller) {
      // First activation needs no reload. A waiting update does.
      if (!$('update-notice').hidden) { refreshing = true; location.reload(); }
    }
  });
} else notice('Offline installation and microphone audio require HTTPS or localhost.');
