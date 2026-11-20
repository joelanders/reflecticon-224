// The hardware panel on the page: an SVG drawn from the machine's kit::PanelLayout (millimetres;
// kw_panel_layout) and lit from its PanelState (kw_panel), the same data the JUCE editor draws.
// Keys are momentary (pointer down/up; shift-click latches one for chords; computer keys from the
// layout's hotkeys), knobs drag up/down, switches click. Callbacks:
//   key(id, down)          a panel key
//   param(paramId, value)  a knob or switch bound to a parameter (plain value)
//   input(id, value)       a knob or switch that is a panel input
//   getParam(paramId)      the parameter's plain value now; getInput(id) likewise
// The caller passes the parameter table (for ranges).

const NS = 'http://www.w3.org/2000/svg';
const DIAL0 = -10, DIALSPAN = 288;   // degrees: 0 just left of 12 o'clock, 9 at about 10 o'clock
const ALPHA14 = [
  0x0000, 0x0006, 0x0202, 0x12CE, 0x12ED, 0x0C24, 0x2359, 0x0400, 0x2400, 0x0900, 0x3FC0, 0x12C0, 0x0800, 0x00C0, 0x0000, 0x0C00,
  0x0C3F, 0x0006, 0x00DB, 0x008F, 0x00E6, 0x2069, 0x00FD, 0x0007, 0x00FF, 0x00EF, 0x1200, 0x0A00, 0x2400, 0x00C8, 0x0900, 0x1083,
  0x02BB, 0x00F7, 0x128F, 0x0039, 0x120F, 0x00F9, 0x0071, 0x00BD, 0x00F6, 0x1209, 0x001E, 0x2470, 0x0038, 0x0536, 0x2136, 0x003F,
  0x00F3, 0x203F, 0x20F3, 0x00ED, 0x1201, 0x003E, 0x0C30, 0x2836, 0x2D00, 0x1500, 0x0C09, 0x0039, 0x2100, 0x000F, 0x0C03, 0x0008];
const SEG14 = [[1.3, 1, 8.7, 1], [9, 1.4, 9, 6.6], [9, 7.4, 9, 12.6], [1.3, 13, 8.7, 13], [1, 7.4, 1, 12.6], [1, 1.4, 1, 6.6],
  [1.4, 7, 4.6, 7], [5.4, 7, 8.6, 7], [1.6, 1.8, 4.5, 6.3], [5, 1.5, 5, 6.5], [8.4, 1.8, 5.5, 6.3], [4.5, 7.7, 1.6, 12.2],
  [5, 7.5, 5, 12.5], [5.5, 7.7, 8.4, 12.2]];

const hex = (argb, fb) => '#' + ((argb || fb) & 0xffffff).toString(16).padStart(6, '0');
function el(tag, attrs, parent) {
  const e = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (parent) parent.append(e);
  return e;
}

export class Panel {
  constructor(layout, params, cb) {
    this.L = layout;
    this.params = params;
    this.cb = cb;
    this.down = new Set();
    this.latched = new Set();
    this.kbd = new Map();   // KeyboardEvent.code -> key id
    this.leds = [];         // [{e: element, id}]
    this.digits = [];       // [{segs: [paths], id}]
    this.alphas = [];       // [{cells: [[lines]], id, count}]
    this.keys = new Map();  // key id -> group
    this.knobs = [];        // [{g, e, get, set}]
    this.switches = [];
    this.svg = el('svg', { viewBox: `0 0 ${layout.width} ${layout.height}`, class: 'hwpanel', role: 'group', 'aria-label': 'front panel' });
    this.draw();
    this.onKeyDown = (ev) => this.keyboard(ev, true);
    this.onKeyUp = (ev) => this.keyboard(ev, false);
    addEventListener('keydown', this.onKeyDown);
    addEventListener('keyup', this.onKeyUp);
    addEventListener('blur', () => this.releaseAll());
  }

  destroy() {
    this.releaseAll();
    removeEventListener('keydown', this.onKeyDown);
    removeEventListener('keyup', this.onKeyUp);
    this.svg.remove();
  }

  press(id, down) {
    if (down === this.down.has(id)) return;
    if (down) this.down.add(id); else this.down.delete(id);
    this.cb.key(id, down);
    this.keys.get(id)?.classList.toggle('down', down);
  }
  releaseAll() {
    for (const id of [...this.down]) this.press(id, false);
    this.latched.clear();
    this.kbd.clear();
    for (const g of this.keys.values()) g.classList.remove('latched');
  }

