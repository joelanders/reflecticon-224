// The C++ side of tests/arith_exhaustive.py: one row's multiply-accumulate,
// exhaustively, through isa-level-cpp/lexicon224x.hpp. Three routes, each a different
// depth into the header:
//
//   helpers  partial() / accumulate() / overflows() composed as execute() uses
//            them: the operand shifts two places between partial products.
//   aruck    the ARU pipeline itself: a Machine clocked edge by edge with
//            aruck(); RR from settled_sum() between the last two edges.
//   machine  step_row() on a WCS program: test rows (OPER, XREG source, one
//            coefficient each) alternate with observer rows (NOP + XFER).
//
// Output: one checksum line per block (prior, negative, coefficient), a
// weighted sum mod 2^64 of one 64-bit word per case:
//     word = ACC & 0xFFFFF | RR << 20 | SAT(3 adds, bit 0 first) << 36
// The Python side computes the same words and weights from its own spec.
//
//   arith_exhaustive exhaustive ROUTE P0 P1 ...       (priors; "Z" = a ZERO row)
//   arith_exhaustive keep ROUTE P0 P1 ...             (keep-shifting, x_prev in [-2^18, 2^18))
//   arith_exhaustive dump KIND ROUTE PRIOR NEG COEFF  (every word of one block, hex)
//   arith_exhaustive chain FILE [DUMP_PROGRAM]        (random programs via load_wcs + step_row)
#include "../../isa-level-cpp/lexicon224x.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace lexicon224x;

static uint64_t weight(uint64_t i) {
    return (i * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) | 1;
}

static uint64_t word_of(int32_t acc, uint16_t rr, unsigned sat) {
    return (uint64_t(uint32_t(acc)) & 0xFFFFF) | uint64_t(rr) << 20 | uint64_t(sat) << 36;
}

struct Prior {
    bool zero;      // this row is a ZERO row (the prior is irrelevant; ACC starts at 0)
    int32_t value;
};

static std::vector<Prior> parse_priors(int argc, char **argv, int first) {
    std::vector<Prior> priors;
    for (int i = first; i < argc; i++) {
        if (std::strcmp(argv[i], "Z") == 0) {
            priors.push_back({true, AccMin});  // junk under the clear
        } else {
            priors.push_back({false, int32_t(std::atol(argv[i]))});
        }
    }
    return priors;
}

// ---- route: helpers -----------------------------------------------------------
static uint64_t row_helpers(Prior prior, int32_t x, unsigned c, bool negative) {
    int32_t acc = prior.zero ? 0 : prior.value;
    unsigned sat = 0;
    int32_t operand = x;
    for (unsigned k = 0; k < 3; k++) {
        int32_t p = partial(operand, bit(c, 5 - 2 * k), bit(c, 4 - 2 * k));
        sat |= unsigned(overflows(acc, p, negative)) << k;
        acc = accumulate(acc, p, negative);
        operand >>= 2;
    }
    return word_of(acc, uint16_t(int16_t(acc >> 3)), sat);
}

// ---- route: aruck -------------------------------------------------------------
// From a state whose partial register is empty, with the operand register at
// x_prev: `shifts` extra shift edges (3 = keep-shifting's x_prev / 64), then
// the row's multiply as execute() clocks it.
static uint64_t row_aruck(Machine &m, Prior prior, int32_t x_prev, unsigned shifts, unsigned c, bool negative) {
    m.operand = x_prev;
    m.partial = 0;
    m.partial_negative = false;
    m.ACC = prior.value;
    for (unsigned k = 0; k < shifts; k++) {
        aruck(m, {OperandClock::Shift, 0, 0, false, false});
    }
    // ARUCK 2 of the row: first partial (c5 c4); ZERO replaces the add.
    aruck(m, {OperandClock::Shift, 0, bits(c, 5, 4), negative, prior.zero});
    unsigned sat = 0;
    // ARUCK 0 and 1 of the next row: partials c3 c2 and c1 c0; adds of the first two.
    sat |= unsigned(aruck(m, {OperandClock::Shift, 0, bits(c, 3, 2), negative, false})) << 0;
    sat |= unsigned(aruck(m, {OperandClock::Hold, 0, bits(c, 1, 0), negative, false})) << 1;
    int16_t rr = int16_t(settled_sum(m) >> 3);  // what an XFER here would save
    sat |= unsigned(aruck(m, {OperandClock::Hold, 0, 0, false, false})) << 2;
    return word_of(m.ACC, uint16_t(rr), sat);
}

// ---- route: machine -----------------------------------------------------------
// The instruction word as the README's table lays it out.
static uint32_t instruction(unsigned c, bool zero, bool xfer, bool negative, unsigned ra, unsigned wa, unsigned op,
                            unsigned low) {
    return uint32_t(c) << 26 | uint32_t(zero) << 25 | uint32_t(xfer) << 24 | uint32_t(negative) << 23 |
           uint32_t(ra) << 20 | uint32_t(wa) << 18 | uint32_t(op) << 16 | low;
}

static void program_for(Machine &m, bool zero, bool negative) {
    for (unsigned k = 0; k < 64; k++) {
        m.wcs[2 * k] = instruction(k, zero, false, negative, 0, 0, OPER, FromXREG << 12);  // R0 := XREG; ACC +/-= R0*k/32
        m.wcs[2 * k + 1] = instruction(0, false, true, false, 1, 1, NOP, 0);  // XFER: RR := the sum; its adds finish
    }
}

