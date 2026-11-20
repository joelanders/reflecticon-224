# Panel fidelity audit: every machine's UIs against the real front panels

**Status:** audit, 2026-11-18. Four read-only passes compared each UI with the real panels, using the
reference photos fetched the same day (each emulator repo's `photos/`, with `SOURCES.md`; gitignored
because most are not freely licensed) plus the manuals. Nothing was changed.

## Bottom line
- **No machine has a faithful "preservation" panel in a shipped UI.** Most UIs are the "easy" kind:
  dropdowns and sliders.
  - In the Lexicon UIs, an *operator* presses the real keys behind the scenes.
  - The kit editor (`shared/plugin-kit/juce/kit_editor.cpp`) is a generic list of parameters plus one
    red text line.
- **Closest to faithful:**
  - RMX16 web page (`~/code/00-ams-dmx/emu/web/page`): every key, LED, digit and alphanumeric, driven by
    the firmware.
  - Older DMX JUCE plugin (`dmx15-80/juce-plugin`): nearly complete.
  - SST-282 web page.
  - A key-for-key LARC panel in the old `~/code/00-other-firmware-explore/lexicon-web/web/app.mjs`
    (2026-09-08), never carried into reflecticon.
- **The emulation underneath is usually far richer than the UI.** Most hardware functions that are
  missing are missing only from the UI layer, not from the core.

## How panel mappings were established
| Machine | Method | Confidence | Unmapped |
|---|---|---|---|
| Lexicon 224/224X head | 224X service manual table 3.2 (scan addresses, LED/digit/pot bytes) + cable sheet 060-01323 + disassembly (`09F5 IN E4`); soak test and `check_catalog.py` | buttons/pots high; select LEDs good; headroom/PROGRAM/OVFL bits from the table only, never exercised | 7-seg bit order checked only by the boot display |
| Lexicon LARC (224XL) | key bytes reverse-engineered from the decoder at 0x0495 (`00Prrrcc`), verified live; display commands inferred from firmware output (`wiki/firmware/larc-protocol.md`) | rom-exact for keys | the 4 indicator bytes (headroom), unknown commands, no cassette (TAPE) model |
| RMX16 | DMX 34.3 front-panel sheet blind-transcribed twice; segment map from the firmware font table at 9340; key/LED roles found by pressing each under emulation and matching the manual | medium-high | **data-bus low nibble chosen so "AMBIENCE"/"RMX 3.2" decode** (a convention, not a trace); o/flo LEDs are analog |
| DMX 15-80 | DMX 8 display card transcribed (one transcriber, two passes); firmware digit table 92DF and keypad table 92FB match the wiring bit for bit; manual p21–22 legends | high | ^/v LEDs never lit by this firmware; XTAL/VCO line; audio-trigger path |
| DN780 | firmware disassembly only (param-key codes, digit map; display checked against a video) | high for what's decoded | keypad 0–9, STO, SEQ, IN/REV codes; no core models 8279 key reads; BRD 2635/4 sheet untranscribed |
| StarGate 323 | audio sheets traced; display sheets 4 and 10 untranscribed | — | display, meter, override LEDs. **New:** `roms/0809.hex` is a 7-segment table (pages: room digit, decay units/tenths by room×decay reproducing Table III exactly, predelay digits, override bits, 8-step peak thermometer); address assignment inferred, medium confidence until sheet 4 is read |
| SST-282 | MOM-1 sheet 2 transcription (2026-10-16) | program codes high | S1A polarity, which decay state is LONG, −15 LED source, tapers |
| SP2016 | ROM strings (soft-key, parameter, DEFINE, choice texts) + hand-picked knob tables | audio high, panel none | host OS ROM missing, so the menu state machine must be rebuilt from the manual |
| Quantec QRS | sheet 6 (TE/TD panel) untranscribed; keys only forced in `tb_cop` | — | everything panel-side |
| Buchla 700 | signal numbers from the firmware source (`msl.c`) | high for signals | faders and XY pad bypass the firmware (FPU CV and RAM pokes) |

## Hardware functionality not reachable in any UI
- **Lexicon:** none of the three UIs (JUCE, web demo, VCV) can reach:
  - registers (SET/CALL+REG A–D, the 224X's 32 extended registers, the LARC's 36 registers, clear,
    labels);
  - TAPE, BANK label edit, the 2nd F functions, IMED/SHIFT modes;
  - the 7-segment digits, unit/OVFL/key LEDs, the LARC text display;
  - diagnostics, the PROGRAM 2 bypass, the rear DIP recall.

  MUTE is latched in the UIs, but momentary on the hardware. The headroom meters read FPC registers,
  not the firmware's LED output.
- **RMX16 kit plugin:**
  - missing: ECHO B, the ECHO 1.00–1.20 s range, stores/recall, the pot key, I/O pots and LEDs,
    direct keys;
  - the web page has all of these, except that the o/flo LEDs are missing and the I/O pots are silent
    (`--analog` not wired into `web.c`);
  - the web program menu lists only 9 of the 17 programs.
