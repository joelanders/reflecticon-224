// Static-WCS run: load a 512-byte WCS image, run N rows, write the DAC
// captures in the reference machines' event format (same as ../isa-level-verilog/tests/static_run.sv).
//
//   static_run IMAGE EVENTS [ROWS]
//
// Stimulus: prepared-zero memory, ADC code +512 on rows 4096..6143 on the pin
// read while CH1 is high: the right input (the reference machines call it left).
#include "lexicon224x.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s IMAGE EVENTS [ROWS]\n", argv[0]);
        return 2;
    }
    long long rows = 4194304;
    if (argc > 3) {
        rows = std::atoll(argv[3]);
    }
    uint8_t image[512];
    FILE *in = std::fopen(argv[1], "rb");
    if (!in || std::fread(image, 1, 512, in) != 512) {
        std::fprintf(stderr, "%s: need a 512-byte WCS image\n", argv[1]);
        return 1;
    }
    std::fclose(in);
    auto machine = std::make_unique<lexicon224x::Machine>();
    lexicon224x::load_wcs(*machine, image);

    // Reference time base (1/576 ns ticks) so traces compare line for line.
    // A capture is observed 60 ns after its converter clock, in the next row.
    const long long first_row = 207531, row = 168750, observed = 178512;
    FILE *out = std::fopen(argv[2], "w");
    for (long long r = 0; r < rows; r++) {
        if (r >= 4096 && r < 6144) {
            machine->adc_right = 512;
        } else {
            machine->adc_right = 0;
        }
        lexicon224x::step_row(*machine);
        if (machine->dac_channels && r + 1 < rows) {
            std::fprintf(out, "A %lld %u 15 0 %u 4095 0 %u 15 0\n", first_row + r * row + observed,
                         machine->dac_channels, machine->dac_code, machine->dac_gain);
        }
    }
    std::fclose(out);
    return 0;
}