  keyboard(ev, down) {
    if (!this.svg.isConnected || this.svg.closest('[hidden]')) return;
    const t = ev.target;
    if (t && (t.tagName === 'INPUT' || t.tagName === 'SELECT' || t.tagName === 'TEXTAREA')) return;
    if (!down) {
      const id = this.kbd.get(ev.code);
      if (id === undefined) return;
      this.kbd.delete(ev.code);
      if (![...this.kbd.values()].includes(id)) this.press(id, false);
      ev.preventDefault();
      return;
    }
    if (ev.metaKey || ev.ctrlKey || ev.altKey) return;
    if (this.kbd.has(ev.code)) { ev.preventDefault(); return; }   // auto-repeat
    const k = ev.key.length === 1 ? ev.key.toLowerCase() : ev.key;
    for (const e of this.L.elements) {
      if (e.k !== 'key' || !e.hotkey) continue;
      const hit = e.hotkey === 'UP' ? k === 'ArrowUp' : e.hotkey === 'DOWN' ? k === 'ArrowDown'
        : [...e.hotkey].some((c) => (c === '\r' ? k === 'Enter' : c === k));
      if (!hit) continue;
      this.kbd.set(ev.code, e.id);
      this.press(e.id, true);
      ev.preventDefault();
      return;
    }
  }

  // cap = cap height (Futura's is 0.76 em); printed legends use the layout's weight, key caps medium
  font(cap, printed = true) { return { 'font-size': (cap / 0.76).toFixed(2), 'white-space': 'pre', 'font-family': "Futura, 'Avenir Next', 'Century Gothic', 'Trebuchet MS', sans-serif", 'font-weight': printed ? (this.L.weight || 500) : 500 }; }

  draw() {
    const L = this.L, s = this.svg, ink = hex(L.ink);
    el('rect', { x: 0, y: 0, width: L.width, height: L.height, rx: 0.8, fill: hex(L.face) }, s);
    const ledInKey = new Set();
    for (const e of L.elements) {
      if (e.k !== 'key') continue;
      for (const l of L.elements) {
        const cx = l.x + l.w / 2, cy = l.y + l.h / 2;
        if (l.k === 'led' && cx > e.x && cx < e.x + e.w && cy > e.y && cy < e.y + e.h) ledInKey.add(l);
      }
    }
    for (const e of L.elements) {
      switch (e.k) {
        case 'hole':
          if (e.style === 0) el('rect', { x: e.x, y: e.y, width: e.w, height: e.h, rx: e.h / 2, fill: '#050505', stroke: '#3a3a3a', 'stroke-width': 0.2 }, s);
          else el('circle', { cx: e.x + e.w / 2, cy: e.y + e.h / 2, r: e.w / 2, fill: '#2a2a2a', stroke: '#111', 'stroke-width': 0.3 }, s);
          break;
        case 'frame':
          if (e.style === 1) el('rect', { x: e.x, y: e.y, width: e.w, height: e.h, rx: 0.5, fill: hex(e.colour, 0xb0b0b0) }, s);
          else el('rect', { x: e.x, y: e.y, width: e.w, height: e.h, rx: 0.4, fill: 'none', stroke: ink, 'stroke-width': 0.32 }, s);
          break;
        case 'path': el('path', { d: e.text, fill: hex(e.colour, L.ink) }, s); break;
        case 'window':
          el('rect', { x: e.x - 0.6, y: e.y - 0.6, width: e.w + 1.2, height: e.h + 1.2, rx: 0.4, fill: '#070303' }, s);
          el('rect', { x: e.x, y: e.y, width: e.w, height: e.h, fill: '#1e0707' }, s);
          break;
        case 'text': {
          const anchor = e.style === 1 ? 'start' : e.style === 2 ? 'end' : 'middle';
          const t = el('text', { x: e.x, y: e.y, fill: hex(e.colour, L.ink), 'text-anchor': anchor, 'dominant-baseline': 'central', ...this.font(e.size || 2.1) }, s);
          t.textContent = e.text;
          break;
        }
        case 'led':
          if (!ledInKey.has(e)) this.leds.push({ e: this.led(s, e), id: e.id });
          break;
        case 'digit': this.digit(s, e); break;
        case 'alpha': this.alpha(s, e); break;
        case 'key': this.key(s, e, L.elements.filter((l) => ledInKey.has(l) && l.x + l.w / 2 > e.x && l.x + l.w / 2 < e.x + e.w && l.y + l.h / 2 > e.y && l.y + l.h / 2 < e.y + e.h)); break;
        case 'pot': this.pot(s, e); break;
        case 'switch': this.toggle(s, e); break;
      }
    }
  }

