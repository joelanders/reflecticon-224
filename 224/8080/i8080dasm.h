#pragma once
/*
    i8080dasm.h -- an 8080 disassembler.

    Its table is generated with i8080.h's decoder, from the same
    descriptions (codegen/i8080_desc.yml), so each opcode is named the way
    the core runs it, undocumented aliases included (08h is NOP, CBh is
    JMP, D9h is RET, DDh/EDh/FDh are CALL). An instruction's length is the
    opcode plus one byte for each machine cycle that reads at PC++, which is
    what the core fetches; the generator checks it against the operand in
    the name (d8: one byte, d16 or a16: two).

    Operands are printed as bare upper-case hex: MVI A,5A  STA 4123  RST 7.

    Header-only: the functions are static inline, so include it anywhere.
*/
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *text;   // the mnemonic and fixed operands, up to the variable operand
    uint8_t length;     // 1, 2 (a byte operand) or 3 (a word operand)
} i8080dasm_op_t;

static const i8080dasm_op_t i8080dasm_ops[256] = {
// <% dasm_table
    { "NOP", 1 },         // 00  NOP
    { "LXI B,", 3 },      // 01  LXI B,d16
    { "STAX B", 1 },      // 02  STAX B
    { "INX B", 1 },       // 03  INX B
    { "INR B", 1 },       // 04  INR B
    { "DCR B", 1 },       // 05  DCR B
    { "MVI B,", 2 },      // 06  MVI B,d8
    { "RLC", 1 },         // 07  RLC
    { "NOP", 1 },         // 08  NOP
    { "DAD B", 1 },       // 09  DAD B
    { "LDAX B", 1 },      // 0A  LDAX B
    { "DCX B", 1 },       // 0B  DCX B
    { "INR C", 1 },       // 0C  INR C
    { "DCR C", 1 },       // 0D  DCR C
    { "MVI C,", 2 },      // 0E  MVI C,d8
    { "RRC", 1 },         // 0F  RRC
    { "NOP", 1 },         // 10  NOP
    { "LXI D,", 3 },      // 11  LXI D,d16
    { "STAX D", 1 },      // 12  STAX D
    { "INX D", 1 },       // 13  INX D
    { "INR D", 1 },       // 14  INR D
    { "DCR D", 1 },       // 15  DCR D
    { "MVI D,", 2 },      // 16  MVI D,d8
    { "RAL", 1 },         // 17  RAL
    { "NOP", 1 },         // 18  NOP
    { "DAD D", 1 },       // 19  DAD D
    { "LDAX D", 1 },      // 1A  LDAX D
    { "DCX D", 1 },       // 1B  DCX D
    { "INR E", 1 },       // 1C  INR E
    { "DCR E", 1 },       // 1D  DCR E
    { "MVI E,", 2 },      // 1E  MVI E,d8
    { "RAR", 1 },         // 1F  RAR
    { "NOP", 1 },         // 20  NOP
    { "LXI H,", 3 },      // 21  LXI H,d16
    { "SHLD ", 3 },       // 22  SHLD a16
    { "INX H", 1 },       // 23  INX H
    { "INR H", 1 },       // 24  INR H
    { "DCR H", 1 },       // 25  DCR H
    { "MVI H,", 2 },      // 26  MVI H,d8
    { "DAA", 1 },         // 27  DAA
    { "NOP", 1 },         // 28  NOP
    { "DAD H", 1 },       // 29  DAD H
    { "LHLD ", 3 },       // 2A  LHLD a16
    { "DCX H", 1 },       // 2B  DCX H
    { "INR L", 1 },       // 2C  INR L
    { "DCR L", 1 },       // 2D  DCR L
    { "MVI L,", 2 },      // 2E  MVI L,d8
    { "CMA", 1 },         // 2F  CMA
    { "NOP", 1 },         // 30  NOP
    { "LXI SP,", 3 },     // 31  LXI SP,d16
    { "STA ", 3 },        // 32  STA a16
    { "INX SP", 1 },      // 33  INX SP
    { "INR M", 1 },       // 34  INR M
    { "DCR M", 1 },       // 35  DCR M
    { "MVI M,", 2 },      // 36  MVI M,d8
    { "STC", 1 },         // 37  STC
    { "NOP", 1 },         // 38  NOP
    { "DAD SP", 1 },      // 39  DAD SP
    { "LDA ", 3 },        // 3A  LDA a16
    { "DCX SP", 1 },      // 3B  DCX SP
    { "INR A", 1 },       // 3C  INR A
    { "DCR A", 1 },       // 3D  DCR A
    { "MVI A,", 2 },      // 3E  MVI A,d8
    { "CMC", 1 },         // 3F  CMC
    { "MOV B,B", 1 },     // 40  MOV B,B
    { "MOV B,C", 1 },     // 41  MOV B,C
    { "MOV B,D", 1 },     // 42  MOV B,D
    { "MOV B,E", 1 },     // 43  MOV B,E
    { "MOV B,H", 1 },     // 44  MOV B,H
    { "MOV B,L", 1 },     // 45  MOV B,L
    { "MOV B,M", 1 },     // 46  MOV B,M
    { "MOV B,A", 1 },     // 47  MOV B,A
    { "MOV C,B", 1 },     // 48  MOV C,B
    { "MOV C,C", 1 },     // 49  MOV C,C
    { "MOV C,D", 1 },     // 4A  MOV C,D
    { "MOV C,E", 1 },     // 4B  MOV C,E
    { "MOV C,H", 1 },     // 4C  MOV C,H
    { "MOV C,L", 1 },     // 4D  MOV C,L
    { "MOV C,M", 1 },     // 4E  MOV C,M
    { "MOV C,A", 1 },     // 4F  MOV C,A
    { "MOV D,B", 1 },     // 50  MOV D,B
    { "MOV D,C", 1 },     // 51  MOV D,C
    { "MOV D,D", 1 },     // 52  MOV D,D
    { "MOV D,E", 1 },     // 53  MOV D,E
    { "MOV D,H", 1 },     // 54  MOV D,H
    { "MOV D,L", 1 },     // 55  MOV D,L
    { "MOV D,M", 1 },     // 56  MOV D,M
    { "MOV D,A", 1 },     // 57  MOV D,A
    { "MOV E,B", 1 },     // 58  MOV E,B
    { "MOV E,C", 1 },     // 59  MOV E,C
    { "MOV E,D", 1 },     // 5A  MOV E,D
    { "MOV E,E", 1 },     // 5B  MOV E,E
    { "MOV E,H", 1 },     // 5C  MOV E,H
    { "MOV E,L", 1 },     // 5D  MOV E,L
    { "MOV E,M", 1 },     // 5E  MOV E,M
    { "MOV E,A", 1 },     // 5F  MOV E,A
    { "MOV H,B", 1 },     // 60  MOV H,B
    { "MOV H,C", 1 },     // 61  MOV H,C
    { "MOV H,D", 1 },     // 62  MOV H,D
    { "MOV H,E", 1 },     // 63  MOV H,E
    { "MOV H,H", 1 },     // 64  MOV H,H
    { "MOV H,L", 1 },     // 65  MOV H,L
    { "MOV H,M", 1 },     // 66  MOV H,M
    { "MOV H,A", 1 },     // 67  MOV H,A
    { "MOV L,B", 1 },     // 68  MOV L,B
    { "MOV L,C", 1 },     // 69  MOV L,C
    { "MOV L,D", 1 },     // 6A  MOV L,D
    { "MOV L,E", 1 },     // 6B  MOV L,E
    { "MOV L,H", 1 },     // 6C  MOV L,H
    { "MOV L,L", 1 },     // 6D  MOV L,L
    { "MOV L,M", 1 },     // 6E  MOV L,M
    { "MOV L,A", 1 },     // 6F  MOV L,A
    { "MOV M,B", 1 },     // 70  MOV M,B
    { "MOV M,C", 1 },     // 71  MOV M,C
    { "MOV M,D", 1 },     // 72  MOV M,D
    { "MOV M,E", 1 },     // 73  MOV M,E
    { "MOV M,H", 1 },     // 74  MOV M,H
    { "MOV M,L", 1 },     // 75  MOV M,L
    { "HLT", 1 },         // 76  HLT
    { "MOV M,A", 1 },     // 77  MOV M,A
    { "MOV A,B", 1 },     // 78  MOV A,B
    { "MOV A,C", 1 },     // 79  MOV A,C
    { "MOV A,D", 1 },     // 7A  MOV A,D
    { "MOV A,E", 1 },     // 7B  MOV A,E
    { "MOV A,H", 1 },     // 7C  MOV A,H
    { "MOV A,L", 1 },     // 7D  MOV A,L
    { "MOV A,M", 1 },     // 7E  MOV A,M
    { "MOV A,A", 1 },     // 7F  MOV A,A
    { "ADD B", 1 },       // 80  ADD B
    { "ADD C", 1 },       // 81  ADD C
    { "ADD D", 1 },       // 82  ADD D
    { "ADD E", 1 },       // 83  ADD E
    { "ADD H", 1 },       // 84  ADD H
    { "ADD L", 1 },       // 85  ADD L
    { "ADD M", 1 },       // 86  ADD M
    { "ADD A", 1 },       // 87  ADD A
    { "ADC B", 1 },       // 88  ADC B
    { "ADC C", 1 },       // 89  ADC C
    { "ADC D", 1 },       // 8A  ADC D
    { "ADC E", 1 },       // 8B  ADC E
    { "ADC H", 1 },       // 8C  ADC H
    { "ADC L", 1 },       // 8D  ADC L
    { "ADC M", 1 },       // 8E  ADC M
    { "ADC A", 1 },       // 8F  ADC A
    { "SUB B", 1 },       // 90  SUB B
    { "SUB C", 1 },       // 91  SUB C
    { "SUB D", 1 },       // 92  SUB D
    { "SUB E", 1 },       // 93  SUB E
    { "SUB H", 1 },       // 94  SUB H
    { "SUB L", 1 },       // 95  SUB L
    { "SUB M", 1 },       // 96  SUB M
    { "SUB A", 1 },       // 97  SUB A
    { "SBB B", 1 },       // 98  SBB B
    { "SBB C", 1 },       // 99  SBB C
    { "SBB D", 1 },       // 9A  SBB D
    { "SBB E", 1 },       // 9B  SBB E
    { "SBB H", 1 },       // 9C  SBB H
    { "SBB L", 1 },       // 9D  SBB L
    { "SBB M", 1 },       // 9E  SBB M
    { "SBB A", 1 },       // 9F  SBB A
    { "ANA B", 1 },       // A0  ANA B
    { "ANA C", 1 },       // A1  ANA C
    { "ANA D", 1 },       // A2  ANA D
    { "ANA E", 1 },       // A3  ANA E
    { "ANA H", 1 },       // A4  ANA H
    { "ANA L", 1 },       // A5  ANA L
    { "ANA M", 1 },       // A6  ANA M
    { "ANA A", 1 },       // A7  ANA A
    { "XRA B", 1 },       // A8  XRA B
    { "XRA C", 1 },       // A9  XRA C
    { "XRA D", 1 },       // AA  XRA D
    { "XRA E", 1 },       // AB  XRA E
    { "XRA H", 1 },       // AC  XRA H
    { "XRA L", 1 },       // AD  XRA L
    { "XRA M", 1 },       // AE  XRA M
    { "XRA A", 1 },       // AF  XRA A
    { "ORA B", 1 },       // B0  ORA B
    { "ORA C", 1 },       // B1  ORA C
    { "ORA D", 1 },       // B2  ORA D
    { "ORA E", 1 },       // B3  ORA E
    { "ORA H", 1 },       // B4  ORA H
    { "ORA L", 1 },       // B5  ORA L
    { "ORA M", 1 },       // B6  ORA M
    { "ORA A", 1 },       // B7  ORA A
    { "CMP B", 1 },       // B8  CMP B
    { "CMP C", 1 },       // B9  CMP C
    { "CMP D", 1 },       // BA  CMP D
    { "CMP E", 1 },       // BB  CMP E
    { "CMP H", 1 },       // BC  CMP H
    { "CMP L", 1 },       // BD  CMP L
    { "CMP M", 1 },       // BE  CMP M
    { "CMP A", 1 },       // BF  CMP A
    { "RNZ", 1 },         // C0  RNZ
    { "POP B", 1 },       // C1  POP B
    { "JNZ ", 3 },        // C2  JNZ a16
    { "JMP ", 3 },        // C3  JMP a16
    { "CNZ ", 3 },        // C4  CNZ a16
    { "PUSH B", 1 },      // C5  PUSH B
    { "ADI ", 2 },        // C6  ADI d8
    { "RST 0", 1 },       // C7  RST 0
    { "RZ", 1 },          // C8  RZ
    { "RET", 1 },         // C9  RET
    { "JZ ", 3 },         // CA  JZ a16
    { "JMP ", 3 },        // CB  JMP a16
    { "CZ ", 3 },         // CC  CZ a16
    { "CALL ", 3 },       // CD  CALL a16
    { "ACI ", 2 },        // CE  ACI d8
    { "RST 1", 1 },       // CF  RST 1
    { "RNC", 1 },         // D0  RNC
    { "POP D", 1 },       // D1  POP D
    { "JNC ", 3 },        // D2  JNC a16
    { "OUT ", 2 },        // D3  OUT d8
    { "CNC ", 3 },        // D4  CNC a16
    { "PUSH D", 1 },      // D5  PUSH D
    { "SUI ", 2 },        // D6  SUI d8
    { "RST 2", 1 },       // D7  RST 2
    { "RC", 1 },          // D8  RC
    { "RET", 1 },         // D9  RET
    { "JC ", 3 },         // DA  JC a16
    { "IN ", 2 },         // DB  IN d8
    { "CC ", 3 },         // DC  CC a16
    { "CALL ", 3 },       // DD  CALL a16
    { "SBI ", 2 },        // DE  SBI d8
    { "RST 3", 1 },       // DF  RST 3
    { "RPO", 1 },         // E0  RPO
    { "POP H", 1 },       // E1  POP H
    { "JPO ", 3 },        // E2  JPO a16
    { "XTHL", 1 },        // E3  XTHL
    { "CPO ", 3 },        // E4  CPO a16
    { "PUSH H", 1 },      // E5  PUSH H
    { "ANI ", 2 },        // E6  ANI d8
    { "RST 4", 1 },       // E7  RST 4
    { "RPE", 1 },         // E8  RPE
    { "PCHL", 1 },        // E9  PCHL
    { "JPE ", 3 },        // EA  JPE a16
    { "XCHG", 1 },        // EB  XCHG
    { "CPE ", 3 },        // EC  CPE a16
    { "CALL ", 3 },       // ED  CALL a16
    { "XRI ", 2 },        // EE  XRI d8
    { "RST 5", 1 },       // EF  RST 5
    { "RP", 1 },          // F0  RP
    { "POP PSW", 1 },     // F1  POP PSW
    { "JP ", 3 },         // F2  JP a16
    { "DI", 1 },          // F3  DI
    { "CP ", 3 },         // F4  CP a16
    { "PUSH PSW", 1 },    // F5  PUSH PSW
    { "ORI ", 2 },        // F6  ORI d8
    { "RST 6", 1 },       // F7  RST 6
    { "RM", 1 },          // F8  RM
    { "SPHL", 1 },        // F9  SPHL
    { "JM ", 3 },         // FA  JM a16
    { "EI", 1 },          // FB  EI
    { "CM ", 3 },         // FC  CM a16
    { "CALL ", 3 },       // FD  CALL a16
    { "CPI ", 2 },        // FE  CPI d8
    { "RST 7", 1 },       // FF  RST 7
// %>
};

