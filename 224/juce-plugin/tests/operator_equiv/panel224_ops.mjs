// Test support: the JS reference operator for 224 sets that
// panel224.js cannot drive (v3.2; the TEST set, whose catalog is not in
// rows-sv). It is ../../tools/panel224_layouts.mjs's createPanel224OperatorWith
// (not edited), plus the members panel224.js has and that copy
// lacks, written as panel224.js writes them but with the layout's
// addresses: selectProgram, loadVariation, gotoPage, unexplainedLoadDifferences
// and readToggles/setToggle (PROGRAM bits 6, 7: v4.3's 030B; on v3.2
// measured on the row machine to behave the same, see panel224_layouts.hpp). The C++ operator
// (source/operator/panel224_operator.hpp) is compared against this.
import { createPanel224OperatorWith, LAYOUT_V4, LAYOUT_V32 } from '../../tools/panel224_layouts.mjs';

export { LAYOUT_V4, LAYOUT_V32 };
export const layoutFor = (catalog) => (catalog.layout === 'v3.2' ? LAYOUT_V32 : LAYOUT_V4);

const DEPTH_SLOT = 4;
const PROG7 = 0x40, PROG8 = 0x80;

function tableText(table, raw) {
  let text = null;
  for (const [start, value] of table || []) {
    if (start > raw) {
      break;
    }
    text = value;
  }
  return text;
}

export function createPanel224LayoutOperator(m, L) {
  const op = createPanel224OperatorWith(m, L);
  const peek = m.peek;
  op.selectProgram = (entry) => op.loadProgram(entry.identity);
  op.loadVariation = async (v) => v === 1 && op.loadProgram(op.current.identity);
  op.gotoPage = async (page) => { op.currentPage = page; };
  op.unexplainedLoadDifferences = (program, bytes) => {
    const differences = [];
    program.pages.forEach((page, i) => page.sliders.forEach((slider, slot) => {
      if (bytes[i][slot] === program.raw[1][i][slot]) {
        return;
      }
      const byPot = page.page === 1 && (slot === DEPTH_SLOT || slot === 5) &&
        parseInt(tableText(slider.table, peek(L.POTS + slot)), 10) === bytes[i][slot];
      if (!byPot) {
        differences.push(`page ${page.page} ${slider.name} ${program.raw[1][i][slot]}->${bytes[i][slot]}`);
      }
    }));
    return differences;
  };
  {
    op.readToggles = async () => {
      const p = peek(L.PROGRAM);
      return { 'MODE ENH': !!(p & 0x40), 'DECAY OPT': !!(p & 0x80) };
    };
    op.setToggle = async (label, on) => {
      let bit = 0;
      if (label === 'MODE ENH') {
        bit = 0x40;
      } else if (label === 'DECAY OPT') {
        bit = 0x80;
      }
      if (bit && !!(peek(L.PROGRAM) & bit) !== on) {
        await op.cancelShift();
        let button = PROG8;
        if (bit === 0x40) {
          button = PROG7;
        }
        await op.press(0, button);
        await m.waitFor(() => !!(peek(L.PROGRAM) & bit) === on, 1);
      }
      return op.readToggles();
    };
  }
  return op;
}
