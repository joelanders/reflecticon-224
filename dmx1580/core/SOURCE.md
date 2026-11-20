# Where the DMX 15-80 core comes from

Copied on 2026-11-12 from the private DMX 15-80 project (`~/code/00-next-verilog-sim-explore/dmx15-80`),
commit `4f7e762`:

| Here | There | Changes |
|---|---|---|
| `core/emu/core/machine.hpp`, `audio_core.hpp`, `sha256.hpp` | `emu/core/` | none |
| `core/emu/i8086.h` | `emu/i8086.h` | none (the project's 8086 interpreter) |
| `core/emu/vendor/i8085/`, `core/emu/vendor/chips/` | `emu/vendor/` | none (floooh chips, zlib; m8080, MIT; their notices included) |
| `core/patches.txt` | `firmware/patches.txt` | none: ROM 3's SHA-256 before and after, and 93 bit masks; no ROM bytes. Compiled in as a string (`gen/dmx1580_patches.h`) |
| `core/engine.hpp` | — | new: the kit's rate bridge, the keypad driver with display read-back, unit swaps for option changes |

**Re-copied 2026-11-18** from commit `918e730` (MB.2 sheet 2: the regen loop as drawn). `machine.hpp` and
`audio_core.hpp` are again byte-identical to the project's. The knobs changed with it:
- Regen A/B (DC loop gain, 0-105 %) became **F/B Gain A/B**, the real pot's rotation (10K log, loaded by
  the sampler's 4K7). Loop gain is 0.29 at half travel and 1 at about 81 %; the readout shows it.
- New **F/B Filter A/B**: a blend with a 7.96 kHz high-pass of the loop signal.
- **Phase A Out** now also inverts A's loop, as the panel switch sits before both.
- New parameter IDs (`fbGainA`, ...), so an old session's regen setting is dropped rather than reinterpreted.
- Checked again: `dx1580_check_machine` all passed, pluginval strictness 10, and the web Node check.

The folder layout keeps the project's relative includes, so the files stay byte-identical and can be
re-copied.

Checked after the copy (2026-11-12, `dx1580_check_machine`):
- a unit booted as the project's plugin engine boots (`juce-plugin/source/engine.hpp`, `Engine::build`),
  then 3 s of the same LCG noise on A and B: output identical byte for byte to that engine built with
  the project's own flags (`c++ -std=c++17 -O2`; the kit builds with `-ffp-contract=off`);
- typed settings read back from the firmware's display: delay and pitch on both channels, clamps to
  the RAM's length, 4K/32K fits, one pitch board (B's pitch is not offered);
- an impulse comes back after the typed delay (250/125 and 1000/500 ms, within 0.03 ms after latency);
- the kit's host checks at 44.1, 48 and 96 kHz; pluginval strictness 10 on the VST3;
- a live delay change shows after 0.88 s of audio, a pitch change after 1.37 s; a RAM change builds
  a new machine with the settings typed in before the swap;
- the cost is 8-11x real time on one thread (M-series Mac), against the RMX16's 32x.

The keypad facts the driver relies on (probed on this core, 2026-11-12) are in the DMX project's note
`notes/2026-11-12-0300-kit-port.md`.
