// The original Lexicon 224's front-panel operator, without any DOM: the
// counterpart of panel.js (224X v8.1) and larc.js (224XL), with the same
// shape, so the page (app.js), the Node precompute (precompute.mjs) and the
// soak test (tests/soak.mjs) drive it the same way.
//
// The control head is the 224X's (224 service manual part 4; the same scan
// addresses), but the v4.x operating system is its own. What it does, read
// from the v4.3 code (ROM1; v4.4 is the same code at the same addresses):
//
//   01EC  the scan loop: address B = 0..8, digit byte out, pot or button bank
//         in. A change calls 0234, which dispatches through the table at 026E:
//         pots 0-5 to 05CC/05D5/05DE/05E7/05F0/0633, bank 6 (PROGRAM) to 030B,
//         bank 7 (IMED SET CALL SHIFT REG A-D) to 0406, bank 8 (display
//         select) to 051F. Scan copies: pots 3F20-3F25, banks 3F26-3F28
//         (active low, FF = nothing held).
//   0203  pot hysteresis, the same rule as the 224X's 0A0E: a fall is taken as
//         read, a rise of 1 or 2 ignored, a bigger rise recorded as reading - 2.
//   0406  bank 7: IMED (0449) makes every pot live at once and sets 3F45 = 0;
//         SET (0471) 3F45 = 1; CALL (047E) 3F45 = 2; SHIFT (04FD) toggles the
//         SHIFT LED, unless TREBLE or DEPTH select is held; REG A-D (048B)
//         store (SET) or recall (CALL) a 16-byte register (3F76 + 16n).
//   030B  PROGRAM buttons. PROG 7 and PROG 8 toggle bits 6 and 7 of 3F65
//         (Mode Enhancement and Decay Optimization; a load turns both on
//         again, 036F). PROG 1-6 load that factory program (3F65 bits 0-5 =
//         its button), clear every pot's pickup (03E2) and compile (09D2).
//         Ignored while the SHIFT LED is lit, or SHIFT is held.
//   051F  display select, while held: 3F44 picks what the digits show (06F2);
//         released, they show the reverb time again. SHIFT + DEPTH shows
//         diffusion (059D).
//   099F  soft pickup: a pot takes over its parameter when its quantized
//         reading reaches or crosses the stored value; bit 7 of its pickup
//         byte is then set (live).
//
// The parameters (the stored byte, its pickup byte, the pot's quantization):
//   BASS         3F67  3F2B  pot >> 3 (0 counts as 1)          05CC
//   MID          3F66  3F2A  pot >> 3                          05D5
//   CROSSOVER    3F68  3F2C  pot >> 3                          05DE
//   TREBLE DECAY 3F69  3F2D  pot >> 3                          05E7
//   DEPTH        3F6A  3F2E  pot * 72 / 256 (0-71)              060A
//   PRE-DELAY    3F6D  3F2F  per-program range (3F70 + minimum) 0633
//   DIFFUSION    3F6E  3F30  DEPTH pot with SHIFT held, pot / 4 (1-63)  05FF
// The 224 has no pages and no variations: page 1 is the six pots, page 2
// ("SHIFT") holds DIFFUSION in the DEPTH pot's place.
import { createPanelOperator } from './panel.js';

export const PANEL224 = {
  POTS: 0x3f20,        // the scan's last accepted reading of each pot
  SCAN: 0x3f26,        // banks 6-8 (PROGRAM, IMED..REG D, display select), active low
  LEDS: 0x3f3f,        // LED byte 4: IMED 1, SET 2, CALL 4, SHIFT 8, REG A-D 10-80
  SHOWN: 0x3f44,       // what the digits show (the held select)
  MODE: 0x3f45,        // 0 IMED, 1 SET, 2 CALL
  PROGRAM: 0x3f65,     // bits 0-5 the program's button, 6 Mode Enhancement, 7 Decay Optimization
};
const CALL = 0x04, SHIFT = 0x08;
const PROG7 = 0x40, PROG8 = 0x80;

