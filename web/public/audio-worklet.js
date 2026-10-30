/* Raw mono audio only. DSP/protocol processing runs in the C WebAssembly worker. */
export class CaptureResampler {
  constructor(rate, emit) {
    this.step = rate / 48000;
    this.next = rate / 48000;
    this.started = false;
    this.previous = 0;
    this.emit = emit;
  }
  push(input) {
    for (let i = 0; i < input.length; ++i) {
      const sample = input[i];
      if (!this.started) {
        this.started = true;
        this.previous = sample;
        this.emit(Math.max(-32768, Math.min(32767, Math.round(sample * 32768))));
        continue;
      }
      while (this.next <= 1) {
        const value = this.previous + (sample - this.previous) * this.next;
        this.emit(Math.max(-32768, Math.min(32767, Math.round(value * 32768))));
        this.next += this.step;
      }
      // Keep phase bounded to avoid cumulative floating-point drift on long files.
      this.next -= 1;
      this.previous = sample;
    }
  }
}

const BaseProcessor = globalThis.AudioWorkletProcessor || class {};
export class RadioProcessor extends BaseProcessor {
  constructor() {
    super();
    this.samples = new Int16Array(16384);
    this.read = this.write = this.played = this.phase = 0;
    this.generation = 0;
    this.active = this.started = this.ended = false;
    this.total = 0;
    this.capture = new Int16Array(960);
    this.captureCount = 0;
    this.silence = new Float32Array(128);
    this.ratio = 48000 / sampleRate;
    this.resampler = new CaptureResampler(sampleRate, sample => {
      this.capture[this.captureCount++] = sample;
      if (this.captureCount === this.capture.length) this.sendClock();
    });
    this.port.onmessage = ({data}) => {
      if (data.type !== 'attach') return;
      this.link?.close();
      this.link = data.port;
      this.link.onmessage = ({data: packet}) => this.message(packet);
      this.link.start();
    };
  }
  message(data) {
    if (data.type === 'reset') {
      this.generation = data.generation;
      this.active = data.active;
      this.read = this.write = this.played = this.phase = 0;
      this.started = this.ended = false;
      this.captureCount = 0;
      this.resampler = new CaptureResampler(sampleRate, sample => {
        this.capture[this.captureCount++] = sample;
        if (this.captureCount === this.capture.length) this.sendClock();
      });
    } else if (data.generation === this.generation && this.active) {
      if (data.type === 'play') {
        if (this.write - this.read + data.samples.length > this.samples.length) return this.fault('Playback buffer overflow.');
        for (const sample of data.samples) this.samples[this.write++ % this.samples.length] = sample;
      } else if (data.type === 'end') {
        this.ended = true;
        this.total = data.total;
      }
    }
  }
  fault(message) {
    this.active = false;
    this.link?.postMessage({type: 'fault', message: `${message} Transmission interrupted. RELEASE THE RADIO PTT.`});
  }
  sendClock() {
    if (this.link) {
      const samples = this.capture;
      this.link.postMessage({type: 'clock', generation: this.generation, played: this.played, samples}, [samples.buffer]);
      this.capture = new Int16Array(960);
    }
    this.captureCount = 0;
  }
  process(inputs, outputs) {
    const output = outputs[0]?.[0];
    if (!output) return true;
    if (this.active && !this.started && (this.write - this.read >= 1920 || (this.ended && this.write > this.read))) this.started = true;
    for (let i = 0; i < output.length; ++i) {
      output[i] = 0;
      if (!this.active || !this.started) continue;
      const available = this.write - this.read;
      if (!available) {
        if (!this.ended || this.played < this.total) this.fault('Playback buffer underrun.');
        continue;
      }
      const first = this.samples[this.read % this.samples.length];
      const next = available > 1 ? this.samples[(this.read + 1) % this.samples.length] : first;
      output[i] = (first + (next - first) * this.phase) / 32768;
      this.phase += this.ratio;
      const advance = Math.floor(this.phase);
      const consumed = Math.min(advance, available);
      this.read += consumed;
      this.played += consumed;
      this.phase -= advance;
    }
    // Keep the audio clock and health watchdog moving even when the mic is silent.
    const input = inputs[0]?.[0];
    if (this.silence.length !== output.length) this.silence = new Float32Array(output.length);
    this.resampler.push(input || this.silence);
    return true;
  }
}
if (globalThis.registerProcessor) registerProcessor('blacksquelch-radio', RadioProcessor);
