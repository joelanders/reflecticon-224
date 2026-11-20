// Print a WCS image as the WCS disassembler reads it (wcs_disassembler.hpp).
//
//   wcs_listing IMAGE [224]   a 512-byte WCS image, as static_run takes
//   wcs_listing - [224]       32-bit words in hex on stdin, one per line (no listing: one line each)
// 224: decode as the original 224 (lexicon224x.hpp Model).
#include "wcs_disassembler.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

int main(int argc, char **argv) {
    if (argc != 2 && !(argc == 3 && std::string(argv[2]) == "224")) {
        std::fprintf(stderr, "usage: %s IMAGE|- [224]\n", argv[0]);
        return 2;
    }
    lexicon224x::Model model = lexicon224x::Model::Lexicon224X;
    if (argc == 3) {
        model = lexicon224x::Model::Lexicon224;
    }
    if (std::strcmp(argv[1], "-") == 0) {
        unsigned word = 0;
        while (std::scanf("%x", &word) == 1) {
            std::printf("%s\n", lexicon224x::lens::disassemble(word, model).c_str());
        }
        return 0;
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
    std::printf("%s", lexicon224x::lens::listing(machine->wcs, model).c_str());
    return 0;
}
