# Shared code

The plugin and web framework every machine folder (`rmx16/`, `dmx1580/`, `dn780/`, `sg323/`) is
built on. Imported 2026-11-20 from the private gear-plugins repo, where it was
`shared/plugin-kit/`; the code still calls itself the kit (`kit::`, `kit_*.cpp`,
`add_machine_plugin`). Their plugin names and codes are listed in the top-level README. The 224's own plugin (`224/juce-plugin/`) is not built on this yet.

Bring your own ROMs: no ROM or firmware bytes are in this repository or in the binaries. The
plugins read the user's files at run time and remember only their paths.

## Layout

```
shared/
  core/    JUCE-free: Machine interface, Host (thread hand-offs), rate bridge + resampler,
           mailbox, ROM recognition (SHA-256 / file name)
  juce/    kit::Processor and kit::Editor (generic, built from the machine's parameter table),
           kit.cmake (add_machine_plugin), ad-hoc signing, the editor-shot tool
  tests/   host_checks.hpp: checks every machine must pass
<name>/                  one folder per machine, at the repository root
  CMakeLists.txt         the plugin and the check tool
  core/                  the emulator (copied from its research project; core/SOURCE.md says from where)
  plugin/<name>_machine.cpp  the kit::MachineType: parameters, ROM table, boot, readouts, presets
  plugin/<name>_panel.hpp    the hardware panel layout, if it has one
  plugin/<name>_plugin.cpp   KIT_DEFINE_PLUGIN(...)
  tests/check_machine.cpp
```

## Adding a machine

1. Put the emulator's core under `<name>/core/`.
2. Write a `kit::Machine` (prepare, set_params, process, latency; optionally readouts, presets,
   state) and a `kit::MachineType` (parameter table, ROM specs, boot function).
3. `add_machine_plugin(...)` in `<name>/CMakeLists.txt`, and `add_subdirectory(<name>)` in the root `CMakeLists.txt`.
4. A test that runs `kit_test::run_host_checks` plus the machine's own checks.

The kit does the rest: host parameters, the worker thread that applies them, machine swaps
without locking the audio thread, mono/stereo input, latency reporting, finding and remembering
ROMs, saved state, and the editor.

## Building

```
cmake -S . -B build -G Ninja   # from the repository root -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
cmake --build build
build/dn780/dn780_check_machine <folder with the v2.2 EPROM image>
build/rmx16/rmx16_check_machine <folder with R316 and IC1-IC4 of v3.2>
build/dmx1580/dmx1580_check_machine <folder with DMX23-3 and DMX-10-SHL-406-1/2/3>
build/sg323/sg323_check_machine <folder with the 0803-0808 PROM images, binary>
```

## In the browser

`web/` builds every machine into one static page (wasm in an AudioWorklet, bring your own ROMs,
kept in the browser). See `web/README.md`.

## Machines

| Machine | Status |
|---|---|
| DN780 (v2.2 firmware) | plugin builds, kit checks and readout checks pass. Programs: the 4 reverbs, Alive, Non-Linear, Reverse, Delay, ADT, Multitap Echo. Not yet: v1.5, Sound-on-Sound, Infinite |
| RMX16 (v3.2 firmware) | plugin builds; kit checks, typed-settings checks on all 17 programs and pluginval (strictness 10) pass. The whole machine runs live (32x real time); settings are typed on its keys and read back from its display. Hardware panel (2026-11-18): a vector front panel whose keys, pots and power switch go straight to the firmware, with what they set adopted as parameters; stores live in the battery RAM, saved with the session. Not yet: ECHO's B side, the input/output level stage and o/flo LEDs; the plugin's name and the output labelling (A/B) are open |
| DMX 15-80 (DMX23-3 + SHL-406 extender) | plugin builds; kit checks, typed-settings checks (delay and pitch per channel, clamps, RAM fits, one pitch board), delays heard at the typed times and pluginval (strictness 10) pass. The core matches the DMX project's engine byte for byte. The whole machine runs live (8-11x real time); delay and pitch are typed on its keypad and read back from its display; the knobs and switches apply directly (regen as the drawn F/B GAIN and F/B FILTER pots, 2026-11-18); a machine-option change builds a new machine with the settings typed in. Hardware panel (2026-11-18): a vector front panel whose keypad and nudge keys go straight to the firmware (delay/pitch typed there are adopted as parameters), knobs and switches bound to the parameters, h/room LEDs from the output level. Not yet: stores saved with the session, loop mode, power/reset; the plugin's name is open |
| StarGate 323 (PROMs 0803-0808) | plugin builds; kit checks, readout checks (all 16 predelay times from the PROMs), an impulse heard at the set predelay and pluginval (strictness 10) pass. The core is the StarGate project's C++ port of its Python model, byte for byte with the same float flags. Runs at 192 kHz inside (45x real time). Panel labels and order, with Input mute (2026-11-18). Not yet: clock drift and source level are not modelled; the plugin's name is open |

Planned: the others (the two Ursa Majors next).

Machines set by typing keys (the RMX16 and the DMX 15-80) return `live_readouts() = true`: the
host then refreshes their readouts every ~50 ms, since the display changes only once the
firmware has taken the keys. Such a machine types its settings on the control thread before
audio starts (so a session's first block hears them), and on the audio thread after.
