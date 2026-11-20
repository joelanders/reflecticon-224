// The StarGate 323 as a kit::Machine: sg323::Machine (core/sg323.hpp) at 192 kHz behind the kit's
// rate bridge. No firmware: fixed logic driven by six PROM tables (0803-0808), the user's images.
//
// Parameters: the front panel (room, decay, predelay, LF and HF EQ, input level, reverb and direct
// mix), the two service switches (S5 dry only, S6 reverb clear), modulation, the input-stage hiss,
// and the unit: the design values, or the tolerances fitted to ravettel's unit (de-emphasis trim,
// treble shelf; its +1558 ppm crystal is not modelled in real time).
//
// Stereo in, summed to mono as on the unit (U7); stereo out (the L and R tap sums).
#pragma once
#include "../../shared/core/machine.hpp"

namespace sg323plug {

const kit::MachineType& machine_type();

}  // namespace sg323plug
