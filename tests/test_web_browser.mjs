/* Browser integration via Chromium DevTools, using only Node's standard library. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import {spawn, spawnSync} from 'node:child_process';
import {DevTools} from './web-cdp.mjs';
const root = path.resolve(process.argv[2] || 'build/wasm/web');
const screenshots = path.resolve(process.argv[3] || 'build/web-tests');
fs.mkdirSync(screenshots,{recursive:true});
const profile = fs.mkdtempSync(path.join(os.tmpdir(),'blacksquelch-browser-'));
const binary = Buffer.from(Array.from({length:128},(_,i)=>(i*73)&255));
const fixture = path.join(profile,'fixture.png'), wave = path.join(profile,'fixture.wav');
fs.writeFileSync(fixture,binary);
const encoded=spawnSync(path.resolve(process.argv[4]||'build/blacksquelch'),['--tones','8','--encode-file','IU2XYZ',fixture,wave],{encoding:'utf8'});
assert.equal(encoded.status,0,encoded.stderr);
const mime = {'.html':'text/html','.js':'text/javascript','.mjs':'text/javascript','.wasm':'application/wasm','.css':'text/css','.png':'image/png','.svg':'image/svg+xml','.webmanifest':'application/manifest+json'};
const server = http.createServer((req,res)=>{
  if(req.url==='/__fixture.wav'){res.writeHead(200,{'Content-Type':'audio/wav'}).end(fs.readFileSync(wave));return;}
  const filename = path.resolve(root,'.'+decodeURIComponent(new URL(req.url,'http://localhost').pathname),req.url.split('?')[0].endsWith('/')?'index.html':'');
  if (!filename.startsWith(root+path.sep)) { res.writeHead(403).end(); return; }
  fs.readFile(filename,(error,bytes)=>{ if(error)res.writeHead(404).end();else res.writeHead(200,{'Content-Type':mime[path.extname(filename)]||'application/octet-stream'}).end(bytes); });
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const browser = spawn(process.env.CHROMIUM || 'chromium',[
  '--headless=new','--no-sandbox','--disable-dev-shm-usage','--use-fake-ui-for-media-stream','--use-fake-device-for-media-stream',
  '--autoplay-policy=no-user-gesture-required','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'
],{stdio:['ignore','ignore','pipe']});
let browserErrors='';
const socketUrl = await new Promise((resolve,reject)=>{
  const timeout=setTimeout(()=>reject(new Error('Chromium did not start: '+browserErrors)),20000);
  browser.stderr.on('data',chunk=>{
    browserErrors+=chunk;
    const match=browserErrors.match(/DevTools listening on (ws:\/\/\S+)/);
    if(match){clearTimeout(timeout);resolve(match[1]);}
  });
  browser.on('error',reject);
});
let page, controller;
try {
  controller = await DevTools.connect(socketUrl);
  const {targetId}=await controller.call('Target.createTarget',{url:'about:blank'});
  const port=new URL(socketUrl).port;
  const targets=await(await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  page=await DevTools.connect(targets.find(target=>target.id===targetId).webSocketDebuggerUrl);
  await page.call('Runtime.enable'); await page.call('Page.enable'); await page.call('Network.enable');
  await page.call('Page.addScriptToEvaluateOnNewDocument',{source:`{
    const OriginalWorker=Worker;
    window.Worker=class extends OriginalWorker{
      constructor(...args){super(...args);if(String(args[0]).endsWith('/worker.js')){
        window.__radioWorker=this;
        this.addEventListener('message',event=>{if(event.data.type==='snapshot')window.__snapshot=event.data.value;});
      }}
    };
  }`});
  await page.call('Emulation.setDeviceMetricsOverride',{width:1360,height:1000,deviceScaleFactor:1,mobile:false});
  await page.call('Page.navigate',{url:origin+'/'});
  await page.wait('document.getElementById("audio-toggle")?.disabled === false');
  await page.wait('navigator.serviceWorker.controller !== null');
  // Install app starts the browser's prompt. A prompt works once: without another, Chromium has nothing to start and hides the button.
  await page.evaluate(`{const event=new Event('beforeinstallprompt',{cancelable:true});window.__installPrompts=0;
    event.prompt=async()=>{window.__installPrompts++;};event.userChoice=Promise.resolve({outcome:'dismissed'});dispatchEvent(event);}`);
  await page.wait('!document.getElementById("install").hidden');
  await page.evaluate('document.getElementById("install").click()');
  await page.wait('window.__installPrompts === 1 && document.getElementById("install").hidden');
  assert.equal(await page.evaluate('document.getElementById("install-dialog").open'),false);
  assert.equal(await page.evaluate('document.querySelectorAll("script[src^=http],link[href^=http]").length'),0);
  assert.equal(await page.evaluate('document.documentElement.scrollWidth <= innerWidth'),true);
  await page.evaluate('document.getElementById("audio-toggle").click()');
  await page.wait('document.getElementById("settings-dialog").open');
  await page.evaluate('document.getElementById("callsign").value="IU2ABC"; document.getElementById("tones").value="16"; document.getElementById("settings-form").requestSubmit()');
  await page.wait('document.getElementById("audio-toggle").innerText.includes("Stop audio")',30000);
  assert.equal(await page.evaluate('document.getElementById("station-call").textContent'),'IU2ABC');
  await page.wait('!document.getElementById("message").disabled');
  await page.evaluate('document.getElementById("message").value="T"; document.getElementById("message").dispatchEvent(new Event("input",{bubbles:true}))');
  await page.evaluate('document.getElementById("prepare").click()');
  await page.wait('!document.getElementById("transmit").hidden');
  assert.equal(await page.evaluate('document.getElementById("message").value'),'T');
  await page.evaluate('document.getElementById("transmit").click()');
  await page.wait('document.getElementById("tx-label").textContent.includes("RELEASE THE RADIO PTT")',25000);
  assert.equal(await page.evaluate('document.getElementById("message").value'),'');
  await page.evaluate('document.getElementById("history-up").click()');
  await page.wait('document.getElementById("message").value === "T"');
  await page.wait('document.getElementById("tx-panel").hidden');
  // Macros use the same C manager and survive reload in IndexedDB.
  await page.evaluate('document.getElementById("macros-open").click()');
  await page.wait('document.getElementById("macros-dialog").open');
  await page.evaluate('document.getElementById("macro-0").value="CQ CQ TEST"; document.getElementById("macros-form").requestSubmit()');
  await page.wait('!document.getElementById("macros-dialog").open');
  await page.evaluate('document.getElementById("clear").click()');
  await page.wait('document.getElementById("message").value === ""');
  await page.call('Input.dispatchKeyEvent',{type:'keyDown',key:'x',code:'KeyX',modifiers:2});
  await page.call('Input.dispatchKeyEvent',{type:'keyUp',key:'x',code:'KeyX',modifiers:2});
  await page.call('Input.dispatchKeyEvent',{type:'keyDown',key:'1',code:'Digit1'});
  await page.call('Input.dispatchKeyEvent',{type:'keyUp',key:'1',code:'Digit1'});
  await page.wait('document.getElementById("message").value === "CQ CQ TEST"');
  await page.evaluate('document.getElementById("prepare").click()');
  await page.wait('!document.getElementById("transmit").hidden');
  await page.evaluate('document.getElementById("cancel-tx").click()');
  await page.wait('document.getElementById("tx-panel").hidden');
  assert.equal(await page.evaluate('document.getElementById("message").value'),'CQ CQ TEST');
  // Prepare an actual binary attachment through the browser File API, then cancel.
  const {root: documentRoot}=await page.call('DOM.getDocument');
  const {nodeId}=await page.call('DOM.querySelector',{nodeId:documentRoot.nodeId,selector:'#file-picker'});
  await page.call('DOM.setFileInputFiles',{nodeId,files:[fixture]});
  await page.wait('!document.getElementById("attachment").hidden');
  await page.evaluate('document.getElementById("prepare").click()');
  await page.wait('document.getElementById("tx-content").textContent.includes("fixture.png")');
  await page.evaluate('document.getElementById("cancel-tx").click()');
  await page.wait('document.getElementById("tx-panel").hidden');
  await page.evaluate('document.getElementById("audio-toggle").click()');
  await page.wait('document.getElementById("audio-toggle").innerText.includes("Start audio")');
  // Feed a real native radio waveform over the same PCM port used by the worklet.
  await page.evaluate(`(async()=>{
    const worker=window.__radioWorker, channel=new MessageChannel();
    let generation=0;
    channel.port1.onmessage=({data})=>{if(data.type==='reset')generation=data.generation;};
    worker.postMessage({type:'audio-connect',port:channel.port2,latency:0.02,session:999},[channel.port2]);
    worker.postMessage({type:'configure',call:'IU2ABC',tones:16,amplitude:0.5});
    await new Promise(resolve=>setTimeout(resolve,100));
    const bytes=await(await fetch('/__fixture.wav')).arrayBuffer(), data=new DataView(bytes);
    let offset=12;
    while(String.fromCharCode(...new Uint8Array(bytes,offset,4))!=='data')offset+=8+data.getUint32(offset+4,true)+data.getUint32(offset+4,true)%2;
    const count=data.getUint32(offset+4,true)/2;
    for(let i=0;i<count;i+=5760){
      const samples=Int16Array.from({length:Math.min(5760,count-i)},(_,j)=>data.getInt16(offset+8+(i+j)*2,true));
      channel.port1.postMessage({type:'clock',generation,played:0,samples},[samples.buffer]);
      await new Promise(resolve=>setTimeout(resolve,0));
    }
    channel.port1.close();
  })()`);
  await page.wait('document.querySelector(".file-download") && !window.__snapshot.storageSaving');
  const savedName=await page.evaluate('document.querySelector(".file-download").innerText');
  await controller.call('Browser.setDownloadBehavior',{behavior:'allow',downloadPath:path.join(profile,'downloads')});
  await page.evaluate('document.querySelector(".file-download").click()');
  const downloaded=path.join(profile,'downloads',savedName);
  for(let i=0;i<100&&!fs.existsSync(downloaded);i++)await new Promise(resolve=>setTimeout(resolve,50));
  assert.deepEqual(fs.readFileSync(downloaded),binary);
  await page.evaluate('document.getElementById("audio-toggle").click()');
  await page.wait('!window.__snapshot.audio && !window.__snapshot.storageSaving');
  for (const [width,height,mobile,name] of [[1360,1000,false,'desktop'],[390,844,true,'mobile'],[320,720,true,'small-mobile']]) {
    await page.call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile});
    assert.equal(await page.evaluate('document.documentElement.scrollWidth <= innerWidth'),true,`Overflow at ${width}px`);
    const {data}=await page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:true});
    fs.writeFileSync(path.join(screenshots,name+'.png'),Buffer.from(data,'base64'));
  }
  // Installed mode must never show an install button, even if a prompt event arrives.
  await page.call('Page.addScriptToEvaluateOnNewDocument',{source:"Object.defineProperty(navigator,'standalone',{get:()=>true});"});
  await page.call('Network.emulateNetworkConditions',{offline:true,latency:0,downloadThroughput:0,uploadThroughput:0});
  await page.evaluate('window.__beforeReload=true');
  await page.call('Page.reload');
  await page.wait('window.__beforeReload !== true && document.getElementById("audio-toggle") && !document.getElementById("audio-toggle").disabled');
  assert.equal(await page.evaluate('document.getElementById("station-call").textContent'),'IU2ABC');
  assert.equal(await page.evaluate('document.getElementById("tx-profile").textContent'),'16 tones');
  assert.equal(await page.evaluate('document.getElementById("install").hidden'),true);
  assert.equal(await page.evaluate('document.querySelectorAll(".file-download").length'),1,'Received file was not restored offline');
  await page.evaluate('document.getElementById("macros-open").click()');
  await page.wait('document.getElementById("macros-dialog").open');
  assert.equal(await page.evaluate('document.getElementById("macro-0").value'),'CQ CQ TEST');
  const secondTarget=await controller.call('Target.createTarget',{url:origin+'/'});
  const secondPages=await(await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const second=await DevTools.connect(secondPages.find(target=>target.id===secondTarget.targetId).webSocketDebuggerUrl);
  try {
    await second.wait('document.getElementById("notice-text")?.textContent.includes("already open")');
    assert.equal(await page.evaluate('document.getElementById("macro-0").value'),'CQ CQ TEST');
  } finally { second.close(); await controller.call('Target.closeTarget',{targetId:secondTarget.targetId}); }
  assert.equal(page.events.filter(event=>event.method==='Runtime.exceptionThrown').length,0,JSON.stringify(page.events.filter(event=>event.method==='Runtime.exceptionThrown')));
  console.log('PASS browser: live audio, PTT completion, history, macros, binary upload/download, persistent received files, mobile, offline and install visibility.');
} catch(error) {
  if(page){try {
    console.error(await page.evaluate('document.body.innerText'));
    const {data}=await page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:true});
    fs.writeFileSync(path.join(screenshots,'failure.png'),Buffer.from(data,'base64'));
    fs.writeFileSync(path.join(screenshots,'events.json'),JSON.stringify(page.events,null,2));
  } catch{}}
  throw error;
} finally {
  page?.close(); controller?.close();
  browser.kill('SIGTERM'); server.closeAllConnections(); server.close();
  await new Promise(resolve=>browser.exitCode!==null?resolve():browser.once('exit',resolve));
  fs.rmSync(profile,{recursive:true,force:true,maxRetries:10,retryDelay:100});
}
