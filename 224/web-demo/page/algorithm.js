// The flow page: one WCS program, its rows, and how values move between
// them. The analysis is the C++ flow lens (../lens/flow.hpp) behind
// lex_flow(); this file loads a program and draws what the lens found.
import createLexicon from './lexicon224x.js';
import { findAnnotation } from './annotations.mjs';

const $ = (id) => document.getElementById(id);
const Module = await createLexicon();
const lexFlow = Module.cwrap('lex_flow', 'number', ['number', 'number', 'number']);
const wcs = Module._malloc(512);

let flow = null;          // what lex_flow() returned, plus the indexes below
let pinned = null;        // the pinned focus, e.g. 'row:13' or 'block:2'
let rowElements = [];
let diagramEdges = [];    // the diagram's arrows: {from, to, links} between blocks

// =====================================================================
// Loading
// =====================================================================

// A 512-byte image in the SBC's byte order: row r at (r ^ 127) * 4, each
// byte complemented by the bus (load_wcs in ../cpp/lexicon224x.hpp).
function putImage(image) {
  const words = new Uint32Array(Module.HEAPU32.buffer, wcs, 128);
  for (let row = 0; row < 128; row++) {
    let word = 0;
    for (let lane = 0; lane < 4; lane++) {
      word |= (image[(row ^ 127) * 4 + lane] ^ 0xff) << (8 * lane);
    }
    words[row] = word >>> 0;
  }
}

// A hex dump: "4000: 02 21 20 3e ..." lines, the bytes of 4000-41FF in order.
function imageFromHex(text) {
  const bytes = [];
  for (const line of text.split('\n')) {
    const colon = line.indexOf(':');
    if (colon < 0) {
      continue;
    }
    for (const pair of line.slice(colon + 1).trim().split(/\s+/)) {
      if (/^[0-9a-fA-F]{2}$/.test(pair)) {
        bytes.push(parseInt(pair, 16));
      }
    }
  }
  return Uint8Array.from(bytes);
}

async function load(bytes, name) {
  let image = bytes;
  if (bytes.length !== 512) {
    image = imageFromHex(new TextDecoder().decode(bytes));
  }
  if (image.length !== 512) {
    $('summary').textContent = `${name}: not a 512-byte WCS image or a hex dump of one`;
    return;
  }
  putImage(image);
  // The machine decides the decode: the 224X/224XL, or the original 224.
  flow = JSON.parse(Module.UTF8ToString(lexFlow(wcs, 1, +$('model').value)));
  flow.name = name;
  flow.into = Array.from({ length: flow.rows }, () => []);
  flow.outOf = Array.from({ length: flow.rows }, () => []);
  for (const link of flow.links) {
    flow.into[link.to].push(link);
    flow.outOf[link.from].push(link);
  }
  flow.annotation = await findAnnotation(flow, 'annotations');
  pinned = null;
  render();
}

let loaded = null;       // the last program's bytes and name, to re-read under another model
$('file').onchange = async (event) => {
  const file = event.target.files[0];
  if (file) {
    loaded = [new Uint8Array(await file.arrayBuffer()), file.name];
    await load(...loaded);
  }
};
$('model').onchange = () => loaded && load(...loaded);

// =====================================================================
// Words
// =====================================================================

function ms(passes) {
  return (passes * flow.rows * flow.rowNs / 1e6).toFixed(2);
}

function samples(passes) {
  if (passes === 1) {
    return '1 sample';
  }
  return `${passes} samples`;
}

function rowList(rows) {
  return rows.map((row) => `row ${row}`).join(', ');
}

// What drives the bus, as an expression.
function busExpression(m) {
  if (m.source === 'memory') {
    return `mem[cpc−${m.offset}]`;
  }
  if (m.source === 'none') {
    return '0';
  }
  return m.source;
}