// Per slot: stored byte, pickup byte, select button (bank 2).
const POTS = [
  { name: 'BASS', cell: 0x3f67, pickup: 0x3f2b },
  { name: 'MID', cell: 0x3f66, pickup: 0x3f2a },
  { name: 'CROSSOVER', cell: 0x3f68, pickup: 0x3f2c },
  { name: 'TREBLE DECAY', cell: 0x3f69, pickup: 0x3f2d },
  { name: 'DEPTH', cell: 0x3f6a, pickup: 0x3f2e },
  { name: 'PRE-DELAY', cell: 0x3f6d, pickup: 0x3f2f },
];
const DIFFUSION = { name: 'DIFFUSION', cell: 0x3f6e, pickup: 0x3f30 };
const DEPTH_SLOT = 4;
const PAGES = [
  { page: 1, heading: '', column: 0, sliders: POTS.map((p) => ({ name: p.name })) },
  { page: 2, heading: 'SHIFT', column: 1,
    sliders: POTS.map((_, slot) => ({ name: slot === DEPTH_SLOT ? DIFFUSION.name : 'INACTIVE' })) },
];
const parameter = (page, slot) => page === 1 ? POTS[slot] : slot === DEPTH_SLOT ? DIFFUSION : null;

// Program names. The display shows only numbers; the names are the Model 224
// Owner's Manual's, section 3.3 "Operating Systems List" (V2.2 and V3.2, six
// programs, in button order; the manual stops at V3.2):
// https://www.barryrudolph.com/recall/manuals/lexicon224part2.pdf.
// That v4.x keeps this button order rests on the firmware and on UA's v4.4
// model, not on a Lexicon document for v4 (see the 2026-09-25 easy-UI note):
// v3.2's buttons 1, 3, 4, 5, 6 compile the same program structure as
// v4.3's; UA's program menu has the same six names in the same order, and its
// Small Concert Hall A defaults are exactly v4.3's PROGRAM 6 (3.0 s, 2.0 s,
// 540 Hz, 6.60 kHz, 23, 024 ms, diffusion 28).
export const PROGRAM_NAMES = ['SMALL CONCERT HALL B', 'VOCAL PLATE', 'LARGE CONCERT HALL B',
  'ACOUSTIC CHAMBER', 'PERCUSSION PLATE A', 'SMALL CONCERT HALL A'];

