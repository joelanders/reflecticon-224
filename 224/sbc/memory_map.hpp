#pragma once
// The SBC's memory map and I/O ports, shared by both machines that run the firmware.
#include <cstdint>

namespace lexicon224x::sbc {

// Memory (224X Service Manual 3.3-3.5):
//   0x0000-0x1FFF  SBC ROM, four 2716s
//   0x2000-0x27FF  NVS module RAM, 2 KB, battery backed
//   0x3C00-0x3FFF  SBC module RAM, 1 KB
//   0x4000-0x41FF  the WCS on the T&C, 128 steps x 4 bytes
//   0x8000-0xFFFE  NVS module ROM, eight 2732s
//   0xFFFF         the NVS module's 4-position DIP switch
// The host simply keeps all of 0x2000-0x7FFF writable,
// which covers both RAMs (the firmware never touches the gaps).
constexpr uint32_t ram_start = 0x2000;
constexpr uint32_t ram_end = 0x8000;
constexpr uint32_t wcs_start = 0x4000;
constexpr uint32_t wcs_end = 0x4200;
constexpr uint16_t dip_switch_address = 0xffff;

inline bool is_wcs(uint16_t address) {
    return address >= wcs_start && address < wcs_end;
}

// The CPU's byte addresses count WCS rows downwards, four bytes (lanes) each.
inline unsigned wcs_row(uint16_t address) {
    return 127 - ((address >> 2) & 127);
}

inline unsigned wcs_lane(uint16_t address) {
    return address & 3;
}

// I/O ports. Ports 0-9 reach the DSP boards, decoded on DMEM ("the 8080
// port-decoding circuitry"), and a port number means different things for
// OUT and for IN. The SBC's own ports are its 8255 (to the control head)
// and its 8251 (the serial port; on the 224XL, the LARC).
enum DspWritePort : uint8_t {
    SelectSingleStep = 0, SelectContinuous = 1, HaltDsp = 2, RunDsp = 3,   // DMEM single cycle/halt/run latches
    MonitorControl = 4,                                                     // (not modeled)
    ClearDelayCounter = 5, XregLowByte = 6, XregHighByte = 7,              // DMEM
};

enum DspReadPort : uint8_t {
    OffsetLow = 0, OffsetHigh = 1,                     // DMEM: the OFST/ lines (U48, U62)
    BusTest = 2,                                        // DMEM: the 8080 bus test register (U42)
    ArithmeticMonitor = 3, MicroinstructionMonitor = 4, TimingMonitor = 5,  // T&C: DPORT3-5 (U8, U6, U7)
    TransferLow = 6, TransferHigh = 7,                  // DMEM: the X register, DAB to SBC (U38, U40)
    HeadroomLeft = 8, HeadroomRight = 9,                // FPC: headroom registers
};

enum SbcPort : uint8_t {
    // the 8255 to the control head: port A data, port B digit address,
    // port C control, the mode word
    ControlHeadData = 0xe4, ControlHeadAddress = 0xe5, ControlHeadControl = 0xe6, ControlHeadPpiMode = 0xe7,
    SerialData = 0xee, SerialControl = 0xef,                                       // LARC 8251
};

inline bool is_sbc_port(uint8_t port) {
    return (port >= ControlHeadData && port <= ControlHeadPpiMode) || port == SerialData || port == SerialControl;
}

// The original 224 (224 Service Manual 1.2, 2.1): the same SBC card (a BLC)
// with four 2716s (ROM1-ROM4) at 0x0000-0x1FFF and only its own 1 KB of RAM
// at 0x3C00-0x3FFF (no NVS module: no battery RAM, no expansion ROM, no DIP
// switch); the WCS and the DSP ports are where they are on the 224X. Its
// 8251 is the BLC's own serial port at 0xDC/0xDD (RS-232, for diagnostics),
// not the LARC link.
constexpr uint32_t ram_start_224 = 0x3c00;
constexpr uint8_t SerialData224 = 0xdc, SerialControl224 = 0xdd;

inline bool is_sbc_port_224(uint8_t port) {
    return (port >= ControlHeadData && port <= ControlHeadPpiMode) || port == SerialData224 || port == SerialControl224;
}

}  // namespace lexicon224x::sbc