// The row as statements, in the order the row does them (README.md, "One row").
function statements(m) {
  const out = [];
  const bus = busExpression(m);
  if (m.op === 'MEMW') {
    out.push({ text: `mem[cpc−${m.offset}] ← RR` });
  }
  const registerUsed = flow.outOf[m.row].some((link) => link.kind === 'register');
  if (m.op === 'MEMR' || m.op === 'OPER' || registerUsed) {
    out.push({ text: `R${m.wa} ← ${bus}`, dead: !registerUsed });
  }
  if (m.dac) {
    out.push({ text: `DAC ${m.dac} ← ${bus}` });
  }
  if (m.wrXreg) {
    out.push({ text: `XREG ← ${bus}` });
  }
  if (m.xfer) {
    out.push({ text: 'RR ← acc' });
  }
  if (m.zero) {
    out.push({ text: 'acc ← 0' });
  }
  if (m.c !== 0) {
    out.push({ text: `acc ${sign(m)}= ${m.c}/32·${multiplicand(m)}` });
  }
  if (m.reset) {
    out.push({ text: 'RESET' });
  }
  return out;
}

function sign(m) {
  if (m.negative) {
    return '−';
  }
  return '+';
}

// What the row multiplies: R[RA], or with keep-shifting the previous multiplicand / 64.
function multiplicand(m) {
  if (m.keepShifting) {
    return 'X/64';
  }
  return `R${m.ra}`;
}

function operand(m) {
  if (m.op === 'MEMR' || m.op === 'MEMW') {
    return `off ${m.offset}`;
  }
  if (m.op !== 'OPER') {
    return '';
  }
  let text = `bus←${m.source}`;
  if (m.dac) {
    text += ` WR_DA ${m.dac}`;
  }
  if (m.wrXreg) {
    text += ' WR_XREG';
  }
  return text;
}

function flags(m) {
  const out = [];
  if (m.xfer) {
    out.push('XFER');
  }
  if (m.zero) {
    out.push('ZERO');
  }
  if (m.keepShifting) {
    out.push('KS');
  }
  return out.join(' ');
}

// =====================================================================
// Drawing
// =====================================================================

function cell(tr, text, className) {
  const td = document.createElement('td');
  td.textContent = text;
  if (className) {
    td.className = className;
  }
  tr.appendChild(td);
  return td;
}

// Hovering shows a thing; clicking pins it (click again, or Escape, to let go).
function hoverable(element, key, show) {
  element.onmouseenter = () => {
    if (!pinned) {
      show();
    }
  };
  element.onclick = () => {
    if (pinned === key) {
      pinned = null;
      clearFocus();
      return;
    }
    pinned = key;
    show();
  };
}

function unhoverable(element) {
  element.onmouseleave = () => {
    if (!pinned) {
      clearFocus();
    }
  };
}

function render() {
  const check = flow.check;
  let checked = `check: all ${check.reads.toLocaleString()} delay-memory reads in a ` +
    `${check.passes.toLocaleString()}-pass run came from the row and lag shown`;
  if (check.agree !== check.reads) {
    checked = `check FAILED: ${check.reads - check.agree} of ${check.reads} reads disagree (${check.disagreements[0]})`;
  }
  const rate = 1e9 / (flow.rows * flow.rowNs);
  $('summary').textContent = `${flow.name}: ${flow.rows} rows per pass · ${rate.toFixed(1)} Hz · ` +
    `${flow.lines.length} delay lines · ${checked}`;
  flow.blockOf = new Array(flow.rows).fill(-1);
  if (flow.annotation) {
    flow.annotation.blocks.forEach((block, index) => {
      for (let row = block.rows[0]; row <= block.rows[1] && row < flow.rows; row++) {
        flow.blockOf[row] = index;
      }
    });
  }
  renderListing();
  renderLines();
  renderAnnotation();
  clearFocus();
  let focus = new URLSearchParams(location.search).get('focus');
  if (focus !== null) {
    if (!focus.includes(':')) {
      focus = `row:${focus}`;
    }
    const [kind, value] = focus.split(':');
    const index = Number(value);
    pinned = focus;
    if (kind === 'block') {
      focusBlock(index);
    } else if (kind === 'feature') {
      focusFeature(index);
    } else if (kind === 'line') {
      focusLine(index);
    } else {
      focusRow(index);
    }
  }
}

