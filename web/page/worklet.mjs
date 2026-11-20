// The AudioWorklet side: one booted kit machine. Each machine has a small entry file,
// worklet-<id>.mjs (written by build.sh), that imports its module and calls register().
//
// Messages in:  {params: [...]}   all values, table order
//               {key: [id, down]} | {input: [id, value]}   the hardware panel
// Messages out: {type: 'ready', latency, live, presets} | {type: 'error', message} |
//               {type: 'readouts', readouts} | {type: 'panel', panel} (~30 a second, with a panel)
import { Machine } from './machine.mjs';

export function register(name, create) {
  class KitProcessor extends AudioWorkletProcessor {
    constructor(options) {
      super();
      const o = options.processorOptions;
      this.m = null;
      this.pending = o.params;
      this.dirty = true;
      this.alive = true;
      this.blocks = 0;
      this.port.onmessage = (e) => {
        if (e.data.params) { this.pending = e.data.params; this.dirty = true; }
        if (e.data.stop) this.alive = false;
        if (e.data.key && this.m) this.m.panelKey(e.data.key[0], e.data.key[1]);
        if (e.data.input) {
          (this.inputs ||= []).push(e.data.input);
          if (this.m) { for (const [i, v] of this.inputs) this.m.panelInput(i, v); this.inputs = []; }
        }
      };
      Machine.load(create).then((m) => {
        const err = m.boot(o.files, sampleRate, o.state || '');
        if (err) { this.port.postMessage({ type: 'error', message: err }); return; }
        m.setParams(this.pending);
        this.dirty = false;
        for (const [i, v] of o.inputs || []) m.panelInput(i, v);
        for (const [i, v] of this.inputs || []) m.panelInput(i, v);
        this.inputs = [];
        this.hasPanel = !!m.panelLayout();
        this.m = m;
        this.live = m.liveReadouts();
        this.port.postMessage({ type: 'ready', latency: m.latency(), live: this.live, presets: m.presets() });
        this.sendReadouts();
      }, (e) => this.port.postMessage({ type: 'error', message: String(e) }));
      this.zero = new Float32Array(128);
    }

    sendReadouts() { this.port.postMessage({ type: 'readouts', readouts: this.m.readouts() }); }

    process(inputs, outputs) {
      const out = outputs[0];
      if (!this.m) { for (const c of out) c.fill(0); return this.alive; }
      let changed = false;
      if (this.dirty) { this.m.setParams(this.pending); this.dirty = false; changed = true; }
      const inp = inputs[0];
      const n = out[0].length;
      if (this.zero.length < n) this.zero = new Float32Array(n);
      const l = inp && inp.length ? inp[0] : this.zero;
      const r = inp && inp.length > 1 ? inp[1] : l;
      const oL = out[0], oR = out.length > 1 ? out[1] : new Float32Array(n);
      if (n <= this.m.maxBlock) this.m.process(l, r, oL, oR, n);   // the usual 128 frames: no views
      else for (let at = 0; at < n; at += this.m.maxBlock) {
        const k = Math.min(this.m.maxBlock, n - at);
        this.m.process(l.subarray(at, at + k), r.subarray(at, at + k), oL.subarray(at, at + k), oR.subarray(at, at + k), k);
      }
      // readouts: right after a change, then ~12 times a second for machines whose display moves
      this.blocks++;
      if (changed || (this.live && this.blocks % Math.max(1, Math.round(sampleRate / n / 12)) === 0)) this.sendReadouts();
      if (this.hasPanel && this.blocks % Math.max(1, Math.round(sampleRate / n / 30)) === 0)
        this.port.postMessage({ type: 'panel', panel: this.m.panel() });
      return this.alive;
    }
  }
  registerProcessor(name, KitProcessor);
}
