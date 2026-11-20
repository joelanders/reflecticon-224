# Hardware panel mode: kit API, RMX16 and DMX 15-80 panels

Follows [the panel fidelity audit](2026-11-18-0131-panel-fidelity-audit.md). The kit can now show a
machine's real front panel next to the easy (parameter) controls. Its keys, pots and switches go to the
machine as raw panel events. What the user sets on the panel comes back and becomes the parameter
values, so the easy controls and the DAW stay in step. The RMX16 and the DMX 15-80 have panels.
DN780 and StarGate have hooks only.

Screenshots (rendered by the `*_editor_shot` tools; vector drawings, no photo content):
- [RMX16 hardware panel](2026-11-18-0200-rmx16-hardware-panel.png)
- [RMX16 easy controls](2026-11-18-0200-rmx16-easy-controls.png)
- [DMX 15-80 hardware panel](2026-11-18-0200-dmx1580-hardware-panel.png), after `B C 1 * 5 0 0 #`

The reference photos stay in each project's gitignored `photos/`. The layouts were measured from
them, and only coordinates and legends are in the code.

## API

### `shared/plugin-kit/core/panel.hpp` (no JUCE)

`PanelLayout` describes the panel in millimetres, as data:

| Field | What it holds |
|---|---|
| `width`, `height` | panel size in mm |
| `face`, `ink` | colours (ARGB) |
| `elements` | the drawn and played parts |
| `inputs` | the continuous inputs |
| `leds`, `digits`, `alpha` | the state sizes |
| `notes` | text for the user |

Each `PanelElement` has a kind: `Frame`, `Window`, `Text`, `Key`, `Led`, `Digit`, `Alpha`, `Pot`,
`Switch` or `Hole`. All kinds share these fields:
- position `x, y, w, h`;
- `id`;
- `param`: a parameter id for pots and switches bound to a parameter. Empty means the element is a
  panel input with that `id`; with `id = -1` as well, it is inert and only drawn.
- `text`, `colour`, `size`;
- `style`, which depends on the kind:
  - keys: `DarkSquare`, `KeypadCream` or `LightSquare`;
  - digits: `Seven` or `Sign`;
  - text: centre, left or right;
  - switches: which way is on;
- `count`, the number of cells (alphanumeric displays);
- `hotkey`, the computer keys for a key: characters, `"UP"`, `"DOWN"`, or `"\r"` for Return.

A `PanelInput` is `{id, name, min, max, def}`.

`PanelState` is what the machine shows now:

| Field | What it holds |
|---|---|
| `valid` | whether the state is usable |
| `digits` | segment bytes in kit order: bit0 a … bit6 g, bit7 dp. A `Sign` digit uses g for minus and b for the vertical bar of plus. |
| `leds` | levels 0–255 |
| `alpha` | text for the 14-segment cells |
| `held` | keys the easy-mode driver is holding, so the editor can show it typing |
| `adopt` | one float per parameter, NaN meaning "leave". Settings the panel's keys made that should become parameter values. |
| `adopt_serial` | increments with each new adopt set |

`PanelKeyQueue<N>` is a lock-free single-producer, single-consumer key-event ring:
- `poll(now, min_hold, apply)` holds each key down for at least `min_hold`.
- A press that follows a quick tap waits until that tap's up has been applied, plus `min_hold/2`.
  This holds whether it is the same key or another. So "0 0" or "3 #" typed faster than the hold
  arrives as separate presses, not as one long press or a chord. Keys really held together still chord.
- It also provides `any_held()`, `last_at()`, `last_key()` and `events()`.

### `kit::Machine` (core/machine.hpp)