function renderListing() {
  const table = $('listing');
  table.innerHTML = '';
  const head = document.createElement('tr');
  const titles = ['row', 'step', 'block', 'op', 'coeff', 'RA', 'WA', '', 'operand', 'does'];
  if (!flow.annotation) {
    titles.splice(titles.indexOf('block'), 1);
  }
  for (const title of titles) {
    const th = document.createElement('th');
    th.textContent = title;
    if (title === 'step') {
      th.title = 'dump step, 127 − row: the numbering the wiki and the notes use';
    }
    head.appendChild(th);
  }
  table.appendChild(head);
  rowElements = [];
  for (const m of flow.listing) {
    const tr = document.createElement('tr');
    tr.className = 'row';
    const quiet = m.op === 'NOP' && m.c === 0 && !m.xfer && !m.zero && flow.outOf[m.row].length === 0;
    if (quiet) {
      tr.classList.add('quiet');
    }
    const block = flow.blockOf[m.row];
    const blockStart = block >= 0 && (m.row === 0 || flow.blockOf[m.row - 1] !== block);
    if (blockStart) {
      tr.classList.add('block-start');
    }
    cell(tr, String(m.row), 'num');
    cell(tr, String(127 - m.row), 'num muted');
    if (flow.annotation) {
      let blockName = '';
      if (blockStart) {
        blockName = flow.annotation.blocks[block].name;
      }
      cell(tr, blockName, 'blockname');
    }
    cell(tr, m.op);
    let coefficient = '';
    if (m.c !== 0) {
      coefficient = `${sign(m)}${m.c}/32`;
    }
    cell(tr, coefficient);
    cell(tr, `R${m.ra}`);
    cell(tr, `R${m.wa}`);
    cell(tr, flags(m));
    cell(tr, operand(m));
    const does = cell(tr, '', 'does');
    statements(m).forEach((statement, index) => {
      if (index > 0) {
        does.appendChild(document.createTextNode('; '));
      }
      const span = document.createElement('span');
      span.textContent = statement.text;
      if (statement.dead) {
        span.className = 'dead';
        span.title = 'no later row multiplies this register value';
      }
      does.appendChild(span);
    });
    hoverable(tr, `row:${m.row}`, () => focusRow(m.row));
    rowElements.push(tr);
    table.appendChild(tr);
  }
  unhoverable(table);
}

function renderLines() {
  const lines = $('lines');
  lines.innerHTML = '<tr><th>write</th><th>taps: row, delay in samples (ms)</th></tr>';
  flow.lines.forEach((line, index) => {
    const tr = document.createElement('tr');
    cell(tr, `row ${line.write}`);
    const taps = cell(tr, '');
    if (line.taps.length === 0) {
      taps.textContent = 'never read';
      taps.className = 'muted';
    }
    for (const tap of line.taps) {
      const span = document.createElement('span');
      span.className = 'tap';
      span.textContent = `${tap.row}: ${tap.passes} (${ms(tap.passes)})`;
      taps.appendChild(span);
    }
    hoverable(tr, `line:${index}`, () => focusLine(index));
    lines.appendChild(tr);
  });
  if (flow.unwrittenReads.length) {
    const tr = document.createElement('tr');
    cell(tr, 'none');
    cell(tr, `read but never written: ${rowList(flow.unwrittenReads)}`, 'muted');
    lines.appendChild(tr);
  }
  unhoverable(lines);
}

// The annotation: its blocks and named rows, and the block diagram.
function renderAnnotation() {
  const annotation = flow.annotation;
  $('blocksCard').hidden = !annotation;
  $('diagramCard').hidden = !annotation;
  if (!annotation) {
    return;
  }
  $('annotationTitle').textContent = annotation.title;
  $('annotationAbout').textContent = annotation.about;
  const list = $('blocks');
  list.innerHTML = '';
  annotation.blocks.forEach((block, index) => {
    const li = document.createElement('li');
    li.innerHTML = `<b>${block.name}</b> <span class="muted">rows ${block.rows[0]}–${block.rows[1]}</span>`;
    hoverable(li, `block:${index}`, () => focusBlock(index));
    list.appendChild(li);
  });
  unhoverable(list);
  const features = $('features');
  features.innerHTML = '';
  annotation.features.forEach((feature, index) => {
    const li = document.createElement('li');
    li.innerHTML = `${feature.name} <span class="muted">rows ${feature.rows.join(', ')}</span>`;
    hoverable(li, `feature:${index}`, () => focusFeature(index));
    features.appendChild(li);
  });
  unhoverable(features);
  renderDiagram();
}

