// Share links: the page's settings in a URL query, for any of the three
// machines. A link names the firmware only by its hash (the visitor supplies
// their own chips), then the program, the variation, the sliders that differ
// from that variation's preset (their exact stored bytes, read from the
// firmware's RAM) and the toggles:
//
//   ?fw=eb3a7a765a703ece&p=1.1&v=2&s=1.0.200,2.4.30&t=DYN_DECAY-1,MODE_ENH-0
//
// Opening one replays it through the easy UI's operator, as a user would.

// The link for the current state. `program` is the catalog entry, `stored`
// reads a parameter's byte by record column and slot, `toggles` is
// {label: on} (may be empty).
export function shareLink({ fw, key, program, variation, stored, toggles }) {
  // (every value is digits, letters and . , _ - : nothing needs escaping)
  const params = [`fw=${fw}`, `p=${key}`, `v=${variation}`];
  const preset = program.raw && (program.raw[variation] || program.raw[1]);
  const moved = [];
  program.pages.forEach((page, i) => page.sliders.forEach((slider, slot) => {
    if (slider.name === 'INACTIVE' || !slider.name || page.column === undefined) return;
    const value = stored(page.column, slot);
    if (!preset || value !== preset[i][slot]) moved.push(`${page.page}.${slot}.${value}`);
  }));
  if (moved.length) params.push(`s=${moved.join(',')}`);
  const flags = Object.entries(toggles).map(([label, on]) => `${label.replace(/ /g, '_')}-${on ? 1 : 0}`);
  if (flags.length) params.push(`t=${flags.join(',')}`);
  return `${location.origin}${location.pathname}?${params.join('&')}`;
}

// The settings a link asks for, or null if this URL is not a share link.
export function readShareLink(search) {
  const params = new URLSearchParams(search);
  if (!params.get('fw') || !params.get('p')) return null;
  const moves = (params.get('s') || '').split(',').filter(Boolean).map((item) => {
    const [page, slot, value] = item.split('.').map(Number);
    return { page, slot, value };
  });
  const toggles = {};
  for (const item of (params.get('t') || '').split(',').filter(Boolean)) {
    const [label, on] = item.split('-');
    toggles[label.replace(/_/g, ' ')] = on === '1';
  }
  return { fw: params.get('fw'), key: params.get('p'), variation: Number(params.get('v') || 1), moves, toggles };
}

// Put the machine in the link's state. `select(entry)` loads a program.
export async function applyShareLink(link, { catalog, keyOf, select, op }) {
  const entry = catalog.find((e) => keyOf(e) === link.key);
  if (!entry) throw new Error(`the link's program ${link.key} is not in this firmware`);
  await select(entry);
  if (link.variation !== 1) await op.loadVariation(link.variation);
  for (const { page, slot, value } of link.moves) await op.moveSlider(page, slot, value);
  if (op.readToggles) {
    const now = await op.readToggles();
    for (const [label, on] of Object.entries(link.toggles)) {
      if (label in now && now[label] !== on) await op.setToggle(label, on);
    }
  }
}
