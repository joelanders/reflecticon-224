#!/usr/bin/env node
// The web demo's answers, for the C++ catalog tests (catalog_test.cpp) to
// compare against. It runs the page's OWN code: helpFor, tableText and
// tableRaw are cut out of page/app.js by their source lines, and share links
// come from web/share.js itself. Only the catalog bookkeeping (keys, labels,
// named-slider order) is restated here, as app.js writes it.
//
//   node tests/catalog/js_reference.mjs OUT.json
import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const web = join(here, '..', '..', '..', 'web-demo', 'page');
const out = process.argv[2];
if (!out) { console.error('usage: node js_reference.mjs OUT.json'); process.exit(2); }

// --- the page's functions, from app.js ---------------------------------
const app = readFileSync(join(web, 'app.js'), 'utf8');
function cut(from, to) {
  const a = app.indexOf(from), b = app.indexOf(to, a + 1);
  if (a < 0 || b < 0) throw new Error(`app.js: cannot find ${from} .. ${to}`);
  return app.slice(a, b);
}
const helpSource = cut('const SUFFIX =', 'function drawPages(');
const tableSource = cut('function tableText(', '// What each parameter does');
const paramHelp = JSON.parse(readFileSync(join(web, 'param_help.json'), 'utf8'));
let op = { kind: 'larc' };
const page = new Function('paramHelp', 'getOp', `
  const op = new Proxy({}, { get: (_, k) => getOp()[k] });
  const range = () => op.kind === 'panel' ? [0, 253] : [2, 254];
  ${tableSource}
  ${helpSource}
  return { helpFor, tableText, tableRaw };`)(paramHelp, () => op);

// --- share.js, as the page imports it ----------------------------------
globalThis.location = { origin: 'https://example.invalid', pathname: '/index.html' };
const { shareLink, readShareLink } = await import(join(web, 'share.js'));

// A small seeded generator (the answers must not change between runs).
let seed = 224;
const random = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
const pick = (list) => list[Math.floor(random() * list.length)];

const TOGGLES = { larc: ['DYN DECAY', 'MODE ENH', 'DECAY OPT'], panel: [], panel224: ['MODE ENH', 'DECAY OPT'] };

// Extra helpFor cases: odd spacing, every suffix shape, headings.
const extraNames = ['LEVEL 1  L)AD', '  DELAY 2   R)B ', 'LEVEL (L)', 'LEVEL L+R)', 'LEVEL L+R)A', 'MID DECAY LR',
  'MID DECAY L', 'MID DECAY R', 'LF DECAY', 'NOTHING KNOWN', 'NOTHING KNOWN L)ABCD', 'L', 'LR', 'X R)E', 'DELAY',
  'LEVEL', 'LEVEL 3', 'DELAY 4  R)', '', '   '];
const extraHeadings = ['', '[     VOICE LEVELS     ]', '[PREECHO DELAYS][FINPDL]', '[ PANS (0=B 99=D)  ]',
  'SLOPE [   PD LEVELS    ]', '[unclosed LEVEL', ']] [NOTE LEVELS] x ( y ) z', '[a (b] c) [BAND DELAYS]',
  '(NOTE PITCHES)', '[BAND LEVELS', 'BAND LEVELS'];