// Blocks on a grid, joined by the delay lines between them: an arrow from the
// block that stores to the block that reads, as thick as the number of taps.
function renderDiagram() {
  const annotation = flow.annotation;
  const layout = annotation.diagram;
  const width = 1200;
  const columnWidth = width / layout.columns;
  const nodeWidth = columnWidth - 46;
  const nodeHeight = 52;
  const rowY = [110, 250];
  const height = 410;
  const svg = $('diagram');
  svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
  const at = {};
  annotation.blocks.forEach((block, index) => {
    const position = layout.nodes[block.id];
    if (position) {
      at[index] = { x: position[0] * columnWidth + 23, y: rowY[position[1]], row: position[1] };
    }
  });

  // Memory links between blocks, grouped; links inside a block are counted.
  const groups = new Map();
  const internal = new Array(annotation.blocks.length).fill(0);
  for (const link of flow.links) {
    if (link.kind !== 'memory') {
      continue;
    }
    const a = flow.blockOf[link.from];
    const b = flow.blockOf[link.to];
    if (a < 0 || b < 0 || !at[a] || !at[b]) {
      continue;
    }
    if (a === b) {
      internal[a]++;
      continue;
    }
    const key = `${a}>${b}`;
    if (!groups.has(key)) {
      groups.set(key, { from: a, to: b, links: [] });
    }
    groups.get(key).links.push(link);
  }
  diagramEdges = [...groups.values()];

  let markup = '<defs><marker id="diagram-arrow" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="9" ' +
    'markerHeight="9" markerUnits="userSpaceOnUse" orient="auto"><path d="M0,0 L8,4 L0,8 z" ' +
    'style="fill: var(--memory)"/></marker></defs>';
  diagramEdges.forEach((edge, index) => {
    const a = at[edge.from];
    const b = at[edge.to];
    const ax = a.x + nodeWidth / 2;
    const bx = b.x + nodeWidth / 2;
    // Arrows going right sit on one side, arrows going back on the other.
    let back = 0;
    if (edge.from > edge.to) {
      back = 1;
    }
    let d;
    if (a.row === b.row) {
      // Same row: an arc above the top row, or below the bottom one.
      let direction = 1;
      let edgeY = a.y + nodeHeight;
      if (a.row === 0) {
        direction = -1;
        edgeY = a.y;
      }
      const lift = 16 + Math.abs(bx - ax) * 0.07 + back * 12;
      d = `M ${ax} ${edgeY} C ${ax} ${edgeY + direction * lift}, ${bx} ${edgeY + direction * lift}, ${bx} ${edgeY}`;
    } else {
      // Between the rows: from the bottom of the top node to the top of the bottom one.
      const ay = a.y + (1 - a.row) * nodeHeight;
      const by = b.y + (1 - b.row) * nodeHeight;
      const shift = (back * 2 - 1) * 6;
      d = `M ${ax + shift} ${ay} C ${ax + shift} ${(ay + by) / 2}, ${bx + shift} ${(ay + by) / 2}, ${bx + shift} ${by}`;
    }
    const stroke = 1 + Math.min(4, edge.links.length);
    markup += `<path class="edge" data-edge="${index}" d="${d}" fill="none" style="stroke: var(--memory)" ` +
      `stroke-width="${stroke}" marker-end="url(#diagram-arrow)"/>`;
  });
  annotation.blocks.forEach((block, index) => {
    const p = at[index];
    if (!p) {
      return;
    }
    let sub = `rows ${block.rows[0]}–${block.rows[1]}`;
    if (internal[index]) {
      sub += ` · ${internal[index]} inside`;
    }
    markup += `<g class="node" data-block="${index}"><rect x="${p.x}" y="${p.y}" width="${nodeWidth}" ` +
      `height="${nodeHeight}" rx="8"/><text x="${p.x + nodeWidth / 2}" y="${p.y + 22}" text-anchor="middle" ` +
      `class="node-name">${block.name}</text><text x="${p.x + nodeWidth / 2}" y="${p.y + 40}" ` +
      `text-anchor="middle" class="node-sub">${sub}</text></g>`;
  });
  svg.innerHTML = markup;
  for (const node of svg.querySelectorAll('.node')) {
    const index = Number(node.dataset.block);
    hoverable(node, `block:${index}`, () => focusBlock(index));
  }
  for (const path of svg.querySelectorAll('.edge')) {
    const index = Number(path.dataset.edge);
    hoverable(path, `edge:${index}`, () => focusEdge(index));
  }
  unhoverable(svg);
}

// =====================================================================
// Focus: a set of rows, and the links that cross into or out of it
// =====================================================================