// The length of the instruction whose opcode is `opcode`, in bytes.
static inline int i8080dasm_length(uint8_t opcode) {
    return i8080dasm_ops[opcode].length;
}

// Disassemble the instruction in `bytes` (the opcode and the two bytes that
// follow it; the second and third are read only if the instruction has
// them). Writes the text to `out` and returns the length in bytes.
static inline int i8080dasm(const uint8_t bytes[3], char *out, size_t size) {
    const i8080dasm_op_t *op = &i8080dasm_ops[bytes[0]];
    if (op->length == 1) {
        snprintf(out, size, "%s", op->text);
    } else if (op->length == 2) {
        snprintf(out, size, "%s%02X", op->text, (unsigned)bytes[1]);
    } else {
        snprintf(out, size, "%s%04X", op->text, (unsigned)(bytes[1] | bytes[2] << 8));
    }
    return op->length;
}

#ifdef __cplusplus
}
#endif

/*
    This file is an altered version of floooh/chips' code-generated CPU
    headers (https://github.com/floooh/chips): a new 8080 decoder generated
    by a fork of codegen/z80_gen.py. The notice below is floooh/chips'
    license, kept as its clause 3 requires.

    zlib/libpng license

    Copyright (c) 2018 Andre Weissflog
    This software is provided 'as-is', without any express or implied warranty.
    In no event will the authors be held liable for any damages arising from the
    use of this software.
    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:
        1. The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software in a
        product, an acknowledgment in the product documentation would be
        appreciated but is not required.
        2. Altered source versions must be plainly marked as such, and must not
        be misrepresented as being the original software.
        3. This notice may not be removed or altered from any source
        distribution.
*/
