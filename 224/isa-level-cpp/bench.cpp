// Speed of step_row() with no output: rows per second against realtime.
// Realtime is one row per 292.96875 ns, 3,413,333 rows/s (100-row pass = 34.13 kHz).
//
//   bench IMAGE [ROWS]
#include "lexicon224x.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s IMAGE [ROWS]\n", argv[0]);
        return 2;
    }
    long long rows = 41943040;
    if (argc > 2) {
        rows = std::atoll(argv[2]);
    }
    uint8_t image[512];
    FILE *in = std::fopen(argv[1], "rb");
    if (!in || std::fread(image, 1, 512, in) != 512) {
        std::fprintf(stderr, "bad image\n");
        return 1;
    }
    std::fclose(in);
    auto m = std::make_unique<lexicon224x::Machine>();
    lexicon224x::load_wcs(*m, image);

    unsigned checksum = 0;
    auto start = std::chrono::steady_clock::now();
    for (long long r = 0; r < rows; r++) {
        if (r >= 4096 && r < 6144) {
            m->adc_right = 512;  // the CH1 pin, as static_run.cpp
        } else {
            m->adc_right = 0;
        }
        lexicon224x::step_row(*m);
        checksum = checksum * 31 + m->dac_channels * 4096 + m->dac_code;
    }
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    double per_row = seconds / rows * 1e9;
    std::printf("%lld rows in %.3f s: %.1f ns/row, %.1fx realtime (checksum %08x)\n", rows, seconds,
                per_row, 292.96875 / per_row, checksum);
}
