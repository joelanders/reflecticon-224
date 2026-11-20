#!/usr/bin/env python3
"""Turn measure_stored's sweeps into a stored-byte sidecar for one firmware set.

    python3 tools/gen_stored_tables.py CATALOG_JSON SWEEPS... --out catalogs-extra/stored/<hash>.stored.json

CATALOG_JSON is the set's catalog (page/catalogs/<hash>.json or
catalogs-extra/<hash>.json); SWEEPS are the text files tools/measure_stored
printed (one line per program, page, slot and direction). The sidecar says,
for every named slider of every program, what the firmware stores for each
control value (a panel's pot reading 0..253, a LARC fader value 2..254):
measured behaviour, no ROM bytes.

    {
      "version": 1,
      "rom": "<catalog hash>",
      "remote": "panel224" | "panel",
      "layout": "v4" | "v3.2" | "",
      "low": 0 | 2, "high": 253 | 254,         (the control's range: pot readings / LARC faders)
      "made": "<UTC time>",
      "method": "...",
      "tables": [[stored for control value low, ..., high], ...],   (distinct tables)
      "programs": [{"key": "x1" | "1.1", "identity": 1?,
                    "sliders": [[page, slot, table index (moving up), table index (moving down)?], ...],
                    "unmeasurable": [[page, slot, why], ...]?,
                    "variations": {"5": {"sliders": [...], "unmeasurable": [...]}}?}, ...]
                                   (sliders whose tables differ in that variation)
    }

A slider whose up and down sweeps differ (the LARC's delays: the byte a
fader value leaves depends on the direction the fader came from) gets two
tables; one whose stored byte never changes (the LARC's SIZE, kept
elsewhere) is listed as unmeasurable.

Variation 1 gives each slider's tables; another measured variation whose
tables differ (the LARC's PREDELAY clamps per variation) gets an override.

Checks (the tool refuses to write otherwise): every named slider of every
catalog program is measured in variation 1; every table is non-decreasing.
"""
import argparse
import re
import datetime
import json
import sys


def read_sweeps(paths, later_wins=False):
    """{(key, page, slot): {(variation, direction): [values, lowest control value first]}},
    {(key, page, slot): {(variation, direction): {control value: stored}}} (--step sweeps), header"""
    sweeps = {}
    samples = {}
    header = {}
    for path in paths:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                if line.startswith('#'):
                    words = line[1:].split()
                    if len(words) >= 2 and words[0] in ('rom', 'layout'):
                        header.setdefault(words[0], set()).add(words[1])
                    if len(words) >= 4 and words[0] == 'rom' and words[2] == 'remote':
                        header.setdefault('remote', set()).add(words[3])
                    if words and words[0] == 'FAILED:':
                        sys.exit(f'{path}: the sweep failed: {line}')
                    continue
                t = line.split()
                key, variation, page, slot, direction = t[0], int(t[1]), int(t[2]), int(t[3]), t[4]
                if ':' in t[6]:
                    points = dict((int(a), int(b)) for a, b in (item.split(':') for item in t[6:]))
                    samples.setdefault((key, page, slot), {})[(variation, direction)] = points
                    continue
                values = [int(v) for v in t[6:]]
                if direction == 'down':
                    values = values[::-1]
                entry = sweeps.setdefault((key, page, slot), {})
                previous = entry.get((variation, direction))
                if previous is not None and previous != values and not later_wins:
                    sys.exit(f'{path}: {key} v{variation} {page}.{slot} {direction}: two sweeps disagree')
                entry[(variation, direction)] = values
    return sweeps, samples, header


