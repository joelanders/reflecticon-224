// The program's parameters as sliders: one column per page, one horizontal
// slider per parameter, titled "name: value" with the firmware's own value
// and a tooltip from the owner's manuals. Shared by the page (app.js) and the
// comparison page (compare.js); moved out of app.js unchanged.
//
//   const params = createParams({ root, op: () => op, operate, known, status });
//
// root      the element that holds the page columns (index.html #pages);
// op()      the operator talking to the running firmware: the LARC (larc.js),
//           the 224X front panel (panel.js) or the original 224's (panel224.js);
// operate   runs an operator task after the ones already queued (the page's own queue);
// known()   the current program's precomputed catalog entry, or null;
// status    shows a line of status text.
//
// params.describe() reads the loaded program's parameters and draws them;
// params.setEnabled(on) disables the sliders while a program or variation
// loads (they belong to the old one); params.pages is what is drawn.

// Slider range: LARC fader values 2-254; panel pot values 0-253 (see panel.js setPot).
export const sliderRange = (op) => op.kind === 'panel' ? [0, 253] : [2, 254];

// A value table is run-length: [[raw, text], ...], text holding from raw on.
export function tableText(table, raw) {
  let text = null;
  for (const [start, value] of table) { if (start > raw) break; text = value; }
  return text;
}
// The raw position for a displayed text; `high` is the top of the slider range.
export function tableRaw(table, text, high) {
  const hit = table.find(([, value]) => value === text);
  if (hit) {
    const next = table[table.indexOf(hit) + 1];
    return Math.round((hit[0] + (next ? next[0] - 1 : high)) / 2);   // the middle of its run
  }
  const wanted = parseFloat(text);                                    // nearest number otherwise
  let best = 128, distance = Infinity;
  for (const [start, value] of table) {
    const d = Math.abs(parseFloat(value) - wanted);
    if (d < distance) { distance = d; best = start; }
  }
  return best;
}

// What each parameter does, from the owner's manuals (page/param_help.json:
// the manual's names, and aliases for every name the displays show).
const SUFFIX = /\s+(\([LR]\)|L\+R\)|LR|[LR]\)[A-D]*|[LR])$/;   // as page/check_param_help.py
export function helpFor(paramHelp, name, heading) {
  if (!paramHelp) return '';
  const full = name.replace(/\s+/g, ' ').trim(), suffix = SUFFIX.exec(full);
  const bare = suffix ? full.slice(0, suffix.index) : full;
  let canonical = paramHelp.aliases[bare];
  // "LEVEL n" and "DELAY n" mean different things per program: the page heading says which.
  if (canonical === 'LEVEL' || canonical === 'DELAY') {
    for (const group of (heading || '').match(/\[[^\]]*\]|[^\[\]]+/g) || []) {
      const words = group.replace(/[[\]]/g, '').replace(/\(.*\)/, '').replace(/\s+/g, ' ').trim();
      if (paramHelp.aliases[words] && paramHelp.aliases[words] !== canonical) { canonical = paramHelp.aliases[words]; break; }
    }
  }
  let text = (paramHelp.params[canonical] || {}).help || '';
  if (suffix) {
    const path = /^(L\+R|[LR])\)([A-D]*)$/.exec(suffix[1]);
    const side = { L: 'left input', R: 'right input', 'L+R': 'both inputs' };
    if (path && path[2]) text += ` Signal path: ${side[path[1]]} to output${path[2].length > 1 ? 's' : ''} ${path[2].split('').join(', ')}.`;
    else if (path) text += ` Fed from the ${side[path[1]]}.`;
    else if (suffix[1] === 'L' || suffix[1] === 'R') text += ` Acts on the ${suffix[1] === 'L' ? 'left' : 'right'} half of this split program.`;
    else if (suffix[1] === 'LR') text += ' Shared by both halves of this split program.';
  }
  return text;
}

