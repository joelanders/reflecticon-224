// The 224X front-panel operator, without any DOM: the control head's
// counterpart of createOperator() in larc.js, with the same shape, so the
// page (app.js) and the Node precompute (precompute.mjs) can drive either.
//
// The v8.1 control head (224X service manual, table 3.2) has three 7-segment
// digits, unit and selection LEDs, six slide pots and three button banks:
//   bank 0  PROGRAM 1-8
//   bank 1  IMED, SET, CALL, SHIFT, REG A-D
//   bank 2  display select: BASS, MID, CROSS-OVER, TREBLE DECAY, DEPTH, PRE-DELAY
// What the firmware does with them (read from the v8.1 code):
//   CALL, then PROGRAM buttons pressed together   load the factory program whose
//                                                 identity is that button mask (8204)
//   PROGRAM n alone                                variation n of the current program
//   IMED                                          next page of six parameters (83FC)
//   a select button, or moving a pot              show that parameter on the digits
// A parameter's value is the pot's 8-bit reading, clamped to a per-parameter
// limit (860C). After a load or a page change a pot is under soft pickup: it
// takes over once it reaches or crosses the stored value (8D3F). The scan
// records pot readings with hysteresis (0A0E, see setPot).
import { NOT_A_SLIDER } from './larc.js';

export const PANEL = {
  SCAN: 0x3c06,        // the firmware's copy of banks 0-2, active low (FF = nothing held)
  POTS: 0x3c00,        // its last accepted reading of each pot
  MODE: 0x3c43,        // bit 2: CALL pending
  PAGE: 0x3c50,        // current page, from 0
  COLUMN: 0x3c52,      // the record column the pots edit on this page (a page can skip one)
  VARIATION: 0x3c58,   // the loaded variation, as its PROGRAM button mask
  RECORD: 0x3c9a,      // working record: identity, then 6 parameter bytes per column
  LIVE: 0x3c0a,        // per parameter (6 per column): bit 7 = the pot has picked it up
  DIRECTORY: 0xa000,   // factory records: 18 slots of 0x2AA bytes, first byte = identity
};
const CALL = 0x04, IMED = 0x01;
export const POT_NAMES = ['BASS', 'MID', 'CROSS-OVER', 'TREBLE DECAY', 'DEPTH', 'PRE-DELAY'];

// Seven-segment patterns (bit 0 = segment a ... bit 6 = g, bit 7 = DP).
const SEGMENTS = {
  0x3f: '0', 0x06: '1', 0x5b: '2', 0x4f: '3', 0x66: '4', 0x6d: '5', 0x7d: '6', 0x07: '7', 0x7f: '8', 0x6f: '9',
  0x00: ' ', 0x40: '-', 0x77: 'A', 0x7c: 'b', 0x39: 'C', 0x58: 'c', 0x5e: 'd', 0x79: 'E', 0x71: 'F', 0x3d: 'G',
  0x76: 'H', 0x74: 'h', 0x38: 'L', 0x54: 'n', 0x5c: 'o', 0x73: 'P', 0x50: 'r', 0x78: 't', 0x3e: 'U', 0x1c: 'u', 0x6e: 'y',
};
const UNITS = [['SEC', 'SEC'], ['MS', 'MSEC'], ['Hz', 'HZ'], ['KHz', 'KHZ']];   // LED byte 5, bits 0-3

// Programs by button mask: "PROG 1+3".
export const buttonsFor = (identity) =>
  'PROG ' + [0, 1, 2, 3, 4, 5, 6, 7].filter((b) => identity >> b & 1).map((b) => b + 1).join('+');

