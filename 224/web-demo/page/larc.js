// The machine driver and the LARC operator, without any DOM, shared by the
// page (app.js) and the Node precompute (precompute.mjs).
//
//   createMachine(Module)   renders 48 kHz frames from the WASM machine (../cpu/web.cpp);
//                           timed LARC actions fire at their exact frame; waits
//                           are conditions checked after each rendered block
//   createOperator(m)       short async scripts that press real LARC keys and
//                           wait for the firmware's own display to answer
//
// With audio stopped the machine runs silently, as fast as it goes, while
// anything is waiting; with audio running the audio callback drives it.

export const RATE = 48000;
export const KEY = { PROG: 0x21, BANK: 0x22, VAR: 0x26, PAGE: 0x3e, PARAM: 0x2e, MUTE: 0x36, SECOND: 0x3a };
export const DIGIT = [0x3d, 0x2c, 0x30, 0x34, 0x38, 0x3c, 0x2d, 0x31, 0x35, 0x39];
export const SELECT = [0x23, 0x27, 0x2b, 0x2f, 0x33, 0x37];   // show slider n's name and value
export const PICKUP = 0x3c20;      // per-slot soft-pickup state: 1 live, 4 fader below the value, 2 above
export const PHYSICAL = 0x3c00;    // per-slot last received fader position
// Parameter bytes are kept 6 per record column at a base that differs
// between firmware versions (v8.21 0x3CA3, v8.1A 0x3C9C): recordLayout()
// reads it from the fader handler's own code.
export const PAGE = 0x3c32;        // the page the LARC is on, from 0
export const ENTRY = 0x3c11;       // the key awaiting a digit: PROG 1, BANK 2, VAR 6
const ENTRY_OF = { [KEY.PROG]: 1, [KEY.BANK]: 2, [KEY.VAR]: 6 };

// A slider display is "NAME VALUE", never a program, page or shift line.
export const NOT_A_SLIDER = /B\d P\d V\d|B\d PROGRAM|PAGE \d|BANK \d|SECOND\s*FUNCTION|ALL SLIDERS|ENTER/;
const SHIFTED = /SECOND\s*FUNCTION/;

export function parseSlider(top) {
  return { name: top.slice(0, 12).trim(), value: top.slice(12).trim().replace(/\s+/g, ' ') };
}

// Chip files are identified by name: SBCn at (n-1)*0x800, NVSn at
// 0x8000+(n-1)*0x1000 (224X/224XL), or the original 224's ROM1-ROM4 at
// (n-1)*0x800 (a 224 v4_4 set's ROM5 has no place in the memory map).
export function chipBase(name) {
  let m = /SBC\s*(\d)/i.exec(name);
  if (m) return (m[1] - 1) * 0x800;
  m = /NVS\s*(\d)/i.exec(name);
  if (m) return 0x8000 + (m[1] - 1) * 0x1000;
  m = /ROM\s*([1-4])(?!\d)/i.exec(name);
  if (m) return (m[1] - 1) * 0x800;
  return null;
}

// The machine a set of chip file names belongs to: 1 = the original 224
// (ROM1-ROM4), 0 = the 224X/224XL (SBCn/NVSn); the C API's model number.
export function modelOf(names) {
  return names.some((name) => /ROM\s*[1-4](?!\d)/i.test(name) && !/SBC|NVS/i.test(name)) ? 1 : 0;
}

