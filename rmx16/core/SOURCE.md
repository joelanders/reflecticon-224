# Where the RMX16 core comes from

Copied on 2026-11-12 from the private RMX16 project (`~/code/00-ams-dmx`), commit `c32dfad`
(which includes the step-15 address fix, `5c9818c`):

| Here | There | Changes |
|---|---|---|
| `core/host/machine.h`, `frontpanel.h`, `usart.h`, `dsp.h` | `emu/host/` | none |
| `core/i8085-cycle/i8085.h` + `LICENSE-*` | `emu/i8085-cycle/` | none (our cycle-stepped 8085; floooh zlib and m8080 MIT notices) |
| `core/vendor/chips/i8255.h` + `LICENSE` | `emu/vendor/chips/` | none (floooh, zlib licence) |
| `core/rmx16_core.c` / `.h` | `emu/web/web.c` (the same role) | new: a C API so the C++ side never includes the C headers |
| `core/engine.hpp` | — | new: the kit's rate bridge, the key driver, the display read-back |

The folder layout is the project's, so the C headers stay byte-identical and can be re-copied.

Checked after the copy (2026-11-12, `rmx16_check_machine`):
- through the C API, 4 s with an impulse at 3.0 s on program 1 gives DAC A/B identical byte for byte
  to the project's `rmx16 firmware 4 --impulse 3.0 --wav-out`;
- typed settings read back from the firmware's display on all 17 programs (19 cases, including
  clamped values);
- the kit's host checks at 44.1, 48 and 96 kHz;
- a live decay change shows after 0.54 s of audio, and a program change with its settings
  retyped after 1.56 s;
- the cost is 32x real time on one thread (M-series Mac).

What the emulator models and how it compares with a real unit stays in the RMX16 project:
- `notes/opus-5.5/2026-11-12-0115-session-17-programs-filters-freeze-step15.md` (latest state);
- `2026-11-11-2016-first-real-unit-check-youtube-shootout.md`;
- `2026-11-12-0036-programs-10-17.md`.

The parameter table in `rmx16_machine.cpp` comes from the 2026-11-12 measurements: predelay
maxima by keypad search, which programs have LF/HF, and the factory values.
