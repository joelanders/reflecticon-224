// Row machine side of the gate-level arithmetic sweep: the 128-row program
// (one coefficient x sign per row, every row: R0 := XREG input, ZERO, XFER),
// each operand held for two passes; prints RR after every row of the second.
//   arith_rows IMAGE < operands > "op row rr" lines
#include "lexicon224x.hpp"
#include <cstdio>
#include <memory>
int main(int argc, char **argv) {
    uint8_t image[512];
    FILE *in = std::fopen(argv[1], "rb");
    if (!in || std::fread(image, 1, 512, in) != 512) return 1;
    std::fclose(in);
    auto m = std::make_unique<lexicon224x::Machine>();
    lexicon224x::load_wcs(*m, image);
    unsigned op;
    while (std::scanf("%x", &op) == 1) {
        m->xreg_from_cpu = uint16_t(op);
        for (int pass = 0; pass < 2; pass++) {
            for (int r = 0; r < 128; r++) {
                unsigned row = m->pc;
                lexicon224x::step_row(*m);
                if (pass == 1) std::printf("%04x %u %04x\n", op, row, unsigned(uint16_t(m->RR)));
            }
        }
    }
}