// =====================================================================
// The machine
// =====================================================================
export function createMachine(Module, { soon } = {}) {
  const c = (name, ret, args) => Module.cwrap(name, ret, args);
  const n = 'number';
  const api = {
    create: c('lex_create', n, [n]), destroy: c('lex_destroy', null, [n]),
    load: c('lex_load', null, [n, n, n, n]), render: c('lex_render', n, [n, n, n, n, n]),
    button: c('lex_button', null, [n, n, n]), pot: c('lex_pot', null, [n, n, n]),
    key: c('lex_key', n, [n, n, n]), fader: c('lex_fader', n, [n, n, n]),
    digits: c('lex_panel_digits', n, [n]), larcText: c('lex_larc_text', n, [n]),
    larcConnected: c('lex_larc_connected', n, [n]), cycles: c('lex_cycles', n, [n]),
    wcs: c('lex_wcs', n, [n]), memory: c('lex_memory', n, [n]), peek: c('lex_peek', n, [n, n]),
    poke: c('lex_poke', null, [n, n, n]), headroom: c('lex_headroom', n, [n, n]), inputGains: c('lex_input_gains', n, [n]),
    setAnalog: c('lex_set_analog', null, [n, n]),
    listing: c('lex_wcs_listing', n, [n]), model: c('lex_model', n, [n]),
  };
  if (!soon) {  // chained setTimeout is clamped to several ms; a MessageChannel message is not
    const channel = new MessageChannel();
    soon = (fn) => { channel.port1.onmessage = fn; channel.port2.postMessage(0); };
  }
  const buffers = {};
  const m = {
    Module, api, handle: 0, time: 0, audioRunning: false, onFail: () => {},
    actions: [],   // [{at, fn}] sorted by time
    waiters: [],   // [{test, deadline, resolve}]
    pumping: false,

    powerOn(chips, model = 0) {
      if (m.handle) api.destroy(m.handle);
      m.handle = api.create(model);
      m.model = model;
      m.time = 0; m.actions = []; m.waiters = [];
      for (const chip of chips) {
        const ptr = Module._malloc(chip.bytes.length);
        Module.HEAPU8.set(chip.bytes, ptr);
        api.load(m.handle, ptr, chip.bytes.length, chip.base);
        Module._free(ptr);
      }
    },
    scratch(frames) {
      if (!buffers[frames]) buffers[frames] = {
        inL: Module._malloc(frames * 4), inR: Module._malloc(frames * 4), out: Module._malloc(frames * 16) };
      return buffers[frames];
    },
    schedule(at, fn) {
      m.actions.push({ at, fn });
      m.actions.sort((a, b) => a.at - b.at);
      m.pump();
    },
    // Render `frames` frames from buffer `b` (input already filled), firing actions.
    renderInto(b, frames) {
      let done = 0;
      while (done < frames) {
        if (m.actions.length && m.actions[0].at <= m.time) { m.actions.shift().fn(); continue; }
        let count = frames - done;
        if (m.actions.length) count = Math.max(1, Math.min(count, Math.ceil((m.actions[0].at - m.time) * RATE)));
        if (api.render(m.handle, b.inL + done * 4, b.inR + done * 4, b.out + done * 16, count)) { m.onFail(); return false; }
        done += count;
        m.time += count / RATE;
      }
      m.waiters = m.waiters.filter((w) => {
        if (w.test()) { w.resolve(true); return false; }
        if (m.time >= w.deadline) { w.resolve(false); return false; }
        return true;
      });
      return true;
    },
    // Resolve when test() holds (true) or `seconds` of machine time pass (false).
    waitFor(test, seconds) {
      return new Promise((resolve) => { m.waiters.push({ test, deadline: m.time + seconds, resolve }); m.pump(); });
    },
    sleep: (seconds) => m.waitFor(() => false, seconds),
    pump() {
      if (m.pumping || m.audioRunning || !m.handle) return;
      m.pumping = true;
      soon(function step() {
        const started = Date.now();
        while (Date.now() - started < 15) {
          if (m.audioRunning || (!m.waiters.length && !m.actions.length)) { m.pumping = false; return; }
          // Run to the next wake-up, in blocks of 1-100 ms.
          let next = Infinity;
          for (const w of m.waiters) next = Math.min(next, w.deadline);
          if (m.actions.length) next = Math.min(next, m.actions[0].at);
          const wanted = Math.ceil((next - m.time) * RATE);
          const frames = Math.max(48, Math.min(4800, Number.isFinite(wanted) ? wanted : 4800)), b = m.scratch(frames);
          Module.HEAPF32.fill(0, b.inL / 4, b.inL / 4 + frames);
          Module.HEAPF32.fill(0, b.inR / 4, b.inR / 4 + frames);
          if (!m.renderInto(b, frames)) { m.pumping = false; return; }
        }
        soon(step);
      });
    },
    display() {
      const text = Module.UTF8ToString(api.larcText(m.handle), 48).padEnd(48);
      return { top: text.slice(0, 24), bottom: text.slice(24) };
    },
    peek: (address) => api.peek(m.handle, address),
    poke: (address, value) => m.schedule(m.time, () => api.poke(m.handle, address, value)),
    fader: (slot, value) => m.schedule(m.time, () => api.fader(m.handle, slot, value)),
  };
  return m;
}

