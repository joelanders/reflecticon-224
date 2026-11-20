// The original Lexicon 224's front-panel operator, parametrized by the
// firmware's RAM layout, so one operator drives both the v4.x operating
// system (v4.3, v4.4, the "P4.A" TEST set) and the older v3.2.
//
// A copy of ../../web-demo/page/panel224.js (createPanel224Operator),
// changed only in that every RAM address and the page list come from a
// layout. With LAYOUT_V4 it is that file's operator exactly (checked by
// regenerating the v4.3 catalog and comparing it with the shipped one, see
// tools/precompute_extra.mjs).
// If that file changes, this copy has to follow.
//
// The v3.2 layout, read from the v3.2 ROM (addresses are v3.2's; the v4.3
// counterpart in brackets):
//   0176  the scan loop [01EC]: pots 3F00-3F05 [3F20], banks 3F06-3F08
//         [3F26], dispatch 01BE [0234] through the table at 01F8 [026E]:
//         pots 04D2/04DB/04E4/04ED/04F6/051F, PROGRAM 026C, bank 7 0323,
//         display select 0429.
//   0323  bank 7 [0406]: IMED 0361, SET 037B, CALL 0388 (MODE 3F25 := 2
//         [3F45]), SHIFT 0412 (toggles LED bit 3 of 3F1F [3F3F]), REG 0395.
//   026C  PROGRAM [030B]: the loaded program's button in 3F44 bits 0-5
//         [3F65]; ignored while SHIFT is held (3F07 bit 3) or lit.
//   0429  display select [051F]: 3F24 [3F44] = 01 02 04 08 10 20 for
//         selects 1-6, as v4.3.
//   BASS 3F46/3F0B, MID 3F45/3F0A, CROSSOVER 3F47/3F0C, TREBLE 3F48/3F0D
//   (05D2: pot >> 3, 0 counts as 1), DEPTH 3F49/3F0E (04F6), PRE-DELAY
//   3F4C/3F0F (051F; range at 3F4F). The DEPTH pot has no SHIFT branch:
//   v3.2 has no DIFFUSION parameter, so it has one page.
import { createPanelOperator } from '../../web-demo/page/panel.js';

export const LAYOUT_V4 = {
  name: 'v4',
  POTS: 0x3f20, SCAN: 0x3f26, LEDS: 0x3f3f, SHOWN: 0x3f44, MODE: 0x3f45, PROGRAM: 0x3f65,
  pots: [
    { name: 'BASS', cell: 0x3f67, pickup: 0x3f2b },
    { name: 'MID', cell: 0x3f66, pickup: 0x3f2a },
    { name: 'CROSSOVER', cell: 0x3f68, pickup: 0x3f2c },
    { name: 'TREBLE DECAY', cell: 0x3f69, pickup: 0x3f2d },
    { name: 'DEPTH', cell: 0x3f6a, pickup: 0x3f2e },
    { name: 'PRE-DELAY', cell: 0x3f6d, pickup: 0x3f2f },
  ],
  diffusion: { name: 'DIFFUSION', cell: 0x3f6e, pickup: 0x3f30 },
};

export const LAYOUT_V32 = {
  name: 'v3.2',
  POTS: 0x3f00, SCAN: 0x3f06, LEDS: 0x3f1f, SHOWN: 0x3f24, MODE: 0x3f25, PROGRAM: 0x3f44,
  pots: [
    { name: 'BASS', cell: 0x3f46, pickup: 0x3f0b },
    { name: 'MID', cell: 0x3f45, pickup: 0x3f0a },
    { name: 'CROSSOVER', cell: 0x3f47, pickup: 0x3f0c },
    { name: 'TREBLE DECAY', cell: 0x3f48, pickup: 0x3f0d },
    { name: 'DEPTH', cell: 0x3f49, pickup: 0x3f0e },
    { name: 'PRE-DELAY', cell: 0x3f4c, pickup: 0x3f0f },
  ],
  diffusion: null,
};

const CALL = 0x04, SHIFT = 0x08;
const DEPTH_SLOT = 4;

