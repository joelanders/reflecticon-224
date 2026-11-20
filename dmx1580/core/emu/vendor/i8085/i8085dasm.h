#pragma once
/*
    i8085dasm.h: an 8085 disassembler. The 256-entry table is generated with
    the core by codegen/i8085_gen.py, from the same description file, so it
    names each opcode the way the core runs it. Unimplemented opcodes (the
    undocumented 8085 ones) show as "unimplemented XX".

    int i8085dasm_op(uint16_t pc, uint8_t (*read)(uint16_t addr, void* user),
                     void* user, char* out, int out_size)
    writes one instruction's text and returns its length in bytes.
*/
#include <stdint.h>
#include <stdio.h>

typedef struct { const char* text; uint8_t length; } i8085dasm_entry_t;

static const i8085dasm_entry_t i8085dasm_table[256] = {
// <% dasm_table
    { "NOP", 1 },         // 00  NOP
    { "LXI B,", 3 },      // 01  LXI B,d16
    { "STAX B", 1 },      // 02  STAX B
    { "INX B", 1 },       // 03  INX B
    { "INR B", 1 },       // 04  INR B
    { "DCR B", 1 },       // 05  DCR B
    { "MVI B,", 2 },      // 06  MVI B,d8
    { "RLC", 1 },         // 07  RLC
    { "unimplemented 08", 1 },// 08  unimplemented 08
    { "DAD B", 1 },       // 09  DAD B
    { "LDAX B", 1 },      // 0A  LDAX B
    { "DCX B", 1 },       // 0B  DCX B
    { "INR C", 1 },       // 0C  INR C
    { "DCR C", 1 },       // 0D  DCR C
    { "MVI C,", 2 },      // 0E  MVI C,d8
    { "RRC", 1 },         // 0F  RRC
    { "unimplemented 10", 1 },// 10  unimplemented 10
    { "LXI D,", 3 },      // 11  LXI D,d16
    { "STAX D", 1 },      // 12  STAX D
    { "INX D", 1 },       // 13  INX D
    { "INR D", 1 },       // 14  INR D
    { "DCR D", 1 },       // 15  DCR D
    { "MVI D,", 2 },      // 16  MVI D,d8
    { "RAL", 1 },         // 17  RAL
    { "unimplemented 18", 1 },// 18  unimplemented 18
    { "DAD D", 1 },       // 19  DAD D
    { "LDAX D", 1 },      // 1A  LDAX D
    { "DCX D", 1 },       // 1B  DCX D
    { "INR E", 1 },       // 1C  INR E
    { "DCR E", 1 },       // 1D  DCR E
    { "MVI E,", 2 },      // 1E  MVI E,d8
    { "RAR", 1 },         // 1F  RAR
    { "RIM", 1 },         // 20  RIM
    { "LXI H,", 3 },      // 21  LXI H,d16
    { "SHLD ", 3 },       // 22  SHLD a16
    { "INX H", 1 },       // 23  INX H
    { "INR H", 1 },       // 24  INR H
    { "DCR H", 1 },       // 25  DCR H
    { "MVI H,", 2 },      // 26  MVI H,d8
    { "DAA", 1 },         // 27  DAA
    { "unimplemented 28", 1 },// 28  unimplemented 28
    { "DAD H", 1 },       // 29  DAD H
    { "LHLD ", 3 },       // 2A  LHLD a16
    { "DCX H", 1 },       // 2B  DCX H
    { "INR L", 1 },       // 2C  INR L
    { "DCR L", 1 },       // 2D  DCR L
    { "MVI L,", 2 },      // 2E  MVI L,d8
    { "CMA", 1 },         // 2F  CMA
    { "SIM", 1 },         // 30  SIM
    { "LXI SP,", 3 },     // 31  LXI SP,d16
    { "STA ", 3 },        // 32  STA a16
    { "INX SP", 1 },      // 33  INX SP
    { "INR M", 1 },       // 34  INR M
    { "DCR M", 1 },       // 35  DCR M
    { "MVI M,", 2 },      // 36  MVI M,d8
    { "STC", 1 },         // 37  STC
    { "unimplemented 38", 1 },// 38  unimplemented 38
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
    { "unimplemented CB", 1 },// CB  unimplemented CB
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
    { "unimplemented D9", 1 },// D9  unimplemented D9
    { "JC ", 3 },         // DA  JC a16
    { "IN ", 2 },         // DB  IN d8
    { "CC ", 3 },         // DC  CC a16
    { "unimplemented DD", 1 },// DD  unimplemented DD
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
    { "unimplemented ED", 1 },// ED  unimplemented ED
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
    { "unimplemented FD", 1 },// FD  unimplemented FD
    { "CPI ", 2 },        // FE  CPI d8
    { "RST 7", 1 },       // FF  RST 7
// %>
};

static inline int i8085dasm_op(uint16_t pc, uint8_t (*read)(uint16_t, void*), void* user, char* out, int out_size) {
    uint8_t op = read(pc, user);
    const i8085dasm_entry_t* e = &i8085dasm_table[op];
    if (e->length == 1) {
        snprintf(out, (size_t)out_size, "%s", e->text);
    } else if (e->length == 2) {
        snprintf(out, (size_t)out_size, "%s%02Xh", e->text, read((uint16_t)(pc + 1), user));
    } else {
        unsigned v = (unsigned)read((uint16_t)(pc + 1), user) | (unsigned)read((uint16_t)(pc + 2), user) << 8;
        snprintf(out, (size_t)out_size, "%s%04Xh", e->text, v);
    }
    return e->length;
}