  led(parent, e) {
    const cx = e.x + e.w / 2, cy = e.y + e.h / 2, r = e.w / 2;
    el('circle', { cx, cy, r: r + 0.35, fill: '#080808' }, parent);
    const g = el('g', { class: 'led', style: `--c:${hex(e.colour, 0xff2a1a)}` }, parent);
    el('circle', { cx, cy, r: r * 1.9, class: 'glow' }, g);
    el('circle', { cx, cy, r, class: 'lens' }, g);
    el('circle', { cx: cx - r * 0.25, cy: cy - r * 0.25, r: r * 0.35, class: 'hi' }, g);
    return g;
  }

  digit(parent, e) {
    const sx = e.w / 12, sy = e.h / 18, t = 1.55, gp = 0.28, slant = 0.12;
    const P = (x, y) => `${(e.x + (x + (18 - y) * slant) * sx).toFixed(3)},${(e.y + y * sy).toFixed(3)}`;
    const h = (x0, x1, y) => `M${P(x0, y)} L${P(x0 + t / 2, y - t / 2)} L${P(x1 - t / 2, y - t / 2)} L${P(x1, y)} L${P(x1 - t / 2, y + t / 2)} L${P(x0 + t / 2, y + t / 2)}Z`;
    const v = (x, y0, y1) => `M${P(x, y0)} L${P(x + t / 2, y0 + t / 2)} L${P(x + t / 2, y1 - t / 2)} L${P(x, y1)} L${P(x - t / 2, y1 - t / 2)} L${P(x - t / 2, y0 + t / 2)}Z`;
    const g = el('g', { class: 'digit', style: `--c:${hex(e.colour, 0xff2a1a)}` }, parent);
    let paths;   // [bit, d]
    if (e.style === 1) paths = [[6, h(0.6, 9.4, 9)], [1, v(5, 3.2, 9 - gp)], [1, v(5, 9 + gp, 14.8)]];
    else {
      const c = P(10.9, 17).split(',').map(Number);
      paths = [[0, h(1 + gp, 9 - gp, 1)], [1, v(9, 1 + gp, 9 - gp)], [2, v(9, 9 + gp, 17 - gp)], [3, h(1 + gp, 9 - gp, 17)],
        [4, v(1, 9 + gp, 17 - gp)], [5, v(1, 1 + gp, 9 - gp)], [6, h(1 + gp, 9 - gp, 9)],
        [7, `M${c[0] - 0.85 * sx},${c[1]} a${0.85 * sx},${0.85 * sx} 0 1,0 ${1.7 * sx},0 a${0.85 * sx},${0.85 * sx} 0 1,0 ${-1.7 * sx},0`]];
    }
    this.digits.push({ id: e.id, segs: paths.map(([bit, d]) => ({ bit, p: el('path', { d }, g) })) });
  }

  alpha(parent, e) {
    const g = el('g', { class: 'alpha', style: `--c:${hex(e.colour, 0xff2a1a)}` }, parent);
    const n = Math.max(1, e.count), cw = e.w / n, cells = [];
    for (let i = 0; i < n; i++) {
      const x0 = e.x + cw * i + cw * 0.12, w = cw * 0.76, sx = w / 10, sy = e.h / 14;
      const P = (x, y) => [x0 + (x + (14 - y) * 0.1) * sx, e.y + y * sy];
      cells.push(SEG14.map(([a, b, c, d]) => {
        const [x1, y1] = P(a, b), [x2, y2] = P(c, d);
        return el('line', { x1, y1, x2, y2, 'stroke-width': Math.max(0.25, sx * 1.05), 'stroke-linecap': 'round' }, g);
      }));
    }
    this.alphas.push({ id: Math.max(0, e.id), cells });
  }