// ---- modes --------------------------------------------------------------------
static int exhaustive(const std::string &route, const std::vector<Prior> &priors, bool keep, long dump_p = -1,
                      int dump_n = -1, int dump_c = -1) {
    const long lo = keep ? -(1L << 18) : -32768, hi = keep ? (1L << 18) : 32768;
    auto machine = std::make_unique<Machine>();
    Machine &m = *machine;
    for (size_t p = 0; p < priors.size(); p++) {
        if (dump_p >= 0 && long(p) != dump_p) {
            continue;
        }
        for (int n = 0; n < 2; n++) {
            if (dump_n >= 0 && n != dump_n) {
                continue;
            }
            std::vector<uint64_t> sums(64, 0);
            if (route == "machine") {
                if (keep) {
                    std::fprintf(stderr, "machine route has no keep mode\n");
                    return 2;
                }
                m = Machine{};
                program_for(m, priors[p].zero, n);
                for (long s = lo; s < hi; s++) {
                    m.xreg_from_cpu = uint16_t(s);
                    for (unsigned c = 0; c < 64; c++) {
                        m.ACC = priors[p].value;  // the prior sum (under a ZERO row: junk)
                        step_row(m);              // test row: loads R0 * 8, first partial
                        step_row(m);              // observer: XFER, then the last add
                        uint64_t w = word_of(m.ACC, uint16_t(m.RR), m.saturated);
                        if (dump_c >= 0) {
                            if (int(c) == dump_c) {
                                std::printf("%016llx\n", (unsigned long long)w);
                            }
                        } else {
                            sums[c] += w * weight(uint64_t(s - lo));
                        }
                    }
                }
            } else {
                for (unsigned c = 0; c < 64; c++) {
                    if (dump_c >= 0 && int(c) != dump_c) {
                        continue;
                    }
                    for (long s = lo; s < hi; s++) {
                        int32_t x = keep ? int32_t(s) : int32_t(s) * 8;
                        uint64_t w;
                        if (route == "helpers") {
                            int32_t xs = x;
                            if (keep) {
                                xs = ((xs >> 2) >> 2) >> 2;
                            }
                            w = row_helpers(priors[p], xs, c, n);
                        } else if (route == "aruck") {
                            w = row_aruck(m, priors[p], x, keep ? 3 : 0, c, n);
                        } else {
                            std::fprintf(stderr, "unknown route %s\n", route.c_str());
                            return 2;
                        }
                        if (dump_c >= 0) {
                            std::printf("%016llx\n", (unsigned long long)w);
                        } else {
                            sums[c] += w * weight(uint64_t(s - lo));
                        }
                    }
                }
            }
            if (dump_c < 0) {
                for (unsigned c = 0; c < 64; c++) {
                    std::printf("%zu %d %u %016llx\n", p, n, c, (unsigned long long)sums[c]);
                }
            }
        }
    }
    return 0;
}

// Chain file: uint32 programs; per program a 512-byte CPU-order WCS image
// (complemented bytes, row a at (a ^ 127) * 4) and 128 little-endian XREG
// input words, one per row. The machine runs them back to back: 128 rows per
// program, no RESET, so the program counter wraps onto the next image.
static int chain(const char *path, long dump_program) {
    FILE *in = std::fopen(path, "rb");
    if (!in) {
        std::perror(path);
        return 1;
    }
    uint32_t programs = 0;
    if (std::fread(&programs, 4, 1, in) != 1) {
        return 1;
    }
    auto machine = std::make_unique<Machine>();
    Machine &m = *machine;
    uint8_t block[512 + 256];
    for (uint32_t p = 0; p < programs; p++) {
        if (std::fread(block, 1, sizeof block, in) != sizeof block) {
            std::fprintf(stderr, "short chain file\n");
            return 1;
        }
        load_wcs(m, block);
        uint64_t sum = 0;
        for (unsigned r = 0; r < 128; r++) {
            m.xreg_from_cpu = uint16_t(block[512 + 2 * r] | block[513 + 2 * r] << 8);
            step_row(m);
            // After a row: RR; ACC as this row's three edges leave it (the sum
            // through the previous row, or 0 under ZERO); SAT of the previous
            // row's three adds; the XREG output; the registers.
            uint64_t w1 = uint64_t(uint16_t(m.RR)) | (uint64_t(uint32_t(m.ACC)) & 0xFFFFF) << 16 |
                          uint64_t(m.saturated) << 36 | uint64_t(m.xreg_to_cpu) << 40;
            uint64_t w2 = uint64_t(m.R[0]) | uint64_t(m.R[1]) << 16 | uint64_t(m.R[2]) << 32 |
                          uint64_t(m.R[3]) << 48;
            if (long(p) == dump_program) {
                std::printf("%u %016llx %016llx\n", r, (unsigned long long)w1, (unsigned long long)w2);
            }
            sum += w1 * weight(2 * r) + w2 * weight(2 * r + 1);
        }
        if (dump_program < 0) {
            std::printf("%u %016llx\n", p, (unsigned long long)sum);
        }
    }
    std::fclose(in);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "see the header comment for usage\n");
        return 2;
    }
    std::string mode = argv[1];
    if (mode == "exhaustive" || mode == "keep") {
        return exhaustive(argv[2], parse_priors(argc, argv, 3), mode == "keep");
    }
    if (mode == "dump" && argc >= 7) {
        // dump KIND ROUTE PRIOR NEG COEFF : PRIOR is a single value or Z
        std::vector<Prior> priors = parse_priors(argc, argv, 4);
        priors.resize(1);
        return exhaustive(argv[3], priors, std::string(argv[2]) == "keep", 0, std::atoi(argv[5]), std::atoi(argv[6]));
    }
    if (mode == "chain") {
        return chain(argv[2], argc > 3 ? std::atol(argv[3]) : -1);
    }
    std::fprintf(stderr, "bad arguments\n");
    return 2;
}