export function createPanelOperator(m) {
  const lit = () => Array.from(new Uint8Array(m.Module.HEAPU8.buffer, m.api.digits(m.handle), 9), (x) => ~x & 0xff);
  // The digits (digit 2 is the leftmost) and the unit LED, as the LARC would
  // spell them: "2.6 SEC", "720 HZ", "33".
  const display = () => {
    const d = lit();
    const text = [2, 1, 0].map((i) => (SEGMENTS[d[i] & 0x7f] ?? '?') + (d[i] & 0x80 ? '.' : '')).join('').trim();
    const unit = UNITS.find((_, bit) => d[5] >> bit & 1);
    // The lit select LEDs (bits 5-7 of LED bytes 6 and 7) say whose value it is.
    const selected = [0, 1, 2, 3, 4, 5].filter((k) => d[6 + (k >= 3)] >> (5 + k % 3) & 1);
    return { top: unit ? `${text} ${unit[1]}` : text, bottom: '', digits: text, selected };
  };
  const peek = m.peek;
  const scanIdle = (bank) => peek(PANEL.SCAN + bank) === 0xff;

  const op = {
    kind: 'panel',
    current: { identity: 0, variation: 1 },
    currentPage: 1,
    display,

    // Hold buttons until the firmware's scan has seen them, then release them
    // and wait until it has seen that too.
    async press(bank, mask) {
      m.schedule(m.time, () => m.api.button(m.handle, bank, mask));
      const seen = await m.waitFor(() => peek(PANEL.SCAN + bank) === (~mask & 0xff), 0.5);
      m.schedule(m.time, () => m.api.button(m.handle, bank, 0));
      await m.waitFor(() => scanIdle(bank), 0.5);
      return seen;
    },
    // Wait until the display has stayed unchanged for `quiet` seconds (and,
    // given `slot`, shows that parameter: only its select LED lit).
    settled(timeout = 2, quiet = 0.12, slot = null) {
      let last = null, since = m.time;
      return m.waitFor(() => {
        const d = display(), text = d.top;
        if (text !== last) { last = text; since = m.time; return false; }
        return (slot === null || (d.selected.length === 1 && d.selected[0] === slot)) && m.time - since >= quiet;
      }, timeout);
    },

    // Move a pot so that the firmware records exactly `value`. The scan
    // (0A0E) takes any fall as it is, ignores a rise of 1 or 2, and records a
    // bigger rise as the reading minus 2: so come down onto the value, or rise
    // to value + 2. Rising, the highest value a pot can record is 253.
    async setPot(slot, value) {
      value = Math.min(value, 253);
      const recorded = peek(PANEL.POTS + slot);
      if (recorded === value) return true;
      const reading = value < recorded ? value : value + 2;
      m.schedule(m.time, () => m.api.pot(m.handle, slot, reading));
      return m.waitFor(() => peek(PANEL.POTS + slot) === value, 0.5);
    },

    // The firmware's own program list: the identities in its factory
    // directory, walked as its lookup (81C7) walks it. Each one is loaded.
    async scanCatalog() {
      const catalog = [];
      let address = PANEL.DIRECTORY;
      for (let slot = 0; slot < 18; slot++) {
        const identity = peek(address);
        if (identity !== 0xff && identity !== 0 && await op.loadProgram(identity))
          catalog.push({ identity, name: buttonsFor(identity), buttons: buttonsFor(identity) });
        address += 0x2aa;
        if ((address & 0xff) === 0xfe) address += 2;
      }
      return catalog;
    },

    // CALL, then the program's buttons together.
    async loadProgram(identity) {
      for (let attempt = 0; attempt < 3; attempt++) {
        if (attempt) await m.sleep(0.5);
        await op.press(1, CALL);
        if (!(peek(PANEL.MODE) & 4)) continue;
        await op.press(0, identity);
        if (await m.waitFor(() => peek(PANEL.RECORD) === identity && peek(PANEL.VARIATION) === 1, 1)) {
          op.current = { identity, variation: 1 };
          op.currentPage = peek(PANEL.PAGE) + 1;
          await op.recordSettled();
          return true;
        }
      }
      return false;
    },
    selectProgram: (entry) => op.loadProgram(entry.identity),
    // PROGRAM n: a missing variation leaves the current one loaded.
    async loadVariation(v) {
      const mask = 1 << (v - 1);
      await op.press(0, mask);
      const ok = await m.waitFor(() => peek(PANEL.VARIATION) === mask && peek(PANEL.RECORD) === op.current.identity, 1);
      if (ok) { op.current.variation = v; op.currentPage = peek(PANEL.PAGE) + 1; await op.recordSettled(); }
      return ok;
    },

    async gotoPage(page) {
      for (let i = 0; i < 9 && peek(PANEL.PAGE) + 1 !== page; i++) {
        const before = peek(PANEL.PAGE);
        await op.press(1, IMED);
        await m.waitFor(() => peek(PANEL.PAGE) !== before, 0.5);
      }
      op.currentPage = peek(PANEL.PAGE) + 1;
      if (op.currentPage !== page) throw new Error(`could not reach page ${page}`);
    },
    // A parameter's stored byte, by record column (a page's column is in the catalog).
    stored: (column, slot) => peek(PANEL.RECORD + 1 + 6 * column + slot),
    // After a load the firmware may go on rebuilding the program for a while:
    // wait until the working record is still.
    recordSettled() {
      let last = null, since = m.time;
      return m.waitFor(() => {
        const now = Array.from({ length: 48 }, (_, i) => peek(PANEL.RECORD + i)).join();
        if (now !== last) { last = now; since = m.time; return false; }
        return m.time - since >= 0.3;
      }, 4);
    },
    // The six parameters of the current page: raw byte and displayed value.
    async readSliders() {
      const sliders = [], base = PANEL.RECORD + 1 + 6 * peek(PANEL.COLUMN);
      for (let slot = 0; slot < 6; slot++) {
        await op.press(2, 1 << slot);
        if (!await op.settled(0.6, 0.1, slot)) throw new Error(`parameter ${slot + 1} did not settle`);
        sliders.push({ name: POT_NAMES[slot], value: display().top, raw: peek(base + slot) });
      }
      return sliders;
    },
    // Every page (IMED cycles 1 → N → 1), leaving the panel on page 1. IMED
    // shows the new page's label on the digits ("2dd", "3LE", ... "1--").
    async readPages() {
      await op.gotoPage(1);
      const pages = [], labels = {};
      for (let page = 1; page <= 8; page++) {
        op.currentPage = page;
        pages.push({ page, heading: '', column: peek(PANEL.COLUMN), sliders: await op.readSliders() });
        await op.press(1, IMED);
        await m.waitFor(() => peek(PANEL.PAGE) !== page - 1, 0.5);
        await op.settled(0.5, 0.05);
        labels[peek(PANEL.PAGE) + 1] = display().digits;
        if (peek(PANEL.PAGE) === 0) break;                                   // wrapped round
      }
      op.currentPage = peek(PANEL.PAGE) + 1;
      for (const p of pages) p.heading = labels[p.page] || '';
      return pages;
    },

    // Take over a parameter without changing it, then move its pot. Soft
    // pickup (8D3F) ignores a pot until it reaches or crosses the stored
    // value; the operator sets the parameter's pickup bit (bit 7), the state
    // the firmware sets itself after a crossing, so no pot but this one moves.
    // A pot already reading `value` has to move for the firmware to look.
    async moveSlider(page, slot, value) {
      await op.gotoPage(page);
      const index = 6 * peek(PANEL.COLUMN) + slot, live = () => peek(PANEL.LIVE + index) & 0x80;
      if (!live()) {
        m.poke(PANEL.LIVE + index, peek(PANEL.LIVE + index) | 0x80);
        if (!await m.waitFor(live, 0.2)) throw new Error(`page ${page} pot ${slot + 1} did not take the pickup`);
      }
      if (peek(PANEL.POTS + slot) === Math.min(value, 253)) await op.setPot(slot, value >= 253 ? 252 : value + 1);
      if (!await op.setPot(slot, value)) return null;
      return await op.settled(1) ? display().top : null;
    },
    // A raw → display table: step the pot 0..253 by 4, run-length encoding
    // the settled display text.
    async sweep(page, slot) {
      await op.moveSlider(page, slot, 0);
      const table = [];
      for (let raw = 0; raw <= 253; raw = raw === 252 ? 253 : raw + 4) {
        if (!await op.setPot(slot, raw)) throw new Error(`pot ${slot + 1} did not take ${raw}`);
        if (raw === 0) await op.press(2, 1 << slot);       // show it: the pot may not have moved
        if (!await op.settled(2, 0.12, slot)) throw new Error(`sweep at raw ${raw}: no settled display of pot ${slot + 1}`);
        const text = display().top;
        if (!table.length || table[table.length - 1][1] !== text) table.push([raw, text]);
      }
      return table;
    },

    // Everything about one program: variations with their presets (text and
    // raw bytes), the page layout, and a value table for every parameter.
    async describeProgramFully(entry) {
      if (!await op.loadProgram(entry.identity)) throw new Error(`could not load ${entry.name}`);
      const presets = {}, raw = {}, variations = [];
      let layout = null;
      for (let v = 1; v <= 8; v++) {
        if (!await op.loadVariation(v)) continue;
        const pages = await op.readPages();
        variations.push(v);
        presets[v] = pages.map((page) => page.sliders.map((slider) => slider.value));
        raw[v] = pages.map((page) => page.sliders.map((slider) => slider.raw));
        layout = layout || pages.map((page) => ({ page: page.page, heading: page.heading, column: page.column,
          sliders: page.sliders.map((slider) => ({ name: slider.name })) }));
      }
      if (!layout) throw new Error(`${entry.name}: no variation loaded`);
      await op.loadVariation(1);
      for (const page of layout)
        for (const [slot, slider] of page.sliders.entries()) {
          try { slider.table = await op.sweep(page.page, slot); }
          catch (e) { throw new Error(`${entry.name}: page ${page.page} pot ${slot + 1}: ${e.message}`); }
        }
      return { ...entry, variations, pages: layout, presets, raw };
    },
  };
  return op;
}