function clearFocus() {
  for (const tr of rowElements) {
    tr.classList.remove('focus', 'from', 'to', 'from-register', 'from-shift', 'from-product', 'from-result',
      'from-memory');
  }
  for (const element of document.querySelectorAll('.lit')) {
    element.classList.remove('lit');
  }
  $('gutter').innerHTML = '';
  $('explain').innerHTML = '<span class="muted">Hover a row, a delay line, a block or an arrow.</span>';
}

// Mark `rows`, the rows at the far end of each of `links`, draw the links,
// and light up the blocks, delay lines and named rows involved.
function show(rows, links, html) {
  clearFocus();
  const inside = new Set(rows);
  for (const row of rows) {
    rowElements[row].classList.add('focus');
  }
  for (const link of links) {
    if (!inside.has(link.from)) {
      rowElements[link.from].classList.add('from', `from-${link.kind}`);
    }
    if (!inside.has(link.to)) {
      rowElements[link.to].classList.add('to');
    }
  }
  drawArcs(links);
  const touched = new Set(rows);
  const lines = $('lines').querySelectorAll('tr');
  flow.lines.forEach((line, index) => {
    if (touched.has(line.write) || line.taps.some((tap) => touched.has(tap.row))) {
      lines[index + 1].classList.add('lit');
    }
  });
  if (flow.annotation) {
    const blocks = new Set(rows.map((row) => flow.blockOf[row]));
    for (const node of $('diagram').querySelectorAll('.node')) {
      if (blocks.has(Number(node.dataset.block))) {
        node.classList.add('lit');
      }
    }
    const linkSet = new Set(links);
    for (const path of $('diagram').querySelectorAll('.edge')) {
      if (diagramEdges[Number(path.dataset.edge)].links.some((link) => linkSet.has(link))) {
        path.classList.add('lit');
      }
    }
    $('blocks').querySelectorAll('li').forEach((li, index) => {
      if (blocks.has(index)) {
        li.classList.add('lit');
      }
    });
    $('features').querySelectorAll('li').forEach((li, index) => {
      if (flow.annotation.features[index].rows.some((row) => touched.has(row))) {
        li.classList.add('lit');
      }
    });
  }
  $('explain').innerHTML = html;
}

function linksTouching(rows) {
  const inside = new Set(rows);
  return flow.links.filter((link) => inside.has(link.from) !== inside.has(link.to) ||
    (rows.length === 1 && link.from === rows[0] && link.to === rows[0]));
}

function focusRow(row) {
  show([row], [...flow.into[row], ...flow.outOf[row]], rowSentences(row));
}

function focusLine(index) {
  const line = flow.lines[index];
  const links = flow.outOf[line.write].filter((link) => link.kind === 'memory');
  const taps = line.taps.map((tap) => `row ${tap.row} after ${samples(tap.passes)} (${ms(tap.passes)} ms)`);
  let html = `<p><b>Delay line</b> stored by row ${line.write}.</p>`;
  if (taps.length) {
    html += `<p>Read back by ${taps.join(', ')}.</p>`;
  } else {
    html += '<p class="muted">No row reads it back.</p>';
  }
  show([line.write], links, html);
}

function blockRows(block) {
  const rows = [];
  for (let row = block.rows[0]; row <= block.rows[1] && row < flow.rows; row++) {
    rows.push(row);
  }
  return rows;
}

// Where a set of rows gets its delayed values from, and where it sends them.
function memorySummary(rows) {
  const inside = new Set(rows);
  const name = (row) => {
    const block = flow.blockOf[row];
    if (block >= 0) {
      return flow.annotation.blocks[block].name;
    }
    return `row ${row}`;
  };
  const incoming = flow.links.filter((l) => l.kind === 'memory' && inside.has(l.to) && !inside.has(l.from));
  const outgoing = flow.links.filter((l) => l.kind === 'memory' && inside.has(l.from) && !inside.has(l.to));
  let html = '';
  if (incoming.length) {
    const items = incoming.map((l) => `${name(l.from)} (row ${l.from} → ${l.to}, ${ms(l.detail)} ms)`);
    html += `<p>${'<span class="kind k-memory">reads</span>'} ${items.join('; ')}.</p>`;
  }
  if (outgoing.length) {
    const items = outgoing.map((l) => `${name(l.to)} (row ${l.from} → ${l.to}, ${ms(l.detail)} ms)`);
    html += `<p>${'<span class="kind k-memory">feeds</span>'} ${items.join('; ')}.</p>`;
  }
  return html;
}