export function createParams({ root, op, operate, known, status }) {
  let pages = [], paramHelp = null;
  const range = () => sliderRange(op());

  fetch('param_help.json').then((r) => r.json()).then((help) => { paramHelp = help; draw(); }).catch(() => {});

  // After a load: read every parameter's stored byte straight from the
  // firmware's RAM (no key presses). If they are exactly the catalog's presets
  // for this variation, show the catalog's values at once. Otherwise (no
  // catalog, or the firmware holds edits) read the pages live through the
  // display, keeping the catalog's value tables and names.
  async function describe() {
    const o = op(), program = known();
    const v = program && program.presets[o.current.variation] ? o.current.variation : 1;
    if (program && program.raw && program.pages.every((page) => page.column !== undefined)) {
      const stored = program.pages.map((page) => page.sliders.map((_, slot) => o.stored(page.column, slot)));
      if (JSON.stringify(stored) === JSON.stringify(program.raw[v])) {
        pages = program.pages.map((page, i) => ({ page: page.page, heading: page.heading,
          sliders: page.sliders.map((slider, slot) => ({ ...slider, value: program.presets[v][i][slot], raw: stored[i][slot] })) }));
        draw();
        return;
      }
    }
    status('reading the sliders…');
    const live = await o.readPages();
    live.forEach((page, i) => {
      const knownPage = program && program.pages[i];
      if (!knownPage) return;
      if (o.kind === 'panel') page.heading = knownPage.heading;
      page.sliders.forEach((slider, slot) => {
        const k = knownPage.sliders[slot];
        if (!k) return;
        if (k.table) slider.table = k.table;
        if (o.kind === 'panel') slider.name = k.name;
      });
    });
    pages = live;
    draw();
  }

  // While a program or variation loads, the sliders belong to the old one.
  function setEnabled(on) {
    for (const input of root.querySelectorAll('input')) input.disabled = !on;
  }

  function draw() {
    root.innerHTML = '';
    for (const p of pages) {
      const column = document.createElement('div');
      column.className = 'page';
      column.innerHTML = `<b>Page ${p.page}</b><div class="heading">${p.heading}</div>`;
      p.sliders.forEach((s, slot) => {
        if (s.name === 'INACTIVE' || !s.name) return;
        const cell = document.createElement('label');
        cell.className = 'slider';
        cell.title = helpFor(paramHelp, s.name, p.heading);
        cell.innerHTML = `<span></span><input type="range" min="${range()[0]}" max="${range()[1]}">`;
        const title = cell.querySelector('span'), input = cell.querySelector('input');
        const show = (text) => { s.value = text; title.textContent = `${s.name}: ${text}`; };
        show(s.value);
        // A fader/pot value is the stored byte itself (clamped to the input's
        // range), except on pages that keep a parameter elsewhere (SIZE pages):
        // trust the byte only where the table agrees with the displayed value.
        const byteAgrees = s.raw !== undefined && (!s.table || tableText(s.table, s.raw) === s.value);
        input.setAttribute('value', byteAgrees ? s.raw : s.table ? tableRaw(s.table, s.value, range()[1]) : 128);
        input.oninput = () => {
          if (s.table) show(tableText(s.table, +input.value));           // instant, from the table
          moveSlider(p.page, slot, +input.value, show);                   // then the firmware's echo
        };
        column.appendChild(cell);
      });
      root.appendChild(column);
    }
  }

  // Rapid moves collapse to the latest value per slider.
  const pendingMoves = new Map();
  function moveSlider(page, slot, value, show) {
    const key = `${page}.${slot}`;
    const queued = pendingMoves.has(key);
    pendingMoves.set(key, value);
    if (queued) return;
    operate(async () => {
      const latest = pendingMoves.get(key);
      pendingMoves.delete(key);
      const echo = await op().moveSlider(page, slot, latest);
      if (echo) show(echo);
    });
  }

  return { describe, draw, setEnabled, range, get pages() { return pages; } };
}
