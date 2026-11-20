// The DN780 as a kit::Machine: dn780::Engine (core/engine.hpp) with the v2.2 front panel as
// parameters, readouts from the firmware's own 8279 display, and the factory memories as presets.
//
// Parameters are the panel's seven keys plus the program: each change writes the parameter bytes
// (E031-E039) and runs the firmware's program loader, as a key press does on the unit. Values are
// first clamped to the firmware's own per-program limits (its table at 1CA0), so the loader is never
// handed a value the panel could not set. Parameters a program does not use read "".
//
// v2.2 only for now: v1.5 has another program list and parameter map (notes/2026-11-09-2230).
// Sound-on-Sound and Infinite are left out (their Level-key hold input is not modelled).
#pragma once
#include "../../shared/core/machine.hpp"

namespace dn780plug {

const kit::MachineType& machine_type();

}  // namespace dn780plug
