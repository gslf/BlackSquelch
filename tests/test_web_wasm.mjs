/* Real native <-> WebAssembly audio and selective repair; no JS packages required. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {spawnSync} from 'node:child_process';
const root = path.resolve(process.argv[2] || 'build/wasm/web');
const native = path.resolve(process.argv[3] || 'build/blacksquelch');
const {default: create} = await import(pathToFileURL(path.join(root, 'modem.mjs')));
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function station(call = 'IU2ABC', tones = 8) {
  const m = await create();
  m.FS.mkdirTree('/data'); m.FS.mkdirTree('/outgoing'); m.FS.chdir('/data');
  assert.equal(m._bs_web_init(), 1);
  m._bs_web_audio_ready(1, 0.02);
  assert.equal(m.ccall('bs_web_configure', 'number', ['string','number','number'], [call, tones, 0.5]), 1);
  const pcm = m._malloc(16384);
  const snapshot = () => JSON.parse(m.UTF8ToString(m._bs_web_snapshot()));
  function feed(samples) {
    for (let i = 0; i < samples.length; i += 5760) {
      const chunk = samples.subarray(i, i + 5760);
      m.HEAP16.set(chunk, pcm >> 1);
      assert.equal(m._bs_web_capture(pcm, chunk.length), chunk.length);
      m._bs_web_tick();
    }
  }
  const clock = () => feed(new Int16Array(480));
  return {m, pcm, snapshot, feed, clock};
}
async function transmit(sender, receiver) {
  const {m, pcm, snapshot, clock} = sender;
  assert.equal(snapshot().state, 1, snapshot().instruction);
  m._bs_web_key(32);
  await sleep(2030);
  clock();
  assert.equal(snapshot().state, 3, snapshot().instruction);
  const chunks = [];
  let consumed = 0, iterations = 0;
  while (snapshot().state === 3 || snapshot().state === 4) {
    assert.ok(++iterations < 50000, 'Transmission did not finish');
    const epoch = m._bs_web_generation();
    const n = m._bs_web_output(pcm, 5760);
    if (n) {
      const samples = m.HEAP16.slice(pcm >> 1, (pcm >> 1) + n);
      chunks.push(samples);
      receiver?.feed(samples);
      consumed += n;
      m._bs_web_ack(epoch, consumed);
    } else await sleep(5);
    clock();
  }
  assert.equal(snapshot().state, 5, snapshot().instruction);
  assert.match(snapshot().instruction, /completed/);
  const combined = new Int16Array(chunks.reduce((sum, chunk) => sum + chunk.length, 0));
  let position = 0;
  for (const chunk of chunks) { combined.set(chunk, position); position += chunk.length; }
  return combined;
}
async function idle(station) {
  const end = Date.now() + 4000;
  while (station.snapshot().state === 5 && Date.now() < end) { station.clock(); await sleep(20); }
  assert.equal(station.snapshot().state, 0);
}
function writeWave(file, samples) {
  const wave = Buffer.alloc(44 + samples.length * 2);
  wave.write('RIFF'); wave.writeUInt32LE(wave.length - 8, 4); wave.write('WAVEfmt ', 8);
  wave.writeUInt32LE(16,16); wave.writeUInt16LE(1,20); wave.writeUInt16LE(1,22);
  wave.writeUInt32LE(48000,24); wave.writeUInt32LE(96000,28); wave.writeUInt16LE(2,32); wave.writeUInt16LE(16,34);
  wave.write('data',36); wave.writeUInt32LE(samples.length * 2,40);
  for (let i = 0; i < samples.length; ++i) wave.writeInt16LE(samples[i],44+i*2);
  fs.writeFileSync(file,wave);
}
function pcmFromWave(file) {
  const data = fs.readFileSync(file);
  let index = 12;
  while (data.toString('ascii', index, index + 4) !== 'data') index += 8 + data.readUInt32LE(index + 4) + data.readUInt32LE(index + 4) % 2;
  const count = data.readUInt32LE(index + 4) / 2;
  return Int16Array.from({length: count}, (_, i) => data.readInt16LE(index + 8 + i * 2));
}
function nativeRun(...args) {
  const result = spawnSync(native, args, {encoding: 'utf8'});
  assert.equal(result.status, 0, result.stderr);
  return result.stdout;
}
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'blacksquelch-wasm-'));
try {
  for (const tones of [8,16]) {
    const rx = await station('IU2XYZ', tones === 8 ? 16 : 8);
    const fixture = path.join(temporary, `native-${tones}.wav`);
    nativeRun('--tones', String(tones), '--encode', 'IU2ABC', 'NATIVE TO BROWSER', fixture);
    rx.feed(pcmFromWave(fixture));
    assert.ok(rx.snapshot().conversation.some(text => text.includes('VERIFIED RX') && text.includes('NATIVE TO BROWSER')));
  }
  const tx = await station('IU2ABC',16);
  assert.equal(tx.m.ccall('bs_web_compose','number',['string'],['BROWSER TO NATIVE']),1);
  assert.equal(tx.m._bs_web_command('s'.charCodeAt(0)),1);
  const pcm = await transmit(tx);
  assert.equal(tx.snapshot().compose,'');
  tx.m._bs_web_key(256);
  assert.equal(tx.snapshot().compose,'BROWSER TO NATIVE');
  const wave = path.join(temporary,'browser.wav'); writeWave(wave,pcm);
  assert.match(nativeRun('--decode',wave),/BROWSER TO NATIVE/);
  // Cancel a prepared transmission: its draft must survive.
  await idle(tx);
  tx.m._bs_web_command('s'.charCodeAt(0)); tx.m._bs_web_key(27);
  assert.equal(tx.snapshot().compose,'BROWSER TO NATIVE');

  // Two equal-sized packets. Lose packet two, send a report, and repair only that packet.
  const fileTx = await station('IU2ABC',8), fileRx = await station('IU2XYZ',16);
  const bytes = Uint8Array.from({length:128},(_,i)=>(i*73)&255);
  fileTx.m.FS.writeFile('/outgoing/binary.dat',bytes);
  assert.equal(fileTx.m.ccall('bs_web_file','number',['string'],['/outgoing/binary.dat']),1);
  const fullDuration = fileTx.snapshot().duration;
  const allSamples = await transmit(fileTx);
  assert.equal(allSamples.length % 2,0);
  fileRx.feed(allSamples.subarray(0,allSamples.length/2));
  fileRx.feed(new Int16Array(9600));
  assert.equal(fileRx.snapshot().incoming.received,1);
  assert.equal(fileRx.snapshot().incoming.complete,false);
  assert.equal(fileRx.m._bs_web_command('m'.charCodeAt(0)),1);
  await transmit(fileRx,fileTx);
  await idle(fileTx);
  assert.equal(fileTx.m._bs_web_command('r'.charCodeAt(0)),1);
  assert.ok(fileTx.snapshot().duration < fullDuration * 0.6,'Repair repeated the whole file');
  await transmit(fileTx,fileRx);
  assert.equal(fileRx.snapshot().incoming.complete,true);
  assert.equal(fileRx.snapshot().incoming.saved,true);
  const names = fileRx.m.FS.readdir('/data/received-files').filter(name=>name.endsWith('binary.dat'));
  assert.equal(names.length,1);
  assert.deepEqual(fileRx.m.FS.readFile(`/data/received-files/${names[0]}`),bytes);
  // Explicit stream failure must preserve a text draft and never report success.
  const broken = await station();
  broken.m.ccall('bs_web_compose','number',['string'],['KEEP THIS']);
  broken.m._bs_web_command('s'.charCodeAt(0)); broken.m._bs_web_key(32);
  await sleep(2030); broken.clock();
  broken.m.ccall('bs_web_audio_fault',null,['string'],['Injected underrun']); broken.clock();
  assert.equal(broken.snapshot().audio,false);
  assert.equal(broken.snapshot().compose,'KEEP THIS');
  assert.match(broken.snapshot().instruction,/interrupted/);
  console.log('PASS WebAssembly: native interoperability in both modes, history, cancellation, binary files, selective repair and audio failure.');
} finally { fs.rmSync(temporary,{recursive:true,force:true}); }
