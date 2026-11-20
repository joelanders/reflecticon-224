#pragma once
// Lens:    the WCS disassembler. It names the fields of each microinstruction
//          (one WCS word, one row), as the row machine's decode() reads them,
//          so the listing and the machine cannot disagree about a bit.
//
//   MEMR c=24/32+ RA=1 WA=0 XFER      offset 1234
//   OPER c= 0/32+ RA=0 WA=2           bus<-ADC  WR_DA AB
//
// Fields: the operation; the coefficient c/32 and the sign of the product
// (+ adds, - subtracts); the multiplicand register RA and the register the
// bus is written to, WA; XFER (RR := ACC) and ZERO (clear ACC after it).
// MEMW and MEMR show the delay memory offset (OFST/ complemented). OPER shows
// what drives the data bus, then its strobes: WR_DA and the DAC channels,
// WR_XREG, keep-shift (the multiplicand becomes the previous one / 64) and
// RESET (the last microinstruction of a sample time).
//
// The 224X and the original 224 pack the fields differently (decode(w,
// model)); the 224's offsets are 14 bits (16K words of delay memory).
#include "lexicon224x.hpp"
#include <cstdio>
#include <string>

namespace lexicon224x::lens {

// The delay offset of a MEMR or MEMW: the field holds its complement.
inline uint16_t delay_offset(const Microinstruction &mi, Model model = Model::Lexicon224X) {
    uint16_t offset = uint16_t(~mi.low);
    if (model == Model::Lexicon224) {
        return offset & 0x3fff;
    }
    return offset;
}

inline std::string disassemble(uint32_t word, Model model = Model::Lexicon224X) {
    static const char *const operations[] = {"NOP ", "OPER", "MEMW", "MEMR"};
    static const char *const sources[] = {"-   ", "RR  ", "XREG", "ADC "};
    Microinstruction mi = decode(word, model);
    char sign = '+';
    if (mi.negative) {
        sign = '-';
    }
    const char *xfer = "    ";
    if (mi.xfer) {
        xfer = "XFER";
    }
    const char *zero = "    ";
    if (mi.zero) {
        zero = "ZERO";
    }
    char buffer[128];
    std::snprintf(buffer, sizeof buffer, "%s c=%2u/32%c RA=%u WA=%u %s %s", operations[mi.op], mi.coefficient, sign,
                  mi.ra, mi.wa, xfer, zero);
    std::string text = buffer;
    if (mi.op == MEMW || mi.op == MEMR) {
        text += " offset " + std::to_string(delay_offset(mi, model));
    }
    if (mi.op == OPER) {
        text += std::string(" bus<-") + sources[mi.source];
        if (mi.wr_da) {
            text += " WR_DA ";
            for (unsigned channel = 0; channel < 4; channel++) {
                if (mi.channels >> channel & 1) {
                    text += "ABCD"[channel];
                }
            }
        }
        if (mi.wr_xreg) {
            text += " WR_XREG";
        }
        if (mi.keep_shifting) {
            text += " keep-shift";
        }
        if (mi.reset) {
            text += " RESET";
        }
    }
    return text;
}

// The program as it runs: one line per row (row, word, fields), up to the
// RESET row. On the 224X the row after it is the pass's last. On the 224 the
// RESET row is itself the last, and each row also shows its step, the SBC's
// numbering (row r is step 127 - r: the 224's pass is steps 127..28).
inline std::string listing(const uint32_t wcs[128], Model model = Model::Lexicon224X) {
    std::string text;
    for (unsigned row = 0; row < 128; row++) {
        char head[40];
        if (model == Model::Lexicon224) {
            std::snprintf(head, sizeof head, "%3u  step %3u  %08x  ", row, 127 - row, unsigned(wcs[row]));
        } else {
            std::snprintf(head, sizeof head, "%3u  %08x  ", row, unsigned(wcs[row]));
        }
        text += head + disassemble(wcs[row], model) + "\n";
        if (decode(wcs[row], model).reset) {
            if (model == Model::Lexicon224) {
                std::snprintf(head, sizeof head, "     (%u rows in the pass)\n", row + 1);
                text += head;
            } else {
                std::snprintf(head, sizeof head, "%3u  ", row + 1);
                text += std::string(head) + "(last row of the pass)\n";
            }
            break;
        }
    }
    return text;
}

}  // namespace lexicon224x::lens