The new members all default to nothing, so machines without a panel are unchanged:
- `panel_key(int id, bool down)`: a key, any thread. The machine queues it, typically in a `PanelKeyQueue`.
- `panel_input(int id, float value)`: a pot or switch that is not a parameter (the RMX's pots and power).
- `PanelState panel() const`: the current state.
- `Info::panel`: the layout. With no elements, the machine has easy mode only.

The params path is unchanged. In easy mode the machines still type settings on their own keys through
their drivers. A manual key stops the driver and makes it forget its target. Before planning again,
the driver reads the display, so it starts from what the panel left.

### `kit::Host` (core/host.hpp)

| Member | What it does |
|---|---|
| `panel_key(id, down)` | queued under a lock and handed to the machine by the worker |
| `panel_input(id, v)` | stored and sent again to every new machine in `install()` |
| `panel_input_value(id)` | reads a stored input back |
| `panel()` | a copy of the state, cached by the worker about every 12 ticks |
| `refresh_panel()` | updates the cache now (used by tests) |
| `adopt(write)` | runs `write` (which stores the adopt values in the params) under the control lock, then marks those values as applied, so the machine does not retype them |

### JUCE (`juce/kit_panel.*`, `kit_editor.*`, `kit_processor.*`)

`kit::PanelView` draws a layout and plays it.

Drawing:
- slanted 7-segment digits, 14-segment alphanumerics;
- LEDs, drawn dark when undriven or off;
- knobs with their dial numerals, toggles, frames, holes.

Playing:
- keys stay down while the mouse is held;
- shift-click latches a key, for chords;
- computer keys come from `hotkey`, several at once;
- the editor highlights keys the easy-mode driver is holding.

The kit editor has a button to switch between "Hardware panel" and "Easy controls".

The processor:
- saves the editor mode and the panel inputs with the session state;
- adopts on its 30 Hz timer (`adoptPanelSettings`), which sets the parameters with a host gesture;
- defaults to the easy editor; `KIT_EDITOR=hardware` makes the hardware panel the default (used to
  run pluginval on that editor).

`<target>_editor_shot OUT.png [param=v ...] [hardware] [press=K1,K2,...]` renders either editor.
- `press=` holds each listed key for 150 ms, with 250 ms between keys, then waits 1.5 s and adopts.
- It also prints the panel state and the parameters.

### Web (`web/`)

- `kit_web.cpp` exports `kw_panel_layout()` (JSON), `kw_panel_key`, `kw_panel_input` and `kw_panel()` (JSON).
- `page/panel.mjs` draws the same layout as SVG.
- The worklet takes `{key}` and `{input}` messages and posts the panel state about 30 times a second.
- The app adopts settings the same way as the plugin and remembers the editor mode and the panel inputs.
- [web/README.md](../web/README.md) has the details.

## Per machine

### RMX16

Layout: `machines/rmx16/rmx16_panel.hpp`, measured from straight-on photos of a v3.x unit.

What works:
- Ten function keys and the 16-key keypad go to `rmxc_key`. Ids are rmxc codes: 1–10 are S1–S10;
  `100 + 10·col + row` is the keypad.
- 13 digits, with digits 7 and 9 as TIL327 sign digits.
- 8-character alphanumeric display; the ten key LEDs; the power LED.
- The pot, in and out knobs are inputs 0–2 (0–255) that the firmware reads.
- The power switch is input 3: off darkens the panel and silences the output, on boots again.
- Machine state is the 2 KB battery RAM, saved with the session. Stores made on the panel survive a
  reload.
- Settings typed on the panel are adopted after 0.4 s of quiet, unless an entry is still open:
  program, predelay, decay, and LF/HF where the program has them.

What it lacks:
- The four o/flo LEDs show analog input level, which is not modelled; they are drawn dark.
- The in/out pots do not change level: the analog level stage is not modelled, only the firmware reads them.
- ECHO's B side.
- The maker's plate is plain lettering, not the logo artwork.

### DMX 15-80

Layout: `machines/dmx1580/dmx1580_panel.hpp`, measured from photos of a 15-80S.

What works:
- Keypad 0–9, A–C, `#`, `*`, the store key (printed D), and nudge up/down, all as firmware keypad codes.
- 4 digits.
- LEDs: a/b/c, rate, xtal/vco (following the vco switch), stereo, power.
- The 6/12/18 dB h/room LEDs follow the output peak with a ~300 ms fall.
- The seven knobs and five switches are bound to the existing parameters.
- Delay and pitch typed on the keypad are read back from the display and the a/b/c LEDs, and adopted
  after `A`/`B`/`C`/`#`/nudge and 0.4 s of quiet.

What it lacks:
- Power and reset are drawn but do nothing.
- The ^/v LEDs and the LED between the nudge keys are never lit by this firmware; they are drawn dark.
- The amber 12 dB h/room threshold is assumed (6/12/18 dB below the samplers' clipping).
- The stereo LED logic is taken from the older plugin: on when neither mono in nor mix out is set.
- The "in mix" switch maps to `monoIn` (A feeds both channels).
- The F/B filter pots are not on this panel; they are in easy mode only.
- Stores made on the panel are not saved with the session (no machine state yet).

### DN780 (hook only)

- `panel()` returns its 16 digits in kit order. The firmware's segment bits were mapped to segments
  by matching against its own hex-digit table at 0x38F3.
- No layout and no keys yet. The 8279's key FIFO is not modelled for panel input.

### StarGate 323 (hook only)

- A comment marks where the 0809 PROM's display and switch decode would feed `panel()` and `panel_key()`.
- Nothing is drawn.

## Verification (2026-11-18)

Check programs:
- `rmx16_check_machine` passes, including section 5, the hardware panel:
  - layout and state sizes, unique key ids;
  - `PROGRAM 5 #` gives `5  0  1.0    -8   ROOM  A0`;
  - the setting is adopted and not typed back;
  - DECAY then UP held 1.5 s gives 1.4;
  - easy mode afterwards types decay 2.5 in 0.4 s;
  - SAVE 3, PROGRAM 1, RECALL 3 works;
  - `PROGRAM 1 #` tapped with no hold or gap at all works;
  - the battery state survives a reboot, and RECALL works after it;
  - power off goes dark and power on boots again.
- `dx1580_check_machine` passes, including section 6:
  - `A 3 0 0 #` gives ` 300  A`; `B C 1 * 5 0 0 #` gives `1.500  B  C`;
  - both are adopted;
  - UP held raises 300 to 305;
  - easy mode afterwards types delay B 500;
  - `A 4 0 0 #` tapped instantly gives ` 400  A`;
  - a loud input lights the h/room LEDs.
- `dn780_check_machine`: the `panel()` digits decode to the displayed predelay and decay.

Plugins:
- pluginval at strictness 10 with GUI tests passes for both VST3s, in the default (easy) editor and
  with `KIT_EDITOR=hardware`.
- The standalones launch and stay up with the hardware editor.

Web: in the Claude app's Chromium pane:
- RMX16 `PROGRAM 3 #` (mouse and keyboard) shows HALL B3 and is adopted.
- DMX `A 3 0 0 #` from the keyboard shows 300 and is adopted.
- Dragging a knob moves its parameter.
- No console errors.

Not verified:
- Sound through the standalone's real audio I/O.
- A real DAW session.
- AU validation of these builds. The installed AU components are older builds from Nov 12, and they
  were not replaced.
- Safari and Firefox for the web page.
