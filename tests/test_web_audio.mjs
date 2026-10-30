import assert from 'node:assert/strict';
import {CaptureResampler, RadioProcessor} from '../web/public/audio-worklet.js';

for (const rate of [44100,48000,96000]) {
  const values=[];
  const resampler=new CaptureResampler(rate,sample=>values.push(sample));
  const source=Float32Array.from({length:rate},(_,i)=>0.5*Math.sin(2*Math.PI*1500*i/rate));
  for(let i=0;i<source.length;i+=128) resampler.push(source.subarray(i,i+128));
  assert.ok(Math.abs(values.length-48000)<2,`Capture clock drift at ${rate}`);
  const energy=Math.sqrt(values.reduce((sum,value)=>sum+(value/32768)**2,0)/values.length);
  assert.ok(energy>0.34&&energy<0.36,`Resampling changed signal amplitude at ${rate}`);
}
// Test the actual worklet against a minimal host, without a browser or packages.
for(const rate of [44100,48000,96000]) {
  globalThis.sampleRate=rate;
  // The exported class uses a fallback base in Node; supply its control port locally.
  RadioProcessor.prototype.port={onmessage:null};
  const processor=new RadioProcessor();
  const events=[];
  const samples=Int16Array.from({length:9600},(_,i)=>Math.round(12000*Math.sin(2*Math.PI*1500*i/48000)));
  let sent=5760;
  processor.link={postMessage(value){
    events.push(value);
    if(value.type==='clock'&&sent<samples.length){
      const chunk=samples.slice(sent,sent+960);sent+=chunk.length;
      processor.message({type:'play',generation:7,samples:chunk});
    }
  }};
  processor.message({type:'reset',generation:7,active:true});
  processor.message({type:'play',generation:6,samples:new Int16Array(16000)});
  assert.equal(processor.write,0,'Stale audio entered a new transmission');
  processor.message({type:'play',generation:7,samples:samples.slice(0,5760)});
  processor.message({type:'end',generation:7,total:samples.length});
  let rendered=0;
  while(processor.played<samples.length&&rendered<rate){
    const output=new Float32Array(128);
    processor.process([[new Float32Array(128)]],[[output]]);
    rendered+=128;
  }
  assert.equal(processor.played,9600);
  assert.ok(Math.abs(rendered-rate*0.2)<=128,`Playback clock drift at ${rate}`);
  assert.equal(events.filter(event=>event.type==='fault').length,0);
  processor.message({type:'reset',generation:8,active:false});
  const silent=new Float32Array(128);processor.process([[silent]],[[silent]]);
  assert.ok(silent.every(sample=>sample===0),'Cancelled audio leaked to the output');
}
globalThis.sampleRate=48000;
RadioProcessor.prototype.port={onmessage:null};
const broken=new RadioProcessor(), faults=[];
broken.link={postMessage(value){if(value.type==='fault')faults.push(value);}};
broken.message({type:'reset',generation:1,active:true});
broken.message({type:'play',generation:1,samples:new Int16Array(1920)});
for(let i=0;i<20;i++) broken.process([[new Float32Array(128)]],[[new Float32Array(128)]]);
assert.equal(faults.length,1);
assert.match(faults[0].message,/underrun/);
assert.equal(broken.active,false);
console.log('PASS browser audio: 44.1/48/96 kHz clocks, resampling, generation isolation, cancellation and underrun detection.');