export function createPanel224Operator(m) {
  const peek = m.peek;
  const base = createPanelOperator(m);           // the digits and LEDs read the same way
  const { display, settled } = base;
  const scan = (bank) => peek(PANEL224.SCAN + bank);

  // Hold exactly `mask` in `bank` (0 releases), until the scan has seen it.
  async function hold(bank, mask) {
    m.schedule(m.time, () => m.api.button(m.handle, bank, mask));
    return m.waitFor(() => scan(bank) === (~mask & 0xff), 0.5);
  }
  const shiftLit = () => peek(PANEL224.LEDS) & SHIFT;

  const op = {
    kind: 'panel', remote: 'panel224',
    current: { identity: 0, variation: 1 },
    currentPage: 1,
    display, settled,

    async press(bank, mask) {
      const seen = await hold(bank, mask);
      await hold(bank, 0);
      return seen;
    },
    // SHIFT is a latch (04FD): pressed alone it toggles the SHIFT LED, and a
    // lit SHIFT LED makes the PROGRAM buttons do nothing.
    async cancelShift() {
      for (const bank of [0, 1, 2]) await hold(bank, 0);
      if (shiftLit()) await op.press(1, SHIFT);
      return !shiftLit();
    },

    // As panel.js: the scan (0203) takes any fall as it is, ignores a rise
    // of 1 or 2 and records a bigger rise as the reading minus 2.
    async setPot(slot, value) {
      value = Math.min(value, 253);
      const recorded = peek(PANEL224.POTS + slot);
      if (recorded === value) return true;
      const reading = value < recorded ? value : value + 2;
      m.schedule(m.time, () => m.api.pot(m.handle, slot, reading));
      return m.waitFor(() => peek(PANEL224.POTS + slot) === value, 0.5);
    },

    // The six program buttons the firmware accepts: each is loaded, and a
    // button that loads nothing is not a program.
    async scanCatalog() {
      const catalog = [];
      for (let b = 0; b < 6; b++) {
        const identity = 1 << b;
        if (await op.loadProgram(identity))
          catalog.push({ identity, name: PROGRAM_NAMES[b], buttons: `PROG ${b + 1}`, bankName: '' });
      }
      return catalog;
    },

    // CALL mode (3F45 = 2), then the program's button.
    async loadProgram(identity) {
      for (let attempt = 0; attempt < 3; attempt++) {
        if (attempt) await m.sleep(0.5);
        if (!await op.cancelShift()) continue;
        if (peek(PANEL224.MODE) !== 2) {
          await op.press(1, CALL);
          if (!await m.waitFor(() => peek(PANEL224.MODE) === 2, 0.5)) continue;
        }
        await op.press(0, identity);
        if (await m.waitFor(() => (peek(PANEL224.PROGRAM) & 0x3f) === identity, 1)) {
          op.current = { identity, variation: 1 };
          op.currentPage = 1;
          await op.recordSettled();
          return true;
        }
      }
      return false;
    },
    selectProgram: (entry) => op.loadProgram(entry.identity),
    // The 224 has no variations: "variation 1" is the program itself, loaded
    // again (its factory values, as after any load).
    async loadVariation(v) { return v === 1 && op.loadProgram(op.current.identity); },
    async gotoPage(page) { op.currentPage = page; },

    // A parameter's stored byte (column 0: the pots, 1: the SHIFT page).
    stored(column, slot) {
      const p = parameter(column + 1, slot);
      return p ? peek(p.cell) : 0;
    },
    // Wait until the program state (3F65-3F70) is still: a load compiles.
    recordSettled() {
      let last = null, since = m.time;
      return m.waitFor(() => {
        const now = Array.from({ length: 12 }, (_, i) => peek(PANEL224.PROGRAM + i)).join();
        if (now !== last) { last = now; since = m.time; return false; }
        return m.time - since >= 0.3;
      }, 4);
    },

    // Show a parameter: hold its select button (with SHIFT for page 2),
    // run `during` while the digits show it, then release.
    async showing(page, slot, during) {
      if (page === 2) {
        if (!await hold(1, SHIFT)) throw new Error('SHIFT was not seen');
      }
      if (!await hold(2, 1 << slot)) throw new Error(`select ${slot + 1} was not seen`);
      const shown = page === 2 ? 0x40 : [0x01, 0x02, 0x04, 0x08, 0x10, 0x20][slot];
      if (!await m.waitFor(() => peek(PANEL224.SHOWN) === shown, 0.5)) throw new Error(`the display did not select ${slot + 1}`);
      try {
        return await during();
      } finally {
        await hold(2, 0);
        if (page === 2) await hold(1, 0);
        await op.cancelShift();
      }
    },
    async readValue(page, slot) {
      return op.showing(page, slot, async () => {
        if (!await settled(0.6, 0.1, slot)) throw new Error(`parameter ${slot + 1} did not settle`);
        return display().top;
      });
    },
    async readPages() {
      const pages = [];
      for (const layout of PAGES) {
        const sliders = [];
        for (const [slot, s] of layout.sliders.entries()) {
          if (s.name === 'INACTIVE') { sliders.push({ name: s.name, value: '', raw: 0 }); continue; }
          sliders.push({ name: s.name, value: await op.readValue(layout.page, slot), raw: op.stored(layout.column, slot) });
        }
        pages.push({ page: layout.page, heading: layout.heading, column: layout.column, sliders });
      }
      return pages;
    },

    // Take a parameter over without changing it, then move its pot: set its
    // pickup byte's bit 7, the state the firmware reaches itself when the pot
    // crosses the stored value (099F), so no other parameter moves. A pot
    // already reading `value` has to move for the scan to report it.
    async takeOver(page, slot) {
      const p = parameter(page, slot);
      if (!p) throw new Error(`page ${page} pot ${slot + 1} is not a parameter`);
      if (!(peek(p.pickup) & 0x80)) {
        m.poke(p.pickup, peek(p.pickup) | 0x80);
        if (!await m.waitFor(() => peek(p.pickup) & 0x80, 0.2)) throw new Error(`${p.name} did not take the pickup`);
      }
    },
    async moveSlider(page, slot, value) {
      await op.takeOver(page, slot);
      if (page === 2 && !await hold(1, SHIFT)) throw new Error('SHIFT was not seen');
      try {
        if (peek(PANEL224.POTS + slot) === Math.min(value, 253)) await op.setPot(slot, value >= 253 ? 252 : value + 1);
        if (!await op.setPot(slot, value)) return null;
      } finally {
        if (page === 2) await hold(1, 0);
        await op.cancelShift();
      }
      await op.recordSettled();
      return op.readValue(page, slot);
    },
    // A raw -> display table: the pot stepped 0..253 with its parameter
    // shown, run-length encoding the settled display text. The decay,
    // crossover and treble pots quantize by 8 and diffusion by 4, so steps of
    // 4 see every value; DEPTH (x 72/256) and PRE-DELAY (1 or 2 raw per
    // millisecond) are stepped by 1.
    async sweep(page, slot) {
      const step = page === 1 && (slot === DEPTH_SLOT || slot === 5) ? 1 : 4;
      await op.moveSlider(page, slot, 0);
      return op.showing(page, slot, async () => {
        const table = [];
        for (let raw = 0; raw <= 253; raw = Math.min(raw + step, raw === 253 ? 254 : 253)) {
          if (!await op.setPot(slot, raw)) throw new Error(`pot ${slot + 1} did not take ${raw}`);
          if (!await settled(2, 0.12, slot)) throw new Error(`sweep at raw ${raw}: no settled display of pot ${slot + 1}`);
          const text = display().top;
          if (!table.length || table[table.length - 1][1] !== text) table.push([raw, text]);
        }
        return table;
      });
    },

    async describeProgramFully(entry) {
      if (!await op.loadProgram(entry.identity)) throw new Error(`could not load ${entry.name}`);
      const pages = await op.readPages();
      const presets = { 1: pages.map((page) => page.sliders.map((s) => s.value)) };
      const raw = { 1: pages.map((page) => page.sliders.map((s) => s.raw)) };
      const layout = PAGES.map((page) => ({ page: page.page, heading: page.heading, column: page.column,
        sliders: page.sliders.map((s) => ({ name: s.name })) }));
      for (const page of layout)
        for (const [slot, slider] of page.sliders.entries()) {
          if (slider.name === 'INACTIVE') continue;
          try { slider.table = await op.sweep(page.page, slot); }
          catch (e) { throw new Error(`${entry.name}: page ${page.page} pot ${slot + 1}: ${e.message}`); }
        }
      return { ...entry, variations: [1], pages: layout, presets, raw };
    },

    // A load also passes the DEPTH and PRE-DELAY pots' readings through the
    // pickup (03BA -> 05F0, 0633): a pot whose reading quantizes to exactly
    // the program's stored value takes over. For PRE-DELAY that comparison is
    // at half resolution (0654), so the pot can move the loaded value by one
    // step. Which of a loaded program's differences from its catalog presets
    // are that (a parameter now holding what its pot's reading gives)?
    unexplainedLoadDifferences(program, bytes) {
      const differences = [];
      program.pages.forEach((page, i) => page.sliders.forEach((slider, slot) => {
        if (bytes[i][slot] === program.raw[1][i][slot]) return;
        const byPot = page.page === 1 && (slot === DEPTH_SLOT || slot === 5) &&
          parseInt(tableText(slider.table, peek(PANEL224.POTS + slot)), 10) === bytes[i][slot];
        if (!byPot) differences.push(`page ${page.page} ${slider.name} ${program.raw[1][i][slot]}->${bytes[i][slot]}`);
      }));
      return differences;
    },

    // PROGRAM 7 and 8: Mode Enhancement and Decay Optimization (3F65 bits 6, 7).
    async readToggles() {
      const p = peek(PANEL224.PROGRAM);
      return { 'MODE ENH': !!(p & 0x40), 'DECAY OPT': !!(p & 0x80) };
    },
    async setToggle(label, on) {
      const bit = label === 'MODE ENH' ? 0x40 : label === 'DECAY OPT' ? 0x80 : 0;
      if (bit && !!(peek(PANEL224.PROGRAM) & bit) !== on) {
        await op.cancelShift();
        await op.press(0, bit === 0x40 ? PROG7 : PROG8);
        await m.waitFor(() => !!(peek(PANEL224.PROGRAM) & bit) === on, 1);
      }
      return op.readToggles();
    },
  };
  return op;
}

// A run-length value table's text at a pot reading.
function tableText(table, raw) {
  let text = null;
  for (const [start, value] of table || []) { if (start > raw) break; text = value; }
  return text;
}

// Refuse anything that is not a clean 224 catalog.
export function panel224CatalogProblems(catalog, expectedCount) {
  const problems = [];
  if (catalog.programs.length !== expectedCount) problems.push(`${catalog.programs.length} of ${expectedCount} programs`);
  for (const p of catalog.programs)
    for (const page of p.pages) for (const slider of page.sliders)
      if (slider.name !== 'INACTIVE' && !(slider.table || []).length)
        problems.push(`${p.name} page ${page.page}: "${slider.name}" has no table`);
  return problems;
}