  key(parent, e, leds) {
    const g = el('g', { class: `key k${e.style}`, tabindex: -1 }, parent);
    el('rect', { x: e.x + 0.25, y: e.y + 0.65, width: e.w - 0.5, height: e.h - 0.5, rx: 0.5, class: 'shadow' }, g);
    const body = el('g', { class: 'body' }, g);
    el('rect', { x: e.x + 0.25, y: e.y + 0.25, width: e.w - 0.5, height: e.h - 0.5, rx: 0.5, class: 'cap' }, body);
    if (e.style === 0) el('rect', { x: e.x + 0.25, y: e.y + 0.25, width: e.w - 0.5, height: (e.h - 0.5) * 0.42, rx: 0.5, class: 'slope' }, body);
    if (e.text) {
      const t = el('text', { x: e.x + e.w / 2, y: e.y + e.h / 2 + 0.2, 'text-anchor': 'middle', 'dominant-baseline': 'central', class: 'legend', ...this.font(e.h * 0.35, false) }, body);
      t.textContent = e.text;
    }
    for (const l of leds) this.leds.push({ e: this.led(body, l), id: l.id });
    if (e.hotkey) el('title', {}, g).textContent = 'key: ' + e.hotkey.replace('\r', 'Return');
    g.addEventListener('pointerdown', (ev) => {
      ev.preventDefault();
      if (ev.shiftKey || this.latched.has(e.id)) {
        const on = !this.latched.has(e.id);
        if (on) this.latched.add(e.id); else this.latched.delete(e.id);
        g.classList.toggle('latched', on);
        this.press(e.id, on);
        return;
      }
      g.setPointerCapture(ev.pointerId);
      this.press(e.id, true);
      const up = () => { g.removeEventListener('pointerup', up); g.removeEventListener('pointercancel', up); this.press(e.id, false); };
      g.addEventListener('pointerup', up);
      g.addEventListener('pointercancel', up);
    });
    this.keys.set(e.id, g);
  }

  range(e) {
    if (e.param) {
      const p = this.params.find((q) => q.id === e.param);
      return p ? { min: p.min, max: p.max, def: p.def, name: p.name } : null;
    }
    const i = this.L.inputs.find((q) => q.id === e.id);
    return i ? { min: i.min, max: i.max, def: i.def, name: i.name } : null;
  }
  value(e) { return e.param ? this.cb.getParam(e.param) : this.cb.getInput(e.id); }
  setValue(e, v) { if (e.param) this.cb.param(e.param, v); else this.cb.input(e.id, v); }

  pot(parent, e) {
    const cx = e.x + e.w / 2, cy = e.y + e.h / 2, r = e.w / 2, rg = this.range(e);
    const n = e.text.length, cap = e.size || 2.2, R = r + 0.75 * cap + 0.8;   // size: numeral cap height
    for (let i = 0; i < n; i++) {
      const a = (DIAL0 + DIALSPAN * i / (n - 1)) * Math.PI / 180;
      const t = el('text', { x: cx + Math.sin(a) * R, y: cy - Math.cos(a) * R, fill: hex(this.L.ink), 'text-anchor': 'middle', 'dominant-baseline': 'central', ...this.font(cap) }, parent);
      t.textContent = e.text[i];
    }
    const g = el('g', { class: 'knob' }, parent);
    el('circle', { cx, cy: cy + r * 0.1, r, fill: 'rgba(0,0,0,0.45)' }, g);
    el('circle', { cx, cy, r, fill: 'url(#kitKnobSkirt)', stroke: '#555', 'stroke-width': 0.15 }, g);
    el('circle', { cx, cy, r: r * 0.66, fill: 'url(#kitKnobCap)', stroke: '#58595a', 'stroke-width': 0.2 }, g);
    const ptr = el('line', { x1: cx, y1: cy - r * 0.16, x2: cx, y2: cy - r * 0.66, stroke: '#202020', 'stroke-width': Math.max(0.3, r * 0.08), 'stroke-linecap': 'round' }, g);
    if (rg) el('title', {}, g).textContent = rg.name;
    const show = () => {
      if (!rg) return;
      const f = (this.value(e) - rg.min) / (rg.max - rg.min || 1);
      ptr.setAttribute('transform', `rotate(${DIAL0 + DIALSPAN * Math.min(1, Math.max(0, f))} ${cx} ${cy})`);
    };
    g.addEventListener('pointerdown', (ev) => {
      if (!rg) return;
      ev.preventDefault();
      g.setPointerCapture(ev.pointerId);
      const y0 = ev.clientY, v0 = this.value(e);
      const move = (m) => {
        const v = Math.min(rg.max, Math.max(rg.min, v0 + (y0 - m.clientY) / 220 * (rg.max - rg.min) * (m.shiftKey ? 0.2 : 1)));
        this.setValue(e, v);
        show();
      };
      const up = () => { g.removeEventListener('pointermove', move); g.removeEventListener('pointerup', up); };
      g.addEventListener('pointermove', move);
      g.addEventListener('pointerup', up);
    });
    g.addEventListener('dblclick', () => { if (rg) { this.setValue(e, rg.def); show(); } });
    this.knobs.push(show);
    show();
  }

