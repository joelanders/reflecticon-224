// Annotations: hand-written notes on a program (its blocks, named rows and
// diagram layout), found by the program's shape.
//
// The shape is every field of every row in the pass except the ones the
// firmware rewrites while the program runs or when a control moves: the
// coefficients, their signs and the delay offsets. So an annotation follows
// its program through parameter changes and live modulation, but never lands
// on a different program.

const OPS = ['NOP', 'OPER', 'MEMW', 'MEMR'];
const SOURCES = ['none', 'memory', 'RR', 'XREG', 'ADC'];

export function shape(flow) {
  let hash = 2166136261;
  const mix = (value) => {
    hash = Math.imul(hash ^ value, 16777619) >>> 0;
  };
  mix(flow.rows);
  if (flow.model === 1) {
    mix(224);               // an original-224 program never takes a 224X annotation
  }
  for (const m of flow.listing) {
    let flags = 0;
    for (const bit of [m.xfer, m.zero, m.keepShifting, m.reset, m.wrXreg]) {
      flags = flags * 2 + Number(bit);
    }
    mix(OPS.indexOf(m.op));
    mix(m.ra * 4 + m.wa);
    mix(flags);
    mix(SOURCES.indexOf(m.source));
    for (const channel of m.dac) {
      mix(channel.charCodeAt(0));
    }
  }
  return hash.toString(16).padStart(8, '0');
}

// The annotation for this program, or null. `base` is the annotations folder's URL.
export async function findAnnotation(flow, base) {
  let index;
  try {
    const response = await fetch(`${base}/index.json`);
    if (!response.ok) {
      return null;
    }
    index = await response.json();
  } catch {
    return null;
  }
  const file = index[shape(flow)];
  if (!file) {
    return null;
  }
  const response = await fetch(`${base}/${file}`);
  if (!response.ok) {
    return null;
  }
  return response.json();
}