const result = { catalogs: [], helpExtra: [], shareMalformed: [] };
for (const file of readdirSync(join(web, 'catalogs')).filter((f) => f.endsWith('.json')).sort()) {
  const catalog = JSON.parse(readFileSync(join(web, 'catalogs', file), 'utf8'));
  const panel = catalog.remote !== 'larc';
  op = { kind: panel ? 'panel' : 'larc' };
  const rangeMax = panel ? 253 : 254;
  const entry = { file, rom: catalog.rom, remote: catalog.remote, programs: [], help: [], tables: [], shares: [] };
  const seenHelp = new Set();
  for (const p of catalog.programs) {
    const bank = p.bank ?? (p.larc ? p.larc.bank : 0);
    const key = panel ? `x${p.identity.toString(16)}` : `${p.bank}.${p.program}`;
    const where = panel ? p.buttons : `B${p.bank} P${p.program}`;
    const group = panel ? (p.bankName || '') : `${bank} ${p.bankName || ''}`;
    const named = [];
    p.pages.forEach((pg, i) => pg.sliders.forEach((s, slot) => {
      if (s.name === 'INACTIVE' || !s.name) return;
      named.push([i, pg.page, slot]);
    }));
    entry.programs.push({ key, label: `${p.name} (${where})`, group, bank, variations: p.variations,
      pages: p.pages.map((pg) => ({ page: pg.page, heading: pg.heading, column: pg.column ?? -1,
        names: pg.sliders.map((s) => s.name), tableLengths: pg.sliders.map((s) => (s.table || []).length) })),
      named });

    for (const pg of p.pages) {
      for (const s of pg.sliders) {
        const id = `${s.name}\u0000${pg.heading}`;
        if (!seenHelp.has(id)) {
          seenHelp.add(id);
          entry.help.push([s.name, pg.heading, page.helpFor(s.name, pg.heading)]);
        }
        if (s.table && s.table.length && entry.tables.length < 4000 && random() < 0.35) {
          const texts = {};
          for (let raw = 0; raw <= 255; raw += 1 + Math.floor(random() * 5)) texts[raw] = page.tableText(s.table, raw);
          const back = s.table.map(([, text]) => [text, page.tableRaw(s.table, text)]);
          for (const odd of ['', '0', '3.3 SEC', 'x', '99', '-5', '1e2', ' 7.5 ', '.5 SEC'])
            back.push([odd, page.tableRaw(s.table, odd)]);
          entry.tables.push({ table: s.table, texts, back, rangeMax });
        }
      }
    }

    // Share links: presets with some sliders moved, several variations.
    for (let n = 0; n < 6; n++) {
      let variation = pick(p.variations);
      if (n === 5) variation = 9;                                  // (a variation the catalog lacks: preset of 1)
      const preset = p.raw[variation] || p.raw[1];
      const stored = preset.map((row) => row.slice());
      const moves = n === 0 ? 0 : Math.floor(random() * 6);
      for (let k = 0; k < moves && named.length; k++) {
        const [i, , slot] = pick(named);
        stored[i][slot] = Math.floor(random() * 256);
      }
      const toggles = {};
      for (const label of TOGGLES[catalog.remote]) toggles[label] = random() < 0.5;
      const byColumn = new Map(p.pages.map((pg, i) => [pg.column, i]));
      const link = shareLink({ fw: catalog.rom, key, program: p, variation, toggles,
        stored: (column, slot) => stored[byColumn.get(column)][slot] });
      const payload = link.slice(link.indexOf('?') + 1);
      entry.shares.push({ key, variation, stored, toggles: Object.entries(toggles), payload,
        read: readShareLink('?' + payload) });
    }
  }
  result.catalogs.push(entry);
}

for (const name of extraNames) for (const heading of extraHeadings)
  result.helpExtra.push([name, heading, page.helpFor(name, heading)]);

// Links a person might paste or a host might hand back.
for (const text of ['?fw=abc&p=1.1', 'fw=abc&p=1.1', '?fw=abc', '?p=1.1', '?fw=&p=1.1',
  '?fw=eb3a7a765a703ece&p=1.1&v=2&s=1.0.200,2.4.30&t=DYN_DECAY-1,MODE_ENH-0',
  '?fw=eb3a7a765a703ece&p=1.1&v=&s=,1.0.200,,&t=,DYN_DECAY-1,',
  '?fw=a%62c&p=x1&v=%33&t=MODE_ENH-1,MODE_ENH-0,DECAY_OPT',
  '?fw=a+b&p=1.1&v=+4+&s=1.0.0x10,2.1.1e2',
  '?fw=a&p=1.1&v=2&v=3&fw=b',
  '?fw=a&p=1.1&t=A_B_C-1-0,X-',
  'https://host/web/index.html?fw=abc&p=2.3&v=5&s=3.1.17#frag']) {
  // (a whole URL: location.search, the query without the fragment)
  let search = '?' + text;
  if (text.startsWith('?')) search = text;
  if (text.startsWith('http')) search = new URL(text).search;
  const read = readShareLink(search);
  result.shareMalformed.push([text, read]);
}

writeFileSync(out, JSON.stringify(result));
const counts = result.catalogs.map((c) => `${c.rom}: ${c.help.length} names, ${c.shares.length} links, ${c.tables.length} tables`);
console.log(`wrote ${out}\n  ${counts.join('\n  ')}`);
