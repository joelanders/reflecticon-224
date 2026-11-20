# Generates the decoder in ../i8080.h, and the disassembler's table in
# ../i8080dasm.h, from i8080_desc.yml.
#
# Forked from floooh/chips codegen/z80_gen.py (zlib license, see
# ../LICENSE-floooh-chips). Altered: 8080 machine-cycle types instead of the
# Z80's; one generated `case` per clock state, labelled with Intel's own
# M/T state name; no prefix tables; unknown opcodes become stubs.
#
#   python3 i8080_gen.py        (needs PyYAML)
import yaml
import templ

DESC_PATH = 'i8080_desc.yml'
INOUT_PATH = '../i8080.h'
DASM_PATH = '../i8080dasm.h'
FIRST_EXTRA_STEP = 256      # steps 0..255 are the opcodes' M1 T4 states

# Machine-cycle types: the status word each puts out at T1, and its direction.
CYCLE_TYPES = {
    'mread':    ('I8080_CYCLE_MREAD', 'read'),
    'sread':    ('I8080_CYCLE_SREAD', 'read'),
    'ioread':   ('I8080_CYCLE_INPUT', 'read'),
    'mwrite':   ('I8080_CYCLE_MWRITE', 'write'),
    'swrite':   ('I8080_CYCLE_SWRITE', 'write'),
    'iowrite':  ('I8080_CYCLE_OUTPUT', 'write'),
    'internal': (None, 'internal'),
    'halt':     ('I8080_CYCLE_HALT', 'halt'),
}

# Register fields of the opcode (User's Manual p. 2-20: SSS/DDD and rp codes).
r_names   = ['B', 'C', 'D', 'E', 'H', 'L', 'M', 'A']
r_map     = ['cpu->b', 'cpu->c', 'cpu->d', 'cpu->e', 'cpu->h', 'cpu->l', 'XXX', 'cpu->a']
rp_names  = ['B', 'D', 'H', 'SP']
rp_map    = ['cpu->bc', 'cpu->de', 'cpu->hl', 'cpu->sp']
rpl_map   = ['cpu->c', 'cpu->e', 'cpu->l', 'cpu->spl']
rph_map   = ['cpu->b', 'cpu->d', 'cpu->h', 'cpu->sph']
# PUSH/POP use PSW (A and the flags) where the others use SP.
rp2_names = ['B', 'D', 'H', 'PSW']
rp2l_map  = ['cpu->c', 'cpu->e', 'cpu->l', 'cpu->f']
rp2h_map  = ['cpu->b', 'cpu->d', 'cpu->h', 'cpu->a']
# Conditions (p. 2-20 note 17), by the CCC field = y.
cc_names  = ['NZ', 'Z', 'NC', 'C', 'PO', 'PE', 'P', 'M']
cc_map    = ['(!(cpu->f&I8080_ZF))', '(cpu->f&I8080_ZF)', '(!(cpu->f&I8080_CF))', '(cpu->f&I8080_CF)',
             '(!(cpu->f&I8080_PF))', '(cpu->f&I8080_PF)', '(!(cpu->f&I8080_SF))', '(cpu->f&I8080_SF)']
# ALU operations, by y: register/memory form, immediate form, and the code
# that the next fetch's T2 carries out (note 9).
alu_names  = ['ADD', 'ADC', 'SUB', 'SBB', 'ANA', 'XRA', 'ORA', 'CMP']
alui_names = ['ADI', 'ACI', 'SUI', 'SBI', 'ANI', 'XRI', 'ORI', 'CPI']
alu_map    = ['I8080_ALU_ADD', 'I8080_ALU_ADC', 'I8080_ALU_SUB', 'I8080_ALU_SBB',
              'I8080_ALU_ANA', 'I8080_ALU_XRA', 'I8080_ALU_ORA', 'I8080_ALU_CMP']
rot_names  = ['RLC', 'RRC', 'RAL', 'RAR']
rot_map    = ['I8080_ALU_RLC', 'I8080_ALU_RRC', 'I8080_ALU_RAL', 'I8080_ALU_RAR']


class State:
    """One clock state of one instruction: its Intel name and its C code."""
    def __init__(self, label, code, terminal=False):
        self.label = label
        self.code = code
        self.terminal = terminal    # the code already jumps somewhere (e.g. halt)