function focusBlock(index) {
  const block = flow.annotation.blocks[index];
  const rows = blockRows(block);
  let html = `<p><b>${block.name}</b> <span class="muted">rows ${block.rows[0]}–${block.rows[1]} ` +
    `(steps ${block.steps})</span></p><p>${block.text}</p>`;
  html += memorySummary(rows);
  show(rows, linksTouching(rows), html);
}

function focusFeature(index) {
  const feature = flow.annotation.features[index];
  const rows = feature.rows.filter((row) => row < flow.rows);
  const steps = rows.map((row) => 127 - row).join(', ');
  let html = `<p><b>${feature.name}</b> <span class="muted">rows ${rows.join(', ')} (steps ${steps})</span></p>`;
  if (feature.text) {
    html += `<p>${feature.text}</p>`;
  }
  const coefficients = rows.map((row) => {
    const m = flow.listing[row];
    return `row ${row}: ${m.op} ${sign(m)}${m.c}/32`;
  });
  html += `<p class="muted">${coefficients.join(' · ')}</p>`;
  show(rows, linksTouching(rows), html);
}

function focusEdge(index) {
  const edge = diagramEdges[index];
  const blocks = flow.annotation.blocks;
  const rows = [...new Set(edge.links.flatMap((l) => [l.from, l.to]))];
  const items = edge.links.map((l) => `row ${l.from} → row ${l.to}: ${samples(l.detail)} (${ms(l.detail)} ms)`);
  const html = `<p><b>${blocks[edge.from].name} → ${blocks[edge.to].name}</b></p>` +
    `<p>${'<span class="kind k-memory">memory</span>'} ${items.join('; ')}.</p>`;
  show(rows, edge.links, html);
}

document.addEventListener('keydown', (event) => {
  if (event.key === 'Escape' && flow) {
    pinned = null;
    clearFocus();
  }
});

// Arcs in the gutter, from each link's row to the other, coloured by kind.
function drawArcs(links) {
  const svg = $('gutter');
  const table = $('listing');
  svg.setAttribute('height', table.offsetTop + table.offsetHeight);
  const kinds = ['register', 'shift', 'product', 'result', 'memory'];
  let markup = '<defs>';
  for (const kind of kinds) {
    markup += `<marker id="arrow-${kind}" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" ` +
      `orient="auto"><path d="M0,0 L8,4 L0,8 z" style="fill: var(--${kind})"/></marker>`;
  }
  markup += '</defs>';
  const right = 106;
  const y = (row) => table.offsetTop + rowElements[row].offsetTop + rowElements[row].offsetHeight / 2;
  for (const link of links) {
    const ya = y(link.from);
    const yb = y(link.to);
    const kindIndex = kinds.indexOf(link.kind);
    const depth = 14 + kindIndex * 4 + 70 * Math.min(1, Math.abs(yb - ya) / 500);
    let d = `M ${right} ${ya} C ${right - depth} ${ya}, ${right - depth} ${yb}, ${right} ${yb}`;
    if (link.from === link.to) {
      d = `M ${right} ${ya - 3} C ${right - 16} ${ya - 9}, ${right - 16} ${yb + 9}, ${right} ${yb + 3}`;
    }
    let dash = '';
    if (link.earlierPass) {
      dash = ' stroke-dasharray="4 3"';
    }
    markup += `<path d="${d}" fill="none" style="stroke: var(--${link.kind})" stroke-width="1.6"${dash} ` +
      `marker-end="url(#arrow-${link.kind})"/>`;
    if (link.kind === 'memory') {
      markup += `<text x="${right - depth * 0.75 - 2}" y="${(ya + yb) / 2 + 4}" text-anchor="end" ` +
        `style="fill: var(--memory); font: 10px ui-monospace, monospace">${link.detail}</text>`;
    }
  }
  svg.innerHTML = markup;
}