def key_of(program, remote):
    """app.js keyOf"""
    if remote != 'larc' and isinstance(program.get('identity'), int):
        return 'x%x' % program['identity']
    return '%d.%d' % (program['bank'], program['program'])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('catalog')
    parser.add_argument('sweeps', nargs='+')
    parser.add_argument('--out', required=True)
    parser.add_argument('--later-wins', action='store_true',
                        help='a sweep in a later file replaces the same (program, variation, slider, direction) '
                             'in an earlier one (re-measurements with a longer --settle/--still)')
    parser.add_argument('--scratch', default='',
                        help='comma list KEY/PAGE.SLOT of sliders whose record byte was seen changing by itself '
                             '(no function of the control even at a long --still): listed as unmeasurable')
    parser.add_argument('--scratch-name', default='',
                        help='regular expression: sliders whose name matches are scratch too (the LARC\'s SIZE, '
                             'whose record byte the firmware uses while it rebuilds the program: flat or noisy in '
                             'every sweep)')
    parser.add_argument('--note', default='', help='appended to the method text (how the sweeps were made)')
    args = parser.parse_args()

    with open(args.catalog) as f:
        catalog = json.load(f)
    sweeps, samples, header = read_sweeps(args.sweeps, args.later_wins)
    for field, value in (('rom', catalog['rom']), ('remote', catalog.get('remote', ''))):
        seen = header.get(field, set())
        if seen != {value}:
            sys.exit(f'the sweeps are for {field} {sorted(seen)}, the catalog is {value}')

    remote = catalog.get('remote', '')
    low, high = 0, 253
    if remote == 'larc':
        low, high = 2, 254
    count = high - low + 1
    tables = []
    index_of = {}
    programs = []
    problems = []
    variations_seen = set()
    def index(values):
        k = tuple(values)
        if k not in index_of:
            index_of[k] = len(tables)
            tables.append(list(values))
        return index_of[k]

    two_way = 0
    flat = 0
    scratch = set(item.strip() for item in args.scratch.split(',') if item.strip())
    per_variation = 0
    sampled_variations = set()
    checked_points = [0]
    for program in catalog['programs']:
        key = key_of(program, remote)
        sliders = []
        unmeasurable = []
        overrides = {}   # variation -> {'sliders': [...], 'unmeasurable': [...]}
        for page in program['pages']:
            for slot, slider in enumerate(page['sliders']):
                name = slider.get('name', '')
                if not name or name == 'INACTIVE':
                    continue
                where = f'program {key} {page["page"]}.{slot} {name}'
                measured = sweeps.get((key, page['page'], slot))
                if not measured:
                    problems.append(f'{where} ({program["name"]}) not measured')
                    continue
                variations_seen.update(v for v, _ in measured)
                if any(len(values) != count for values in measured.values()):
                    problems.append(f'{where}: a sweep without {count} values')
                    continue
                # What each measured variation stores: ('table', up, down) or ('none', why).
                results = {}
                for v in sorted({v for v, _ in measured}):
                    up = measured.get((v, 'up'))
                    down = measured.get((v, 'down'))
                    if up is None:
                        up = measured.get((1, 'up'))   # (a --down-only re-measurement)
                    if down is None:
                        if v != 1 and (1, 'down') in measured:
                            problems.append(f'{where}: variation {v}: no downward sweep (variation 1 has one)')
                            continue
                        down = up
                    if up is None:
                        problems.append(f'{where}: variation {v}: no upward sweep')
                        continue
                    if f'{key}/{page["page"]}.{slot}' in scratch or (args.scratch_name and
                                                                      re.search(args.scratch_name, name)):
                        results[v] = ('none', 'scratch: the stored byte changes by itself')
                        continue
                    if len(set(up)) == 1 and len(set(down)) == 1:
                        # The stored byte does not follow the control (kept elsewhere, as the
                        # LARC's SIZE): no table; the plugin treats the slider as unmeasured.
                        results[v] = ('none', 'the stored byte does not follow the control')
                        continue
                    for direction, values in (('up', up), ('down', down)):
                        if any(values[r] > values[r + 1] for r in range(count - 1)):
                            bad = [r + low for r in range(count - 1) if values[r] > values[r + 1]]
                            problems.append(f'{where}: variation {v}: the {direction} table is not '
                                            f'non-decreasing (at {bad[:10]})')
                    results[v] = ('table', tuple(up), tuple(down))
                if 1 not in results:
                    problems.append(f'{where}: variation 1 not measured')
                    continue

                def describe(result):
                    if result[0] == 'none':
                        return 'unmeasurable', [page['page'], slot, result[1]]
                    entry = [page['page'], slot, index(result[1])]
                    if result[2] != result[1]:
                        entry.append(index(result[2]))   # direction-dependent: the table moving down
                    return 'sliders', entry

                kind, entry = describe(results[1])
                if kind == 'sliders':
                    sliders.append(entry)
                    if len(entry) > 3:
                        two_way += 1
                else:
                    unmeasurable.append(entry)
                    flat += 1
                # Variations checked with --step sweeps: every sampled value must be what
                # the variation's tables (its own, else variation 1's) say.
                for (v, direction), points in samples.get((key, page['page'], slot), {}).items():
                    expected = results.get(v, results[1])
                    sampled_variations.add(v)
                    checked_points[0] += len(points)
                    if expected[0] != 'table':
                        continue
                    table = expected[1]
                    if direction == 'down':
                        table = expected[2]
                    bad = [(c, b, table[c - low]) for c, b in sorted(points.items()) if table[c - low] != b]
                    if bad:
                        problems.append(f'{where}: variation {v} {direction}: the sampled sweep differs from the '
                                        f'tables (value, measured, table) {bad[:5]}; measure it with --step 1')
                for v, result in results.items():
                    if v != 1 and result != results[1]:
                        kind, entry = describe(result)
                        overrides.setdefault(str(v), {}).setdefault(kind, []).append(entry)
                        per_variation += 1
        entry = {'key': key}
        if 'identity' in program:
            entry['identity'] = program['identity']
        entry['sliders'] = sliders
        if unmeasurable:
            entry['unmeasurable'] = unmeasurable
        if overrides:
            entry['variations'] = {v: overrides[v] for v in sorted(overrides, key=int)}
        programs.append(entry)
    if problems:
        for p in problems:
            print('problem:', p, file=sys.stderr)
        sys.exit(f'{len(problems)} problem(s): nothing written')

    layout = catalog.get('layout', '')
    if catalog.get('remote') == 'panel224' and not layout:
        layout = 'v4'
    out = {
        'version': 1,
        'rom': catalog['rom'],
        'remote': catalog.get('remote', ''),
        'layout': layout,
        'low': low,
        'high': high,
        'made': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
        'method': ('tools/measure_stored: each program loaded on the row machine, each named slider taken over '
                   f'and its control driven over {low}..{high} through the plugin\'s operator (pot readings on the '
                   'panels, fader values on the LARC); the stored byte read from RAM once it held still (--still). '
                   'Directions measured: ' + ', '.join(sorted({d for v in sweeps.values() for _, d in v})) +
                   '; variations measured: ' + ', '.join(str(v) for v in sorted(variations_seen)) +
                   ' (a variation whose tables differ from variation 1 gets an override).' +
                   (f' Checked with --step sweeps: variations {sorted(sampled_variations)}, {checked_points[0]} '
                    f'sampled values, all as the tables say.' if sampled_variations else '') + (' ' + args.note if args.note else '')),
        'tables': tables,
        'programs': programs,
    }
    # One table per line: readable diffs, still compact.
    text = json.dumps({k: v for k, v in out.items() if k not in ('tables', 'programs')}, indent=1)[:-2]
    text += ',\n "tables": [\n' + ',\n'.join('  ' + json.dumps(t, separators=(',', ':')) for t in tables) + '\n ],\n'
    text += ' "programs": [\n' + ',\n'.join('  ' + json.dumps(p, separators=(',', ':')) for p in programs) + '\n ]\n}\n'
    json.loads(text)
    with open(args.out, 'w') as f:
        f.write(text)
    n = sum(len(p['sliders']) for p in programs)
    print(f'{args.out}: {len(programs)} programs, {n} sliders ({two_way} direction-dependent, {flat} unmeasurable, '
          f'{per_variation} per-variation overrides), {len(tables)} distinct tables')


if __name__ == '__main__':
    main()