// =====================================================================
// The operator
// =====================================================================
export function createOperator(m) {
  let layout = null;
  const display = () => m.display();
  const shows = (pattern) => () => pattern.test(display().top);
  const op = {
    current: { bank: 1, program: 1, variation: 1 },
    currentPage: 1,
    display, shows,

    // Press and release a key (release = code & ~0x20), leaving the firmware time to act.
    async tap(code) {
      const t = m.time;
      m.schedule(t, () => m.api.key(m.handle, code, 1));
      m.schedule(t + 0.03, () => m.api.key(m.handle, code, 0));
      await m.sleep(0.1);
    },

    // Wait until the display shows a slider line (of slider `name`, if
    // given) that has stayed unchanged for `quiet` seconds. Some parameters,
    // such as SIZE, take ~0.4 s to answer, so a late echo of another slider
    // can arrive after a move: naming the slider rules that out.
    settledSlider(timeout = 2, quiet = 0.12, name = null) {
      let last = null, since = m.time;
      return m.waitFor(() => {
        const top = display().top;
        if (top !== last) { last = top; since = m.time; return false; }
        return !NOT_A_SLIDER.test(top) && (!name || parseSlider(top).name === name) && m.time - since >= quiet;
      }, timeout);
    },
    // Where the firmware keeps the parameters: its fader handler (v8.21 at
    // 862E) computes base + 6 * [column] + slot with LDA column; ADD A;
    // MOV H,A; ADD A; ADD H; ADD B; MOV E,A; MVI D,0; LXI H,base.
    recordLayout() {
      if (layout && layout.handle === m.handle) return layout;
      const pattern = [0x3a, -1, -1, 0x87, 0x67, 0x87, 0x84, 0x80, 0x5f, 0x16, 0x00, 0x21, -1, -1];
      for (let a = 0x8000; a < 0x10000 - pattern.length; a++) {
        if (pattern.every((b, i) => b < 0 || m.peek(a + i) === b)) {
          layout = { handle: m.handle, column: m.peek(a + 1) | m.peek(a + 2) << 8, base: m.peek(a + 12) | m.peek(a + 13) << 8 };
          return layout;
        }
      }
      throw new Error('this firmware\'s parameter record was not found');
    },
    // A parameter's stored byte, by record column (a page's column is in the catalog).
    stored: (column, slot) => m.peek(op.recordLayout().base + 6 * column + slot),
    column: () => m.peek(op.recordLayout().column),
    // After a load the firmware may go on rebuilding the program for a second
    // or two (SIZE), using the record as scratch: wait until it is still.
    recordSettled() {
      let last = null, since = m.time;
      return m.waitFor(() => {
        const now = Array.from({ length: 48 }, (_, i) => m.peek(op.recordLayout().base + i)).join();
        if (now !== last) { last = now; since = m.time; return false; }
        return m.time - since >= 0.3;
      }, 4);
    },

    // 2nd F is a shift key: if the key after it is lost, the next BANK means
    // "ENTER LABEL FOR BANK" (bank renaming). Every 2nd F use is verified, and
    // a pending shift is cancelled by pressing 2nd F again.
    async cancelShift() {
      if (SHIFTED.test(display().top)) { await op.tap(KEY.SECOND); await m.sleep(0.1); }
    },
    // 2nd F + PAGE ("ALL SLIDERS") makes every slider on the page live AND
    // sets each one to its fader's position, so it is not a harmless
    // activation: unmoved faders sit at 0. Only the raw LARC view uses it now.
    // The firmware can be busy for a second or more after a slider move (SIZE
    // rebuilds the program), dropping keys: let the display settle, retry slowly.
    async activateSliders() {
      await op.settledSlider(2, 0.2);
      for (let attempt = 0; attempt < 5; attempt++) {
        if (attempt) await m.sleep(0.5);
        await op.cancelShift();
        await op.tap(KEY.SECOND);
        if (!await m.waitFor(shows(SHIFTED), 0.8)) continue;
        await op.tap(KEY.PAGE);
        if (await m.waitFor(shows(/ALL SLIDERS/), 0.8)) return true;
      }
      await op.cancelShift();
      throw new Error('could not activate the sliders');
    },

    // PROG, BANK or VAR, then a digit. A key pressed while the firmware is
    // busy can be lost, and the digit alone would then mean something else
    // (after a lost VAR it selects a program): wait until the firmware is
    // awaiting the digit for this key (ENTRY), retrying the key.
    async entry(key, digit) {
      let armed = false;
      for (let attempt = 0; attempt < 4 && !armed; attempt++) {
        if (attempt) await m.sleep(0.3);
        await op.tap(key);
        armed = await m.waitFor(() => m.peek(ENTRY) === ENTRY_OF[key], 0.4);
      }
      if (!armed) throw new Error(`the firmware did not take key 0x${key.toString(16)}`);
      await op.tap(DIGIT[digit]);
    },

    // BANK n: the firmware shows "NAME BANK n", or the current bank if n does not exist.
    async selectBank(bank) {
      await op.entry(KEY.BANK, bank);
      await m.waitFor(shows(/BANK \d/), 0.3);
      const match = /BANK (\d)/.exec(display().top);
      return match && +match[1] === bank;
    },
    // PROG n: a real program keeps the "PROGRAM n" prompt until it has loaded
    // (up to about a second), then shows "NAME Bn Pm Vv"; a missing one reverts
    // at once to the current program.
    async loadProgram(bank, program) {
      await op.entry(KEY.PROG, program);
      // The old program's line can show first: wait for this one (a program
      // that does not exist never appears).
      const target = new RegExp(`B${bank} P${program} V(\\d)`);
      if (!await m.waitFor(() => target.test(display().top.slice(12)), 2.5)) return false;
      op.current = { bank, program, variation: +target.exec(display().top.slice(12))[1] };
      op.currentPage = m.peek(PAGE) + 1;
      await op.recordSettled();                              // let the firmware finish compiling
      return true;
    },
    async selectProgram(bank, program) {
      if (op.current.bank !== bank) await op.selectBank(bank);
      return op.loadProgram(bank, program);
    },
    async loadVariation(v) {
      await op.entry(KEY.VAR, v);
      // The old variation's line can show first: wait for this one.
      const target = `B${op.current.bank} P${op.current.program} V${v}`;
      const ok = await m.waitFor(() => display().top.slice(12).startsWith(target), 2.5);
      if (ok) { op.current.variation = v; op.currentPage = m.peek(PAGE) + 1; await op.recordSettled(); }
      return ok;
    },

    // The program list: every bank, every program number (there are gaps).
    async scanCatalog() {
      const catalog = [];
      for (let bank = 1; bank <= 9; bank++) {
        if (!await op.selectBank(bank)) continue;                            // no such bank
        const bankName = display().top.slice(0, 12).trim();
        for (let program = 1; program <= 9; program++) {
          // The firmware keeps compiling for a while after the name appears and
          // can drop keys meanwhile: retry once before calling a slot empty.
          for (let attempt = 0; attempt < 2; attempt++) {
            if (await op.loadProgram(bank, program)) {
              catalog.push({ bank, program, bankName, name: display().top.slice(0, 12).trim() });
              break;
            }
            await m.sleep(0.5);
            await op.selectBank(bank);                       // a miss leaves the old program's bank
          }
        }
      }
      return catalog;
    },

    // Six sliders of the current page: name and value, each verified.
    async readSliders() {
      const sliders = [];
      for (let slot = 0; slot < 6; slot++) {
        let ok = false;
        for (let attempt = 0; attempt < 3 && !ok; attempt++) {
          await op.tap(SELECT[slot]);
          ok = await op.settledSlider(0.6, 0.1);
        }
        if (!ok) throw new Error(`slider ${slot + 1} did not answer: "${display().top}"`);
        sliders.push({ ...parseSlider(display().top), raw: op.stored(op.column(), slot) });
      }
      return sliders;
    },
    // Every page (PAGE cycles 1 → N → 1), leaving the LARC on page 1.
    // Every page (PAGE cycles 1 → N → 1), leaving the LARC on page 1. The
    // firmware's own page byte says where it is.
    async readPages() {
      await op.gotoPage(1);
      const pages = [];
      for (let page = 1; page <= 9; page++) {
        pages.push({ page, heading: display().bottom.trim(), column: op.column(), sliders: await op.readSliders() });
        if (!await op.nextPage()) throw new Error(`PAGE did not leave page ${page}`);
        if (m.peek(PAGE) === 0) break;                                           // wrapped round
      }
      op.currentPage = 1;
      return pages;
    },
    // PAGE, retried if the firmware was busy and dropped it.
    async nextPage() {
      const before = m.peek(PAGE);
      for (let attempt = 0; attempt < 3; attempt++) {
        await op.tap(KEY.PAGE);
        if (await m.waitFor(() => m.peek(PAGE) !== before, 0.5)) { await m.sleep(0.1); return true; }
      }
      return false;
    },
    async gotoPage(page) {
      for (let i = 0; i < 10 && m.peek(PAGE) + 1 !== page; i++)
        if (!await op.nextPage()) break;
      op.currentPage = m.peek(PAGE) + 1;
      if (op.currentPage !== page) throw new Error(`could not reach page ${page}`);
    },

    // Moving a slider on any page: go to that page, take the slot over
    // without changing its value, then send the fader. Soft pickup (87A0)
    // ignores a fader until it matches or crosses the stored value; that
    // cannot be arranged for every slider (faders send 2..254, some values
    // are stored as 0, 1 or 255; SIZE-type pages store elsewhere), and 2nd F
    // + PAGE would move every slider on the page to its fader. So the operator
    // sets the one pickup byte to "live" (1), the state the firmware sets
    // itself after a crossing, and the fader message then goes through the
    // firmware's own handler.
    async takeOver(slot) {
      if (m.peek(PICKUP + slot) === 1) return;
      m.poke(PICKUP + slot, 1);
      if (!await m.waitFor(() => m.peek(PICKUP + slot) === 1, 0.2)) throw new Error(`slider ${slot + 1} did not take the pickup`);
    },
    // Returns the firmware's echo, or null if there was none. A fader message
    // equal to the last one is ignored by the firmware: step next to it first.
    async moveSlider(page, slot, value) {
      await op.gotoPage(page);
      await op.takeOver(slot);
      if (m.peek(PHYSICAL + slot) === value) { m.fader(slot, value === 254 ? 253 : value + 1); await m.sleep(0.07); }
      m.fader(slot, value);
      await m.sleep(0.07);
      return await op.settledSlider(1) ? parseSlider(display().top).value : null;
    },

    // A raw → display table for one slider: step the fader 2..254 by 4 and
    // run-length encode the settled display text. The other sliders keep
    // their values.
    async sweep(page, slot, name) {
      await op.moveSlider(page, slot, 2);
      const table = [];
      for (let raw = 2; raw <= 254; raw = raw === 254 ? 255 : Math.min(254, raw + 4)) {
        if (raw !== 2) m.fader(slot, raw);
        await m.sleep(0.07);
        // A late echo of another slider, or a busy firmware, can leave the
        // display elsewhere: show this slider again (its select key) and retry.
        let shown = raw !== 2 && await op.settledSlider(1, 0.12, name);
        for (let attempt = 0; attempt < 3 && !shown; attempt++) {
          await op.tap(SELECT[slot]);
          shown = await op.settledSlider(1, 0.12, name);
        }
        if (!shown) throw new Error(`sweep at raw ${raw}: no settled display of ${name}: "${display().top}"`);
        const text = parseSlider(display().top).value;
        if (!table.length || table[table.length - 1][1] !== text) table.push([raw, text]);
      }
      return table;
    },

    // Everything about one program: variations with their presets, the page
    // layout, and a value table for every active slider. Throws on any doubt.
    async describeProgramFully(entry) {
      let selected = false;
      for (let attempt = 0; attempt < 3 && !selected; attempt++) {
        if (attempt) { await op.cancelShift(); await m.sleep(0.5); op.current.bank = 0; }
        selected = await op.selectProgram(entry.bank, entry.program);
      }
      if (!selected) throw new Error(`could not select ${entry.name}`);
      const presets = {}, raw = {}, variations = [];
      let layout = null;
      for (let v = 1; v <= 9; v++) {
        let loaded = false;
        for (let attempt = 0; attempt < 2 && !loaded; attempt++) {
          if (attempt) await m.sleep(0.5);
          loaded = await op.loadVariation(v);
        }
        if (!loaded) continue;
        const pages = await op.readPages();
        variations.push(v);
        presets[v] = pages.map((page) => page.sliders.map((slider) => slider.value));
        raw[v] = pages.map((page) => page.sliders.map((slider) => slider.raw));
        layout = layout || pages.map((page) => ({ page: page.page, heading: page.heading, column: page.column,
          sliders: page.sliders.map((slider) => ({ name: slider.name })) }));
      }
      if (!layout) throw new Error(`${entry.name}: no variation loaded`);
      // Sweep from variation 1's presets: some values depend on other sliders
      // (a delay's display includes another's offset), so a table is exact
      // only in that context; the page shows the firmware's echo after a move.
      await op.loadVariation(1);
      for (const page of layout)
        for (const [slot, slider] of page.sliders.entries()) {
          if (slider.name === 'INACTIVE' || !slider.name) continue;
          try { slider.table = await op.sweep(page.page, slot, slider.name); }
          catch (e) { throw new Error(`${entry.name}: page ${page.page} ${slider.name}: ${e.message}`); }
        }
      return { ...entry, variations, pages: layout, presets, raw };
    },

    // PARAM cycles DYN DECAY / MODE ENH / DECAY OPT; a digit sets the one shown.
    async readToggles() {
      const state = {};
      for (let i = 0; i < 3; i++) {
        await op.tap(KEY.PARAM);
        for (const label of ['DYN DECAY', 'MODE ENH', 'DECAY OPT']) {
          const x = new RegExp(`${label}\\s*\\[(\\d)\\]`).exec(display().top);
          if (x) state[label] = x[1] === '1';
        }
      }
      await op.tap(SELECT[0]);   // leave the PARAM display
      return state;
    },
    async setToggle(label, on) {
      for (let i = 0; i < 4 && !display().top.startsWith(label); i++) await op.tap(KEY.PARAM);
      await op.tap(DIGIT[on ? 1 : 0]);
      await m.sleep(0.1);
      return op.readToggles();
    },
    async toggleMute() {
      await op.tap(KEY.MUTE);
      await m.waitFor(shows(/MUTE/), 0.3);
      return /ACTIVATED/.test(display().top) && !/DEACTIVATED/.test(display().top);
    },
  };
  return op;
}

// Refuse anything that is not a clean catalog.
export function catalogProblems(catalog, expectedCount) {
  const problems = [];
  if (catalog.programs.length !== expectedCount) problems.push(`${catalog.programs.length} of ${expectedCount} programs`);
  for (const p of catalog.programs) for (const page of p.pages) for (const slider of page.sliders)
    if (NOT_A_SLIDER.test(slider.name) || (slider.name !== 'INACTIVE' && !(slider.table || []).length))
      problems.push(`${p.name} page ${page.page}: "${slider.name}"`);
  return problems;
}