// The owner's manual names (see panel224.js PROGRAM_NAMES): section 3.3
// lists V2.2 and V3.2's six programs in button order.
export const PROGRAM_NAMES = ['SMALL CONCERT HALL B', 'VOCAL PLATE', 'LARGE CONCERT HALL B',
  'ACOUSTIC CHAMBER', 'PERCUSSION PLATE A', 'SMALL CONCERT HALL A'];

export function createPanel224OperatorWith(m, L) {
  const peek = m.peek;
  const base = createPanelOperator(m);
  const { display, settled } = base;
  const scan = (bank) => peek(L.SCAN + bank);
  const PAGES = [{ page: 1, heading: '', column: 0, sliders: L.pots.map((p) => ({ name: p.name })) }];
  if (L.diffusion) {
    PAGES.push({ page: 2, heading: 'SHIFT', column: 1,
      sliders: L.pots.map((_, slot) => ({ name: slot === DEPTH_SLOT ? L.diffusion.name : 'INACTIVE' })) });
  }
  const parameter = (page, slot) => {
    if (page === 1) {
      return L.pots[slot];
    }
    if (L.diffusion && slot === DEPTH_SLOT) {
      return L.diffusion;
    }
    return null;
  };

  async function hold(bank, mask) {
    m.schedule(m.time, () => m.api.button(m.handle, bank, mask));
    return m.waitFor(() => scan(bank) === (~mask & 0xff), 0.5);
  }
  const shiftLit = () => peek(L.LEDS) & SHIFT;

  const op = {
    kind: 'panel', remote: 'panel224', layout: L.name,
    current: { identity: 0, variation: 1 },
    currentPage: 1,
    display, settled,

    async press(bank, mask) {
      const seen = await hold(bank, mask);
      await hold(bank, 0);
      return seen;
    },
    async cancelShift() {
      for (const bank of [0, 1, 2]) {
        await hold(bank, 0);
      }
      if (shiftLit()) {
        await op.press(1, SHIFT);
      }
      return !shiftLit();
    },
    async setPot(slot, value) {
      value = Math.min(value, 253);
      const recorded = peek(L.POTS + slot);
      if (recorded === value) {
        return true;
      }
      let reading = value + 2;
      if (value < recorded) {
        reading = value;
      }
      m.schedule(m.time, () => m.api.pot(m.handle, slot, reading));
      return m.waitFor(() => peek(L.POTS + slot) === value, 0.5);
    },
    async scanCatalog() {
      const catalog = [];
      for (let b = 0; b < 6; b++) {
        const identity = 1 << b;
        if (await op.loadProgram(identity)) {
          catalog.push({ identity, name: PROGRAM_NAMES[b], buttons: `PROG ${b + 1}`, bankName: '' });
        }
      }
      return catalog;
    },
    async loadProgram(identity) {
      for (let attempt = 0; attempt < 3; attempt++) {
        if (attempt) {
          await m.sleep(0.5);
        }
        if (!await op.cancelShift()) {
          continue;
        }
        if (peek(L.MODE) !== 2) {
          await op.press(1, CALL);
          if (!await m.waitFor(() => peek(L.MODE) === 2, 0.5)) {
            continue;
          }
        }
        await op.press(0, identity);
        if (await m.waitFor(() => (peek(L.PROGRAM) & 0x3f) === identity, 1)) {
          op.current = { identity, variation: 1 };
          op.currentPage = 1;
          await op.recordSettled();
          return true;
        }
      }
      return false;
    },
    stored(column, slot) {
      const p = parameter(column + 1, slot);
      if (!p) {
        return 0;
      }
      return peek(p.cell);
    },
    recordSettled() {
      let last = null, since = m.time;
      return m.waitFor(() => {
        const now = Array.from({ length: 12 }, (_, i) => peek(L.PROGRAM + i)).join();
        if (now !== last) {
          last = now;
          since = m.time;
          return false;
        }
        return m.time - since >= 0.3;
      }, 4);
    },
    async showing(page, slot, during) {
      if (page === 2) {
        if (!await hold(1, SHIFT)) {
          throw new Error('SHIFT was not seen');
        }
      }
      if (!await hold(2, 1 << slot)) {
        throw new Error(`select ${slot + 1} was not seen`);
      }
      let shown = [0x01, 0x02, 0x04, 0x08, 0x10, 0x20][slot];
      if (page === 2) {
        shown = 0x40;
      }
      if (!await m.waitFor(() => peek(L.SHOWN) === shown, 0.5)) {
        throw new Error(`the display did not select ${slot + 1}`);
      }
      try {
        return await during();
      } finally {
        await hold(2, 0);
        if (page === 2) {
          await hold(1, 0);
        }
        await op.cancelShift();
      }
    },
    async readValue(page, slot) {
      return op.showing(page, slot, async () => {
        if (!await settled(0.6, 0.1, slot)) {
          throw new Error(`parameter ${slot + 1} did not settle`);
        }
        return display().top;
      });
    },
    async readPages() {
      const pages = [];
      for (const layout of PAGES) {
        const sliders = [];
        for (const [slot, s] of layout.sliders.entries()) {
          if (s.name === 'INACTIVE') {
            sliders.push({ name: s.name, value: '', raw: 0 });
            continue;
          }
          sliders.push({ name: s.name, value: await op.readValue(layout.page, slot), raw: op.stored(layout.column, slot) });
        }
        pages.push({ page: layout.page, heading: layout.heading, column: layout.column, sliders });
      }
      return pages;
    },
    async takeOver(page, slot) {
      const p = parameter(page, slot);
      if (!p) {
        throw new Error(`page ${page} pot ${slot + 1} is not a parameter`);
      }
      if (!(peek(p.pickup) & 0x80)) {
        m.poke(p.pickup, peek(p.pickup) | 0x80);
        if (!await m.waitFor(() => peek(p.pickup) & 0x80, 0.2)) {
          throw new Error(`${p.name} did not take the pickup`);
        }
      }
    },
    async moveSlider(page, slot, value) {
      await op.takeOver(page, slot);
      if (page === 2 && !await hold(1, SHIFT)) {
        throw new Error('SHIFT was not seen');
      }
      try {
        if (peek(L.POTS + slot) === Math.min(value, 253)) {
          let nudge = value + 1;
          if (value >= 253) {
            nudge = 252;
          }
          await op.setPot(slot, nudge);
        }
        if (!await op.setPot(slot, value)) {
          return null;
        }
      } finally {
        if (page === 2) {
          await hold(1, 0);
        }
        await op.cancelShift();
      }
      await op.recordSettled();
      return op.readValue(page, slot);
    },
    async sweep(page, slot) {
      let step = 4;
      if (page === 1 && (slot === DEPTH_SLOT || slot === 5)) {
        step = 1;
      }
      await op.moveSlider(page, slot, 0);
      return op.showing(page, slot, async () => {
        const table = [];
        let raw = 0;
        for (;;) {
          if (!await op.setPot(slot, raw)) {
            throw new Error(`pot ${slot + 1} did not take ${raw}`);
          }
          if (!await settled(2, 0.12, slot)) {
            throw new Error(`sweep at raw ${raw}: no settled display of pot ${slot + 1}`);
          }
          const text = display().top;
          if (!table.length || table[table.length - 1][1] !== text) {
            table.push([raw, text]);
          }
          // panel224.js: raw = min(raw + step, raw === 253 ? 254 : 253), stop at 254.
          if (raw === 253) {
            break;
          }
          raw = Math.min(raw + step, 253);
        }
        return table;
      });
    },
    async describeProgramFully(entry) {
      if (!await op.loadProgram(entry.identity)) {
        throw new Error(`could not load ${entry.name}`);
      }
      const pages = await op.readPages();
      const presets = { 1: pages.map((page) => page.sliders.map((s) => s.value)) };
      const raw = { 1: pages.map((page) => page.sliders.map((s) => s.raw)) };
      const layout = PAGES.map((page) => ({ page: page.page, heading: page.heading, column: page.column,
        sliders: page.sliders.map((s) => ({ name: s.name })) }));
      for (const page of layout) {
        for (const [slot, slider] of page.sliders.entries()) {
          if (slider.name === 'INACTIVE') {
            continue;
          }
          try {
            slider.table = await op.sweep(page.page, slot);
          } catch (e) {
            throw new Error(`${entry.name}: page ${page.page} pot ${slot + 1}: ${e.message}`);
          }
        }
      }
      return { ...entry, variations: [1], pages: layout, presets, raw };
    },
  };
  return op;
}