- **DMX kit plugin:**
  - missing: keypad, stores, nudge, reset, single-shot, lock-in loop start/end and varispeed.
  - **Bug in the older JUCE plugin:** "Recall store" types `D`+n, but `typeKeys` maps 'D' to 0xD (`#`)
    instead of store (0xE). Its keypad is also labelled E where the panel says D, and UP/DOWN are
    swapped.
- **DN780:**
  - missing: Sound-on-Sound / Infinite (CLI only, as a fixed gate), user memories 40–89, store and
    sequences, keypad, input knob, meter, IN/REV mutes, remote, diagnostics, v1.5 firmware.
- **StarGate:**
  - **Input Mute is missing from the plugin, the CLI and both cores**;
  - also missing: peak meter, override LEDs, real readouts (the kit's text is synthesised);
  - mislabelled: LF/HF *decay* shown as EQ in Hz, decay as "position N", input in dB.
- **SST-282 web:**
  - PUSH TO SET is missing (the echo knob acts live; the unit samples only while held);
  - the S1A row toggle is flattened into 16 buttons;
  - second-row program names are generic, though the photos and manual give FATTY, CLOUD, SLAP 1,
    SLAP 2, ECHO, SPACE REPEATS 2/3/4.
- **SP2016:**
  - missing: the display, soft keys (the "Clear reverb" button bypasses the ROM handler), DEFINE help,
    COMMAND menu, 65 user presets, LINE IN/OUT bypass, per-channel faders, meters, status lamps,
    selftests;
  - decay shows a raw index; Gated Reverb's gate and most RMX programs are absent.
- **Quantec:**
  - CLI only: no FREEZE, ENHANCE, MUTE, ENT, live changes, 64-slot memory or meters.
  - `STATUS.md` is out of date: it says AD/DA "not started"; the transcription session is now doing
    them.
- **Buchla:**
  - Tuning, Amplitude, longpot, scroll, pedals, CV and LEDs are not exposed.

## Recommendation
Give every machine two layers over the same core:
1. **Hardware mode:** a photo-faithful panel.
   - Raw key down/up, including chords and holds, goes straight to the emulated scan or serial port.
   - Displays and LEDs are rendered from the panel state the firmware writes.
   - Indicators that exist but are never driven (DMX ^/v) are drawn dark, not omitted.
   - No operator assumptions.
2. **Easy mode:** today's dropdowns and sliders, implemented by pressing real keys (as the Lexicon
   operator already does), so they can never reach anything hardware mode can't.

The shared kit is the main gap. `kit::Machine` (`shared/plugin-kit/core/machine.hpp`) offers params,
readouts and one text string. It needs:
- a key-event entry point;
- structured panel state (segments, LED bits, alphanumerics);
- pot inputs for the analog I/O stages.

The cores already expose most of this: `rmxc_key`, the DMX key codes, `lex_button`/`lex_key`/
`lex_panel_digits`/`lex_larc_text`, and the DN780 display RAM.

Cheap fixes before any skin work:
- the DMX JUCE store-recall bug;
- the RMX web program menu (9 of 17 programs);
- StarGate Input Mute (sheet 8, U2 4053);
- SST-282 PUSH TO SET and program names;
- Quantec `STATUS.md`.

Bigger panel-side reverse engineering still to do:
- DN780 8279 keyboard FIFO and remaining key codes (diagnostic 02 prints them; dispatch at
  00E6–010D);
- StarGate sheet 4 to confirm the 0809 addressing;
- Quantec sheet 6;
- SP2016 menu state machine rebuilt from the manual.

## Cheap fixes done (2026-11-18, later)
- **DMX JUCE plugin:** Recall store now works: the panel's D = store = code E. The keypad reads D, and DOWN sits left of UP. `editor_shot` checks the recall. dmx15-80 `90fb5d2`; `tests/run.py` 44 ok.
- **RMX16 web:** the program menu lists all 17 programs (10–17 typed as two digits). The pot knob sits above the POT key. 00-ams-dmx `766e7e8`; `test_dsp` 17/17, `boot.mjs` ok, checked in the browser.
- **StarGate Input Mute:** from sheet 8 U2. INMUTE grounds the RV1 wiper's 10K instead of feeding U8, so direct and reverb input stop and the tail decays.
  - Fast model and CLI: `--input-mute T0-T1|all`.
  - C++ core: stargate323 `bf3b2d0`; compare_cpp timed-mute case −46 dB; pytest 9 passed.
  - Kit plugin: Input mute plus panel labels and order (Input 0–10, LF/HF decay min–max, decay time in seconds from Table III). 00-gear-plugins `06f215c`; check_machine all passed, pluginval 10 SUCCESS.
- **SST-282 web:**
  - PUSH TO SET: the echo pot acts only while it is held, with a labelled "live" option.
  - S1A row button plus the 8 interlocked buttons.
  - Real second-row names (Fatty … Space repeats 4), also in `sst282/machine.py`.
  - Not a git repo; note `notes/opus-5.5/2026-11-18-0200-panel-push-to-set-row-names.md`.
- **Quantec `STATUS.md`:** already current: the converter session updated it at 01:29. No change.