class OpDesc:
    def __init__(self, name, desc):
        self.name = name
        self.cond = desc.get('cond', 'False')
        self.cond_compiled = compile(self.cond, '<string>', 'eval')
        self.desc = desc


def err(msg):
    raise SystemExit(f'i8080_gen: {msg}')


def fields(opcode):
    # 76 543 210
    # xx yyy zzz,  yyy = ppq
    y = (opcode >> 3) & 7
    return {'op': opcode, 'x': opcode >> 6, 'y': y, 'z': opcode & 7, 'p': y >> 1, 'q': y & 1}


def map_name(s, f):
    # longer placeholders first: $RP2 before $RP, $ALUI before $ALU
    return s\
        .replace('$RY', r_names[f['y']])\
        .replace('$RZ', r_names[f['z']])\
        .replace('$RP2', rp2_names[f['p']])\
        .replace('$RP', rp_names[f['p']])\
        .replace('$CC', cc_names[f['y']])\
        .replace('$ALUI', alui_names[f['y']])\
        .replace('$ALU', alu_names[f['y']])\
        .replace('$ROT', rot_names[f['y'] & 3])\
        .replace('$Y', str(f['y']))


def map_cpu(s, f):
    # longer placeholders first: $RP2L before $RP2 before $RPL before $RP;
    # $PCH/$PCL before $PC; $HL before $H/$L; $ACT/$ALU before $A; $WZ before $W/$Z
    return s\
        .replace('$RP2L', rp2l_map[f['p']])\
        .replace('$RP2H', rp2h_map[f['p']])\
        .replace('$RPL', rpl_map[f['p']])\
        .replace('$RPH', rph_map[f['p']])\
        .replace('$RP', rp_map[f['p']])\
        .replace('$RY', r_map[f['y']])\
        .replace('$RZ', r_map[f['z']])\
        .replace('$CC', cc_map[f['y']])\
        .replace('$ALU', alu_map[f['y']])\
        .replace('$ROT', rot_map[f['y'] & 3])\
        .replace('$Y*8', f"0x{f['y'] * 8:02X}")\
        .replace('$PCH', 'cpu->pch')\
        .replace('$PCL', 'cpu->pcl')\
        .replace('$PC', 'cpu->pc')\
        .replace('$SP', 'cpu->sp')\
        .replace('$HL', 'cpu->hl')\
        .replace('$DE', 'cpu->de')\
        .replace('$WZ', 'cpu->wz')\
        .replace('$W', 'cpu->w')\
        .replace('$Z', 'cpu->z')\
        .replace('$H', 'cpu->h')\
        .replace('$L', 'cpu->l')\
        .replace('$TMP', 'cpu->tmp')\
        .replace('$ACT', 'cpu->act')\
        .replace('$F', 'cpu->f')\
        .replace('$A', 'cpu->a')


def statement(s):
    """An action string as C statements (empty, or ending in ';')."""
    s = (s or '').strip()
    if s and not s.endswith(';') and not s.endswith('}'):
        s += ';'
    return s


def split_address(ab):
    """`$PC++` puts PC out at T1 and increments it at T2 (Table 2-2 shows
    'PC = PC + 1' in T2). Returns (T1 address expression, T2 statement)."""
    for suffix in ('++', '--'):
        if ab.endswith(suffix):
            reg = ab[:-2]
            return reg, f'{reg}{suffix};'
    return ab, ''


def states_for(desc, f):
    """Every clock state after M1 T3, in order."""
    states = [State('M1 T4', statement(map_cpu(desc.get('t4', ''), f)))]
    if 't5' in desc:
        states.append(State('M1 T5', statement(map_cpu(desc['t5'], f))))
    for n, mc in enumerate(desc.get('mcycles', []), start=2):
        kind = mc['type']
        if kind not in CYCLE_TYPES:
            err(f'unknown machine-cycle type {kind}')
        status, direction = CYCLE_TYPES[kind]
        action = statement(map_cpu(mc.get('action', ''), f))
        m = f'M{n}'
        if direction == 'read':
            addr, inc = split_address(map_cpu(mc['ab'], f))
            dst = map_cpu(mc['dst'], f)
            states.append(State(f'{m} T1', f'_t1({addr},{status});'))
            states.append(State(f'{m} T2', f'_dbin();{inc}'))
            states.append(State(f'{m} T3', f'_ready_rd();{dst}=_gd();{action}'))
        elif direction == 'write':
            addr, inc = split_address(map_cpu(mc['ab'], f))
            data = map_cpu(mc['db'], f)
            states.append(State(f'{m} T1', f'_t1({addr},{status});'))
            states.append(State(f'{m} T2', f'_sd({data});{inc}'))
            states.append(State(f'{m} T3', f'_ready_wr();{action}'))
        elif direction == 'internal':
            for t, s in enumerate(mc['states'], start=1):
                states.append(State(f'{m} T{t}', statement(map_cpu(s, f))))
        elif direction == 'halt':
            states.append(State(f'{m} T1', f'_t1(cpu->pc,{status});'))
            states.append(State(f'{m} T2', '_goto(I8080_HALT_TWH1);', terminal=True))
        for t in ('t4', 't5'):
            if t in mc:
                states.append(State(f'{m} {t.upper()}', statement(map_cpu(mc[t], f))))
    return states


