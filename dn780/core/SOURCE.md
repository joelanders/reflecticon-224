# Where the DN780 core comes from

Copied on 2026-11-11 from the private `dn780` project (`~/code/00-next-verilog-sim-explore/dn780`),
commit `54d28f3`:

| Here | There | Changes |
|---|---|---|
| `core/dn780.hpp` | `emu/core/dn780.hpp` | z80.h path; the 8279 display writes (`Firmware::display()`), which change no microcode or coefficient |
| `core/engine.hpp` | `emu/core/engine.hpp` | the kit's `RateBridgeT<64>` and `Mailbox` instead of local copies |
| `core/third_party/z80.h` | `emu/fx/third_party/z80.h` | none (floooh, zlib licence) |
| `core/ic44_hypothesis.bin` | `emu/fx/ic44_hypothesis.bin` | none; our own inferred image, also embedded in `dn780_machine.cpp` |

Checked after the copy (2026-11-11): the engine here gives bit-identical output to the original
at 44.1, 48 and 96 kHz on both ROMs, with random block sizes, a live decay change and a program
change. The evidence for what the emulator models, and how it compares with real units, stays in
the dn780 project (`emu/fx/README.md`, `notes/`).

The v2.2 parameter map behind `dn780_machine.cpp` (ranges, display digits, the clamp table at
1CA0, the factory table at 3701) is the dn780 project's `notes/2026-11-10-0244-v22-param-map.md`.
