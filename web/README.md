# The kit machines in the browser

One page for every machine on the kit (DN780, RMX16, DMX 15-80, StarGate 323). Each runs live in an
AudioWorklet as wasm, the same C++ as the plugins. Bring your own ROMs: drop the image files on the
page; they are kept in that browser's IndexedDB and never uploaded. Nothing here holds ROM bytes.

```
web/build.sh                                  # all machines, or: web/build.sh sg323 dmx1580
python3 -m http.server -d web/page 8323       # then open http://localhost:8323/#sg323
node web/tests/node_check.mjs sg323 <ROM folder> [seconds]
```

Needs emscripten (`em++`). The page is static: any web server works, no special headers (no
SharedArrayBuffer). Phones need https for the microphone; the rest works over plain http on localhost.

| File | What |
|---|---|
| `kit_web.cpp` | a C API over `kit::Machine`: the parameter table as JSON, ROM recognition, boot, parameters, audio blocks, readouts, presets, the hardware panel (`kw_panel_layout`, `kw_panel_key`, `kw_panel_input`, `kw_panel`) |
| `build.sh` | builds `page/machines/<id>.js` (wasm embedded, ES module) and `page/worklet-<id>.mjs` per machine; writes the DMX repair masks header as CMake does |
| `page/machine.mjs` | the JS class over a module; works on the main thread, in the worklet (no TextDecoder there) and in Node |
| `page/worklet.mjs` | the AudioWorkletProcessor: boots, takes parameters and panel keys/inputs between blocks, sends readouts and (~30 a second) the panel state |
| `page/panel.mjs` | the hardware panel as SVG from the machine's `kit::PanelLayout` (mm): keys held while the pointer is down (shift-click latches), computer keys, knobs (drag up/down), switches, LEDs, 7-segment and 14-segment displays |
| `page/app.mjs`, `index.html`, `style.css` | the page: machine tabs, ROM slots, controls built from the parameter table, the panel text, source, dry/wet, output meter |
| `page/sounds.mjs` | synthesized test sources (drum loop, plucks, clicks, noise bursts) |
| `tests/node_check.mjs` | the module in Node through the same API: boot, render noise, readouts, speed |

How it differs from the plugin:
- No `kit::Host`: the worklet calls the machine directly, so `set_params()` runs on the audio thread
  between blocks. A change that builds a new machine (the DMX's RAM or pitch-board options, for
  example) can drop out for a moment. Knob changes are fine.
- Readouts come back ~12 times a second for machines whose display moves (`live_readouts`), otherwise
  after each change.
- Parameter values are remembered per machine (localStorage); ROMs per machine (IndexedDB).
- Hardware panel (RMX16, DMX 15-80; the toggle sits by Controls, the choice is remembered): what the
  panel keys set comes back as `adopt` values in the panel state and becomes the parameter values,
  sent back unchanged so the machine does not retype them. Panel inputs (the RMX's pots and power)
  are remembered per machine. Machine state (the RMX's battery RAM) is not kept across reloads yet.

Checked 2026-11-12:
- Node, 5 s of noise at 48 kHz in 128-frame blocks: DN780 9.7x real time, RMX16 33x, DMX 12x,
  StarGate 34x. All boot, output is finite and not silent, readouts cover every parameter.
- In a browser (the Claude app's pane, Chromium):
  - all four boot from dropped ROM files and play the drum loop (output peaks -9 to -16 dB);
  - ROMs and settings survive a reload;
  - a StarGate room/predelay change shows on the panel at once, and a DMX delay typed to 500 ms
    shows in the firmware's display within ~1 s;
  - at 375 px wide there is no sideways scroll;
  - no console errors.
Checked 2026-11-18 (hardware panel, Chromium pane): RMX16 PROGRAM 3 # from a mouse click and the
keyboard shows HALL B3 and the page adopts program 3 (easy mode then shows it); DMX A 3 0 0 # from
the keyboard shows 300 and is adopted as Delay A; a dragged regen knob moves the parameter; the
h/room LEDs follow the drum loop; no console errors.

- Not checked: Safari/iOS and Firefox, a real phone, the microphone and audio-file sources, long
  runs for dropouts. Output is not compared sample by sample with the native build (wasm's libm can
  differ in the last bits).
