// The DMX 15-80 as a kit::Machine: dmxkit::Engine (core/engine.hpp), the whole machine running live.
//
// Parameters:
//   - delay and pitch per channel, typed on the unit's keypad by the engine's driver and read back
//     from the firmware's display (the readouts show what the display confirmed);
//   - the front panel's knobs and switches (levels, regeneration, VCO, phase, mono input, mixed
//     outputs, LOCK IN, trigger), applied at every block;
//   - the machine options (RAM per channel, pitch boards, analog stages, smooth pitch), which build
//     a new machine with the settings typed in before it is heard.
//
// Stereo in (L = channel A, R = channel B), stereo out. As parameters (easy mode) not offered yet:
// the stores (the key printed D, code E, + digit), the nudge keys, loop mode. The hardware panel
// (dmx1580_panel.hpp, the 15-80S) reaches them: raw keypad and nudge keys, its knobs and switches
// as the parameters. Not emulated there: power, reset; the stores are not saved with the session.
#pragma once
#include "../../shared/core/machine.hpp"

namespace dmx1580plug {

const kit::MachineType& machine_type();

}  // namespace dmx1580plug
