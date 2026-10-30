export class BrowserAudio {
  constructor(worker, notice) { this.worker = worker; this.notice = notice; this.stopping = false; this.session = 0; this.operation = 0; this.confirmed = false; }
  async start(input, output) {
    if (!isSecureContext || !navigator.mediaDevices?.getUserMedia) throw new Error('Audio requires HTTPS or localhost and microphone permission.');
    const Context = window.AudioContext || window.webkitAudioContext;
    if (!Context || !window.AudioWorkletNode) throw new Error('This browser does not support AudioWorklet. Use a current Safari, Firefox, Chrome or Edge.');
    await this.stop(false);
    // Resume within the click gesture, before requesting microphone permission (including iOS).
    try { this.context = new Context({sampleRate: 48000, latencyHint: 'interactive'}); }
    catch { this.context = new Context({latencyHint: 'interactive'}); }
    const context = this.context;
    const operation = this.operation;
    ++this.session;
    this.confirmed = false;
    const resumed = context.resume();
    try {
      await resumed;
      const stream = await navigator.mediaDevices.getUserMedia({audio: {
        deviceId: input && input !== 'default' ? {exact: input} : undefined,
        channelCount: {ideal: 1}, sampleRate: {ideal: 48000},
        echoCancellation: false, noiseSuppression: false, autoGainControl: false
      }, video: false});
      if (operation !== this.operation) {
        for (const track of stream.getTracks()) track.stop();
        throw new Error('Audio setup was cancelled.');
      }
      this.stream = stream;
      if (output && output !== 'default') {
        if (!context.setSinkId) throw new Error('This browser uses the system speaker. Choose Default, or change the output in your system settings.');
        await context.setSinkId(output);
      }
      await context.audioWorklet.addModule(new URL('./audio-worklet.js', import.meta.url));
      if (operation !== this.operation) throw new Error('Audio setup was cancelled.');
      this.node = new AudioWorkletNode(context, 'blacksquelch-radio', {numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1], channelCount: 1, channelCountMode: 'explicit'});
      this.source = context.createMediaStreamSource(this.stream);
      this.source.connect(this.node);
      this.node.connect(context.destination);
      const channel = new MessageChannel();
      this.node.port.postMessage({type: 'attach', port: channel.port1}, [channel.port1]);
      const latency = Math.max(0.1, (context.baseLatency || 0) + (context.outputLatency || 0) + 0.08);
      this.worker.postMessage({type: 'audio-connect', port: channel.port2, latency, session: this.session}, [channel.port2]);
      await context.resume();
      this.node.onprocessorerror = () => this.fail('Audio processor stopped. RELEASE THE RADIO PTT.');
      for (const track of this.stream.getTracks()) track.onended = () => this.fail('Microphone disconnected. RELEASE THE RADIO PTT.');
      context.onstatechange = () => {
        if (!this.stopping && (context.state === 'interrupted' || context.state === 'suspended' || context.state === 'closed'))
          this.fail('Audio was suspended. RELEASE THE RADIO PTT, then reconnect audio.');
      };
      const settings = this.stream.getAudioTracks()[0].getSettings();
      if (settings.echoCancellation || settings.noiseSuppression || settings.autoGainControl)
        this.notice('The browser kept microphone voice processing enabled. Disable audio enhancements in system settings for radio use.');
      if (navigator.wakeLock) {
        try { this.lock = await navigator.wakeLock.request('screen'); } catch { /* Optional; visibility handling still stops interrupted audio. */ }
      }
      return context.sampleRate;
    } catch (error) { await this.stop(false); throw error; }
  }
  fail(message) {
    if (this.stopping) return;
    this.notice(message);
    void this.stop(true, message);
  }
  async stop(notify = true, reason = 'Audio stopped. RELEASE PTT IF HELD.') {
    ++this.operation;
    this.stopping = true;
    this.confirmed = false;
    if (notify) this.worker.postMessage({type: 'audio-disconnect', reason});
    const context = this.context;
    this.context = undefined;
    if (context) context.onstatechange = null;
    if (this.node) { this.node.onprocessorerror = null; this.node.disconnect(); this.node = undefined; }
    this.source?.disconnect();
    this.source = undefined;
    if (this.stream) {
      for (const track of this.stream.getTracks()) { track.onended = null; track.stop(); }
      this.stream = undefined;
    }
    if (context && context.state !== 'closed') await context.close().catch(() => {});
    await this.lock?.release().catch(() => {});
    this.lock = undefined;
    this.stopping = false;
  }
}
