// A kit machine module (machines/<id>.js) behind a small JS class. Used on the main thread (the
// parameter table, ROM recognition), in the AudioWorklet (boot, audio) and by tests/node_check.mjs.
// Uses nothing an AudioWorkletGlobalScope lacks (no TextEncoder/TextDecoder, fetch or atob).

export class Machine {
  static async load(create) {
    const mod = await create({ print: () => {}, printErr: () => {} });
    return new Machine(mod);
  }

  constructor(mod) {
    this.mod = mod;
    this.info = JSON.parse(this.str(mod._kw_info()));
    this.maxBlock = mod._kw_max_block();
  }

  str(ptr) { return this.mod.UTF8ToString(ptr); }

  cstr(s) {
    const n = this.mod.lengthBytesUTF8(s) + 1;
    const p = this.mod._malloc(n);
    this.mod.stringToUTF8(s, p, n);
    return p;
  }

  sizeFits(len) { return !!this.mod._kw_size_fits(len); }

  // files: [{name, bytes: Uint8Array}]
  addFiles(files) {
    const m = this.mod;
    m._kw_files_clear();
    for (const f of files) {
      if (!this.sizeFits(f.bytes.length)) continue;
      const name = this.cstr(f.name);
      const p = m._malloc(f.bytes.length);
      m.HEAPU8.set(f.bytes, p);
      m._kw_file_add(name, p, f.bytes.length);
      m._free(p);
      m._free(name);
    }
  }

  // {complete, describe, slots: [{role, found, known, label, file}]}
  recognise(files) {
    this.addFiles(files);
    const r = JSON.parse(this.str(this.mod._kw_recognise()));
    this.mod._kw_files_clear();
    return r;
  }

  // '' when running, else the reason
  boot(files, rate, state = '') {
    this.addFiles(files);
    const s = this.cstr(state);
    const ok = this.mod._kw_boot(s);
    this.mod._free(s);
    if (!ok) return this.str(this.mod._kw_error()) || 'boot failed';
    this.mod._kw_prepare(rate);
    return '';
  }

  setParams(values) {
    const p = this.mod._kw_params() >> 2;
    this.mod.HEAPF32.set(values, p);
    this.mod._kw_set_params();
  }

  latency() { return this.mod._kw_latency(); }
  liveReadouts() { return !!this.mod._kw_live_readouts(); }
  readouts() { return JSON.parse(this.str(this.mod._kw_readouts())); }
  presets() { return JSON.parse(this.str(this.mod._kw_presets())); }

  // hardware panel (null when the machine has none)
  panelLayout() {
    const L = JSON.parse(this.str(this.mod._kw_panel_layout()));
    return L.elements.length ? L : null;
  }
  panelKey(id, down) { this.mod._kw_panel_key(id, down ? 1 : 0); }
  panelInput(id, v) { this.mod._kw_panel_input(id, v); }
  panel() { return JSON.parse(this.str(this.mod._kw_panel())); }

  // n <= maxBlock frames; inL/inR may be the same array
  process(inL, inR, outL, outR, n) {
    const m = this.mod;
    const b = (i) => m._kw_buf(i) >> 2;
    m.HEAPF32.set(n === inL.length ? inL : inL.subarray(0, n), b(0));
    m.HEAPF32.set(n === inR.length ? inR : inR.subarray(0, n), b(1));
    m._kw_process(n);
    const h = m.HEAPF32;   // after the call: memory may have grown
    outL.set(h.subarray(b(2), b(2) + n));
    outR.set(h.subarray(b(3), b(3) + n));
  }
}
