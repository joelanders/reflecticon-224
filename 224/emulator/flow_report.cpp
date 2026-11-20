// Print a WCS image's flow (flow.hpp): its delay lines, the links between
// rows, and the check against a running copy of the machine.
//
//   flow_report IMAGE [224]   a 512-byte WCS image, as static_run takes;
//                             224: an original 224 program
#include "flow.hpp"
#include "../isa-level-cpp/wcs_disassembler.hpp"
#include <cstdio>
#include <string>

using namespace lexicon224x;

int main(int argc, char **argv) {
    if (argc != 2 && !(argc == 3 && std::string(argv[2]) == "224")) {
        std::fprintf(stderr, "usage: %s IMAGE [224]\n", argv[0]);
        return 2;
    }
    Model model = Model::Lexicon224X;
    double row_ms = 292.96875e-6;
    if (argc == 3) {
        model = Model::Lexicon224;
        row_ms = 488.28125e-6;
    }
    uint8_t image[512];
    FILE *in = std::fopen(argv[1], "rb");
    if (!in || std::fread(image, 1, 512, in) != 512) {
        std::fprintf(stderr, "%s: need a 512-byte WCS image\n", argv[1]);
        return 1;
    }
    std::fclose(in);
    auto machine = std::make_unique<Machine>();
    load_wcs(*machine, image);
    lens::Flow f = lens::flow(machine->wcs, model);

    double pass_ms = f.rows * row_ms;
    std::printf("%u rows per pass: %.1f Hz\n", f.rows, 1000.0 / pass_ms);
    const char *kinds[] = {"Register", "Shift", "Product", "Result", "Memory"};
    unsigned counts[5] = {};
    for (const lens::Link &link : f.links) {
        counts[unsigned(link.kind)]++;
    }
    std::printf("links:");
    for (unsigned k = 0; k < 5; k++) {
        std::printf(" %s %u", kinds[k], counts[k]);
    }
    std::printf("\n\ndelay lines (write row: taps as row@lag)\n");
    uint32_t longest = 0;
    for (const lens::DelayLine &line : f.lines) {
        std::printf("  %3u:", line.write_row);
        if (line.taps.empty()) {
            std::printf(" (never read)");
        }
        for (const lens::Tap &tap : line.taps) {
            std::printf(" %u@%u (%.2f ms)", tap.row, tap.passes, tap.passes * pass_ms);
            if (tap.passes < lens::memory_words(model) && tap.passes > longest) {
                longest = tap.passes;
            }
        }
        std::printf("\n");
    }
    if (!f.unwritten_reads.empty()) {
        std::printf("reads no row writes:");
        for (unsigned row : f.unwritten_reads) {
            std::printf(" %u", row);
        }
        std::printf("\n");
    }
    lens::MemoryCheck check = lens::measure_memory(machine->wcs, f, longest + 2);
    std::printf("\nmeasured over %u passes: %u reads of written addresses, %u agree\n", check.passes, check.reads,
                check.agree);
    for (const std::string &line : check.disagreements) {
        std::printf("  %s\n", line.c_str());
    }
    return check.agree == check.reads ? 0 : 1;
}
