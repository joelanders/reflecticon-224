// The program list, as the easy UI shows it: shared by the page (app.js) and
// the comparison page (compare.js). `op` is the operator that talks to the
// running firmware: the LARC (larc.js), the 224X front panel (panel.js) or
// the original 224's (panel224.js).
import { buttonsFor } from './panel.js';

// Programs are keyed "B.P" on the LARC and by their button mask on the panel.
export function programKey(op, entry) {
  if (op.kind === 'panel') return `x${entry.identity.toString(16)}`;
  return `${entry.bank}.${entry.program}`;
}

// Load a program the way its operator does.
export function selectProgram(op, entry) {
  if (op.kind === 'panel') return op.loadProgram(entry.identity);
  return op.selectProgram(entry.bank, entry.program);
}

// Where the program lives, for its menu entry: the panel's buttons, or "B1 P2".
export function programPlace(op, entry) {
  if (op.kind === 'panel') return entry.buttons || buttonsFor(entry.identity);
  return `B${entry.bank} P${entry.program}`;
}

// The firmware's programs. A precomputed catalog (page/catalogs/HASH.json,
// made by precompute.mjs) has every program's pages, slider value tables
// and variation presets; without one, the program list is scanned live
// (status(text) reports it) and cached in localStorage.
// Returns {catalog: [{bank, program, identity, buttons, bankName, name}], precomputed}.
export async function readCatalog(op, romHash, status) {
  let precomputed = null, catalog = [];
  try {
    const response = await fetch(`catalogs/${romHash}.json`);
    precomputed = response.ok ? await response.json() : null;
  } catch { precomputed = null; }
  const key = `lexicon224x-catalog-${romHash}`;
  if (precomputed) catalog = precomputed.programs.map(({ bank, program, identity, buttons, larc, bankName, name }) =>
    ({ bank: bank ?? (larc ? larc.bank : 0), program, identity, buttons, bankName: bankName || '', name }));
  else try { catalog = JSON.parse(localStorage.getItem(key)) || []; } catch { catalog = []; }
  if (!catalog.length) {
    status('reading the program list…');
    catalog = await op.scanCatalog();
    localStorage.setItem(key, JSON.stringify(catalog));
  }
  return { catalog, precomputed };
}

// Fill a <select> with the catalog, one group per bank.
export function fillProgramMenu(select, catalog, op) {
  select.innerHTML = '';
  let group = null;
  for (const entry of catalog) {
    const label = op.kind === 'panel' ? entry.bankName : `${entry.bank} ${entry.bankName}`;
    if (!group || group.label !== label) {
      group = document.createElement('optgroup');
      group.label = label;
      select.appendChild(group);
    }
    group.appendChild(new Option(`${entry.name} (${programPlace(op, entry)})`, programKey(op, entry)));
  }
}