// The row in sentences: what it does, where its values come from, where they go.
function rowSentences(row) {
  const m = flow.listing[row];
  const into = flow.into[row];
  const outOf = flow.outOf[row];
  const kind = (k) => `<span class="kind k-${k}">${k}</span>`;
  const earlier = (link) => {
    if (link.earlierPass && link.kind !== 'memory') {
      return ' in the previous pass';
    }
    return '';
  };
  const out = [`<p><b>Row ${row}</b>: <code>${statements(m).map((s) => s.text).join('; ')}</code></p>`];

  for (const link of into.filter((l) => l.kind === 'memory')) {
    out.push(`<p>${kind('memory')} reads what row ${link.from} stored ${samples(link.detail)} ` +
      `(${ms(link.detail)} ms) ago.</p>`);
  }
  if (m.op === 'MEMR' && !into.some((l) => l.kind === 'memory')) {
    out.push('<p class="muted">Reads an address no row writes.</p>');
  }
  for (const link of into.filter((l) => l.kind === 'result')) {
    let use = 'puts it on the bus';
    if (m.op === 'MEMW') {
      use = 'stores it in delay memory';
    } else if (m.dac) {
      use = `sends it to DAC ${m.dac}`;
    }
    out.push(`<p>${kind('result')} takes the RR that row ${link.from} saved${earlier(link)} and ${use}.</p>`);
  }
  if (m.op === 'MEMW' && m.xfer) {
    out.push('<p class="muted">The store uses RR from before this row\'s own XFER.</p>');
  }
  for (const link of into.filter((l) => l.kind === 'register')) {
    let who = `row ${link.from}${earlier(link)}`;
    if (link.from === row) {
      who = 'this row (its register write comes before the multiply)';
    }
    out.push(`<p>${kind('register')} multiplies R${link.detail}, written by ${who}.</p>`);
  }
  for (const link of into.filter((l) => l.kind === 'shift')) {
    out.push(`<p>${kind('shift')} keeps shifting row ${link.from}'s multiplicand.</p>`);
  }
  const products = into.filter((l) => l.kind === 'product');
  if (products.length) {
    const rows = products.map((l) => {
      if (l.earlierPass) {
        return `${l.from}′`;
      }
      return String(l.from);
    }).join(', ');
    let note = '';
    if (products.some((l) => l.earlierPass)) {
      note = ' (′: previous pass)';
    }
    out.push(`<p>${kind('product')} XFER saves into RR the sum of the products of rows ${rows}${note}.</p>`);
  }
  if (m.zero) {
    out.push('<p class="muted">ZERO: a new sum starts with this row\'s product.</p>');
  }

  if ((m.op === 'MEMR' || m.op === 'OPER') && !m.busUsed) {
    out.push(`<p class="muted">Nothing uses this row's bus value: R${m.wa} is written again before any ` +
      'row multiplies it.</p>');
  }
  const registerUses = outOf.filter((l) => l.kind === 'register' && l.to !== row);
  if (registerUses.length) {
    out.push(`<p>→ R${m.wa} is multiplied by ${rowList(registerUses.map((l) => l.to))}.</p>`);
  }
  for (const link of outOf.filter((l) => l.kind === 'product')) {
    out.push(`<p>→ its product is part of the sum row ${link.to} saves.</p>`);
  }
  const resultUses = outOf.filter((l) => l.kind === 'result');
  if (resultUses.length) {
    out.push(`<p>→ the RR it saves is used by ${rowList(resultUses.map((l) => l.to))}.</p>`);
  }
  for (const link of outOf.filter((l) => l.kind === 'shift')) {
    out.push(`<p>→ row ${link.to} keeps shifting its multiplicand.</p>`);
  }
  const reads = outOf.filter((l) => l.kind === 'memory');
  if (reads.length) {
    const taps = reads.map((l) => `row ${l.to} (${samples(l.detail)}, ${ms(l.detail)} ms)`).join(', ');
    out.push(`<p>→ read back by ${taps}.</p>`);
  } else if (m.op === 'MEMW') {
    out.push('<p class="muted">No row reads this back.</p>');
  }
  return out.join('');
}

// Development: ?url=... loads a file the server can see (?model=224 for an
// original 224 program); ?focus=13, ?focus=block:3 or ?focus=feature:2 pins
// something.
const params = new URLSearchParams(location.search);
if (params.get('model') === '224') {
  $('model').value = '1';
}
const url = params.get('url');
if (url) {
  const response = await fetch(url);
  loaded = [new Uint8Array(await response.arrayBuffer()), url.split('/').pop()];
  await load(...loaded);
}