def parse_descs():
    with open(DESC_PATH) as fp:
        desc = yaml.safe_load(fp)
    return [OpDesc(name, d) for name, d in desc.items()]


def expand(descs):
    """For each opcode: (display name, states, instruction length in bytes)."""
    ops = []
    for opcode in range(256):
        f = fields(opcode)
        match = None
        for d in descs:
            if eval(d.cond_compiled, {}, f):
                if match:
                    err(f"opcode {opcode:02X} matches both '{match.name}' and '{d.name}'")
                match = d
        if match:
            name = map_name(match.name, f)
            ops.append((name, states_for(match.desc, f), instruction_length(name, match.desc)))
        else:
            ops.append((f'unimplemented {opcode:02X}',
                        [State('M1 T4', '_unimplemented();', terminal=True)], 1))
    return ops


def instruction_length(name, desc):
    """The opcode plus one byte for each machine cycle that reads at PC++;
    the name's operand (d8, or d16/a16) must agree."""
    length = 1 + sum(1 for mc in desc.get('mcycles', []) if mc.get('ab') == '$PC++')
    operand_bytes = {'d8': 1, 'd16': 2, 'a16': 2}
    named = 1 + operand_bytes.get(operand(name), 0)
    if length != named:
        err(f"'{name}' reads {length - 1} operand bytes but names {named - 1}")
    return length


def operand(name):
    """The last word of a display name: 'd8' in 'MVI B,d8'."""
    return name.replace(',', ' ').split()[-1]


def gen_dasm(ops):
    """One table entry per opcode: the text before the operand, and the length."""
    lines = []
    for opcode, (name, _, length) in enumerate(ops):
        text = name
        if length > 1:
            text = name[:len(name) - len(operand(name))]
        lines.append(f'    {{ "{text}", {length} }},{" " * (12 - len(text))}// {opcode:02X}  {name}')
    return '\n'.join(lines)


def gen_decoder(ops):
    main = []
    extra = []
    next_extra = FIRST_EXTRA_STEP
    for opcode, (name, states, _) in enumerate(ops):
        # the first state lives at `case opcode`, the rest in the extra block
        steps = [opcode] + list(range(next_extra, next_extra + len(states) - 1))
        next_extra += len(states) - 1
        for i, st in enumerate(states):
            if st.terminal:
                tail = ''
            elif i + 1 < len(states):
                tail = f'_goto({steps[i + 1]});'
            else:
                tail = '_fetch();'
            line = f'case {steps[i]:4}: {st.code}{tail} // {name} {st.label}'
            (main if i == 0 else extra).append(line)
    indent = ' ' * 8
    lines = [indent + l for l in main + extra]
    return '\n'.join(lines), next_extra


def step_defines(first_free):
    names = ['M1_T1', 'M1_T2', 'M1_T3', 'INTA_T2', 'HALT_TWH1', 'HALT_TWH']
    return '\n'.join(f'#define I8080_{n} {first_free + i}' for i, n in enumerate(names))


def fill(path, regions):
    with open(path) as f:
        lines = f.read().splitlines()
    for name, text in regions.items():
        lines = templ.replace(lines, name, text)
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    ops = expand(parse_descs())
    decoder, first_free = gen_decoder(ops)
    fill(INOUT_PATH, {'decoder': decoder, 'extra_step_defines': step_defines(first_free)})
    fill(DASM_PATH, {'dasm_table': gen_dasm(ops)})
    implemented = sum(1 for name, _, _ in ops if not name.startswith('unimplemented'))
    print(f'i8080_gen: {implemented}/256 opcodes described, {first_free} decoder steps')