  toggle(parent, e) {
    const cx = e.x + e.w / 2, cy = e.y + e.h / 2;
    const g = el('g', { class: 'switch' + (e.param || e.id >= 0 ? ' live' : '') }, parent);
    // sized from the element, as the JUCE editor draws it
    const len = e.h * 0.4, wid = e.w * 0.24;
    el('circle', { cx, cy, r: e.w * 0.3, fill: 'url(#kitNut)' }, g);
    el('circle', { cx, cy, r: e.w * 0.185, fill: '#3a3a38' }, g);
    const lever = el('rect', { x: cx - wid / 2, width: wid, height: len, rx: wid * 0.45, fill: 'url(#kitLever)' }, g);
    const tip = el('circle', { cx, r: wid * 0.625, fill: '#f8f8f6' }, g);
    const rg = this.range(e);
    if (rg) el('title', {}, g).textContent = rg.name;
    const show = () => {
      const on = (e.param || e.id >= 0) ? this.value(e) > 0.5 : e.style === 1;
      const upPos = on === (e.style === 1);
      lever.setAttribute('y', upPos ? cy - len * 0.95 : cy - len * 0.05);
      tip.setAttribute('cy', upPos ? cy - len * 0.95 + wid / 2 : cy + len * 0.95 - wid / 2);
    };
    g.addEventListener('click', () => {
      if (!(e.param || e.id >= 0)) return;
      this.setValue(e, this.value(e) > 0.5 ? 0 : 1);
      show();
    });
    this.switches.push(show);
    show();
  }

  // a parameter or input changed elsewhere (easy mode, adopt, presets)
  sync() { for (const f of this.knobs) f(); for (const f of this.switches) f(); }

  update(st) {
    const valid = !!st.valid;
    for (const { e, id } of this.leds) {
      const lv = valid && id >= 0 && st.leds && id < st.leds.length ? st.leds[id] / 255 : 0;
      e.style.setProperty('--lv', lv);
      e.classList.toggle('on', lv > 0);
    }
    for (const d of this.digits) {
      const v = valid && st.digits ? st.digits[d.id] || 0 : 0;
      for (const s of d.segs) s.p.classList.toggle('on', !!(v >> s.bit & 1));
    }
    for (const a of this.alphas) {
      a.cells.forEach((cell, i) => {
        let ch = valid && st.alpha ? st.alpha.charCodeAt(a.id + i) || 32 : 32;
        if (ch >= 97 && ch <= 122) ch -= 32;
        const bits = ch >= 32 && ch < 96 ? ALPHA14[ch - 32] : 0;
        cell.forEach((ln, k) => ln.classList.toggle('on', !!(bits >> k & 1)));
      });
    }
    const held = new Set(st.held || []);
    for (const [id, g] of this.keys) g.classList.toggle('driver', held.has(id));
  }
}

// gradients shared by every panel (once per page)
export function panelDefs() {
  if (document.getElementById('kitPanelDefs')) return;
  const svg = el('svg', { id: 'kitPanelDefs', width: 0, height: 0, style: 'position:absolute' });
  const defs = el('defs', {}, svg);
  const rg = el('radialGradient', { id: 'kitKnobSkirt', cx: '35%', cy: '30%', r: '80%' }, defs);
  el('stop', { offset: '0%', 'stop-color': '#e6e6e2' }, rg);
  el('stop', { offset: '100%', 'stop-color': '#6c6d6e' }, rg);
  const cg = el('linearGradient', { id: 'kitKnobCap', x1: 0, y1: 0, x2: 1, y2: 1 }, defs);
  el('stop', { offset: '0%', 'stop-color': '#f4f4f2' }, cg);
  el('stop', { offset: '100%', 'stop-color': '#9a9b9c' }, cg);
  const ng = el('linearGradient', { id: 'kitNut', x1: 0, y1: 0, x2: 1, y2: 1 }, defs);
  el('stop', { offset: '0%', 'stop-color': '#c8c8c4' }, ng);
  el('stop', { offset: '100%', 'stop-color': '#5a5a58' }, ng);
  const lg = el('linearGradient', { id: 'kitLever', x1: 0, y1: 0, x2: 1, y2: 0 }, defs);
  el('stop', { offset: '0%', 'stop-color': '#f0f0ee' }, lg);
  el('stop', { offset: '100%', 'stop-color': '#7a7a78' }, lg);
  document.body.prepend(svg);
}