// Name the panel's programs from a LARC catalog of the same program library
// (224XL v8.1A for v8.1): the LARC program whose values agree best with the
// panel's own display, over every variation both have, page by page. It must
// have the same pages, agree on at least 60 % of the values, and beat the
// runner-up by at least 3 values (sibling programs such as Concert Hall and
// Dark Hall share most of theirs). INACTIVE parameters are not compared, and
// the LARC's "%" is ignored (the panel has no % LED). Revisions between the
// two firmware versions show up as the disagreements listed in the report;
// the panel's own values are kept.
export function nameFromLarc(programs, larc) {
  const norm = (text) => text.replace(/\s*%$/, '');
  const report = [];
  for (const p of programs) {
    const scored = larc.programs.map((q) => {
      let same = 0, total = 0;
      const differences = [];
      for (const v of p.variations) {
        const theirs = q.presets[String(v)];
        if (!theirs) continue;
        p.presets[v].forEach((page, i) => page.forEach((value, slot) => {
          const other = theirs[i] && theirs[i][slot];
          const name = q.pages[i] && q.pages[i].sliders[slot].name;
          if (other === undefined || other === '' || name === 'INACTIVE') return;
          total++;
          if (norm(other) === norm(value)) same++;
          else differences.push(`V${v} page ${i + 1} ${name}: ${value} here, ${other} there`);
        }));
      }
      return { q, same, total, differences, pagesMatch: q.pages.length === p.pages.length };
    }).sort((a, b) => b.same - a.same);
    const [best, next] = scored;
    const clear = best && best.pagesMatch && best.same >= 0.6 * best.total && best.same - (next ? next.same : 0) >= 3;
    report.push({ identity: p.identity, buttons: p.buttons, name: clear ? best.q.name : null,
      larc: best && `${best.q.name} B${best.q.bank} P${best.q.program}`, same: best && best.same, total: best && best.total,
      runnerUp: next && `${next.q.name} ${next.same}`, differences: best ? best.differences : [] });
    if (!clear) continue;
    p.name = best.q.name;
    p.bankName = best.q.bankName;
    p.larc = { bank: best.q.bank, program: best.q.program };
    p.pages.forEach((page, i) => {
      page.label = page.heading;                                   // the panel's own, e.g. "2dd"
      page.heading = best.q.pages[i].heading;
      page.sliders.forEach((slider, slot) => { slider.pot = slider.name; slider.name = best.q.pages[i].sliders[slot].name; });
    });
  }
  return report;
}

// Refuse anything that is not a clean panel catalog.
export function panelCatalogProblems(catalog, expectedCount) {
  const problems = [];
  if (catalog.programs.length !== expectedCount) problems.push(`${catalog.programs.length} of ${expectedCount} programs`);
  for (const p of catalog.programs) {
    if (!p.variations.includes(1)) problems.push(`${p.name}: no variation 1`);
    for (const page of p.pages) for (const slider of page.sliders)
      if (NOT_A_SLIDER.test(slider.name) || !(slider.table || []).length)
        problems.push(`${p.name} page ${page.page}: "${slider.name}" has no table`);
  }
  return problems;
}
