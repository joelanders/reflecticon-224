// The SystemVerilog side of tests/arith_exhaustive.py: one row's
// multiply-accumulate through the lexicon224x package functions, exhaustively.
//
// Same blocks and checksum as tests/arith_exhaustive.cpp:
//     word = ACC & 0xFFFFF | RR << 20 | SAT(3 adds, bit 0 first) << 36
// ACC comes from multiply_accumulate() as dsp.sv calls it (keep-shifting:
// three shift_two() of x_prev, as dsp.sv does), RR from to_sample(). The
// package has no SAT output, so SAT here is "accumulate() did not return the
// plain 20-bit sum", computed per add along partial()/shift_two()/accumulate()
// in multiply_accumulate's own order; that three-step ACC must also equal
// multiply_accumulate's (counted in the "internal" line, expected 0).
//
//   +mode=exhaustive|keep +priors=FILE   (one prior per line; "Z" = a ZERO row)
//   +dump=1 +dp=P +dn=N +dc=C            (every word of one block, hex)
module arith_exhaustive;
    timeunit 1ns;
    timeprecision 1ns;
    import lexicon224x::*;

    function automatic longint unsigned weight(longint unsigned i);
        return (i * 64'h9E3779B97F4A7C15 + 64'h632BE59BD9B4E019) | 64'd1;
    endfunction

    function automatic longint unsigned row_word(value_t acc0, value_t x, logic [5:0] c, logic negative,
                                                 output logic inconsistent);
        value_t acc, step, x4, x16, sum;
        logic [2:0] sat;
        logic [15:0] rr;
        // multiply_accumulate's steps, one add at a time, for SAT.
        x4 = shift_two(x);
        x16 = shift_two(x4);
        step = acc0;
        sum = step + (negative ? -partial(x, c[5], c[4]) : partial(x, c[5], c[4]));
        acc = accumulate(step, partial(x, c[5], c[4]), negative);
        sat[0] = acc != sum;
        step = acc;
        sum = step + (negative ? -partial(x4, c[3], c[2]) : partial(x4, c[3], c[2]));
        acc = accumulate(step, partial(x4, c[3], c[2]), negative);
        sat[1] = acc != sum;
        step = acc;
        sum = step + (negative ? -partial(x16, c[1], c[0]) : partial(x16, c[1], c[0]));
        acc = accumulate(step, partial(x16, c[1], c[0]), negative);
        sat[2] = acc != sum;
        // The function under test.
        step = multiply_accumulate(acc0, x, c, negative);
        inconsistent = step != acc;
        rr = to_sample(step);
        return {25'd0, sat, rr, step};
    endfunction

    initial begin
        string mode, priors_file, line;
        int fd, count, dump, dp, dn, dc;
        value_t priors [$];
        logic zeros [$];
        longint unsigned sums [64];
        longint internal;
        logic bad;

        if (!$value$plusargs("mode=%s", mode)) mode = "exhaustive";
        if (!$value$plusargs("priors=%s", priors_file)) $fatal(1, "+priors=FILE required");
        if (!$value$plusargs("dump=%d", dump)) dump = 0;
        if (!$value$plusargs("dp=%d", dp)) dp = -1;
        if (!$value$plusargs("dn=%d", dn)) dn = -1;
        if (!$value$plusargs("dc=%d", dc)) dc = -1;
        fd = $fopen(priors_file, "r");
        if (fd == 0) $fatal(1, "cannot open %s", priors_file);
        while ($fgets(line, fd) > 0) begin
            int v;
            if (line.len() < 1) continue;
            if (line.getc(0) == "Z") begin
                priors.push_back(ACC_MIN);  // junk under the clear
                zeros.push_back(1'b1);
            end else begin
                count = $sscanf(line, "%d", v);
                if (count != 1) continue;
                priors.push_back(value_t'(v));
                zeros.push_back(1'b0);
            end
        end
        $fclose(fd);

        internal = 0;
        for (int p = 0; p < priors.size(); p++) begin
            if (dump != 0 && p != dp) continue;
            for (int n = 0; n < 2; n++) begin
                if (dump != 0 && n != dn) continue;
                for (int c = 0; c < 64; c++) begin
                    longint unsigned total;
                    if (dump != 0 && c != dc) continue;
                    total = 0;
                    if (mode == "keep") begin
                        for (int s = -(1 << 18); s < (1 << 18); s++) begin
                            value_t x;
                            longint unsigned w;
                            x = shift_two(shift_two(shift_two(value_t'(s))));  // dsp.sv's keep-shifting
                            w = row_word(zeros[p] ? '0 : priors[p], x, 6'(c), n[0], bad);
                            internal += longint'(bad);
                            if (dump != 0) $display("%016h", w);
                            total += w * weight(longint'(s) + 64'd262144);
                        end
                    end else begin
                        for (int s = -32768; s < 32768; s++) begin
                            longint unsigned w;
                            w = row_word(zeros[p] ? '0 : priors[p], from_sample(16'(s)), 6'(c), n[0], bad);
                            internal += longint'(bad);
                            if (dump != 0) $display("%016h", w);
                            total += w * weight(longint'(s) + 64'd32768);
                        end
                    end
                    sums[c] = total;
                end
                if (dump == 0)
                    for (int c = 0; c < 64; c++) $display("%0d %0d %0d %016h", p, n, c, sums[c]);
            end
        end
        if (dump == 0) $display("internal %0d", internal);
        $finish;
    end
endmodule
