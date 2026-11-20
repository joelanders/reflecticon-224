// The RMX16 as a kit::Machine: rmx16::Engine (core/engine.hpp), the whole machine running live,
// with the v3.2 front panel as plain parameters. A change is typed on the unit's keys by the
// engine's driver and read back from the firmware's own display, which is what the readouts show.
//
// Parameters: program (17), predelay (ms), decay, LF, HF, output swap. Values are clamped to what
// each program accepts before they are typed (the table in rmx16_machine.cpp, measured from v3.2
// on the RMX16 project's emulator, 2026-11-12), so the firmware is never handed a value its panel
// would refuse; a filter the program lacks is not typed and reads "".
//
// v3.2 only (its program list and ranges). As parameters (easy mode) not offered yet: ECHO's B side
// (keys A/B), the SAVE/RECALL stores, the pots (POT key). The hardware panel (rmx16_panel.hpp)
// reaches all of those: raw keys, the three pots the firmware reads, the power switch; the battery
// RAM (stores) is the machine state saved with the session. The analog input/output level stage
// and the o/flo LEDs are not modelled.
#pragma once
#include "../../shared/core/machine.hpp"

namespace rmx16plug {

const kit::MachineType& machine_type();

}  // namespace rmx16plug
