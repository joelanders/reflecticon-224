# Gate-level Verilog of the five 224X DSP boards, and its checks.
#
#   make                 generate the boards from the netlists and compile each one
#   make check           every check below that needs no firmware
#   make hp5004a         the service manual's HP 5004A signature tables
#                        (needs the two diagnostic-program captures, see hp5004a/README.md)

NETLISTS := ../schematics/netlists
BOARDS := tc aru dmem fpc dmem_io

# Each board: the netlist KiCad exports from its schematic, the nets that
# become module ports, and (T&C, DMEM) the changes that exist only for simulation.
NETLIST_tc      := $(NETLISTS)/tc.net
NETLIST_aru     := $(NETLISTS)/aru.net
NETLIST_dmem    := $(NETLISTS)/dmem.net
NETLIST_fpc     := $(NETLISTS)/fpc.net
NETLIST_dmem_io := $(NETLISTS)/dmem-io.net

GEN_tc      := --ports ports/tc.json --sim sim/tc.json
GEN_aru     := --ports ports/aru.json
GEN_dmem    := --ports ports/dmem.json --sim sim/dmem.json
GEN_fpc     := --ports ports/fpc.json
GEN_dmem_io := --ports ports/dmem_io.json

GENERATED := $(foreach b,$(BOARDS),generated/$(b)_board.v)
BACKPLANE_VH := out/backplane_contract.vh
PRIMS := prims/prims.v

CHECKS := check-dmem-io-timing check-dmem-read-write \
  check-tc-halt check-tc-single-step check-tc-run-latch \
  check-tc-dmem-single-step check-tc-time-slots check-tc-control-outputs \
  check-tc-aru-register-file check-tc-aru-multiply check-tc-fpc-output \
  check-fpc-bus-interface check-fpc-debug-mux \
  check-aru-fig34-drive check-aru-multiply check-result-register-path

.PHONY: all generate compile check lint clean $(CHECKS) \
  hp5004a hp5004a-tc-pc hp5004a-tc-coeff hp5004a-tc-wcs-lane hp5004a-aru hp5004a-dmem hp5004a-fpc

all: generate compile

generate: $(GENERATED)

# The generator also writes reports/<board>_part_coverage.md (parts -> primitives).
generated/%_board.v: scripts/netlist_to_verilog.py scripts/netlist_lint.py
	@mkdir -p reports
	python3 scripts/netlist_to_verilog.py $(NETLIST_$*) \
	  --out $@ --top $*_board_netlist \
	  --part-report reports/$*_part_coverage.md $(GEN_$*)

generated/tc_board.v: $(NETLIST_tc) ports/tc.json sim/tc.json
generated/aru_board.v: $(NETLIST_aru) ports/aru.json
generated/dmem_board.v: $(NETLIST_dmem) ports/dmem.json sim/dmem.json
generated/fpc_board.v: $(NETLIST_fpc) ports/fpc.json
generated/dmem_io_board.v: $(NETLIST_dmem_io) ports/dmem_io.json

reports/tc_part_coverage.md:
	@mkdir -p reports
	python3 scripts/netlist_to_verilog.py $(NETLIST_tc) --out out/tc_board_regenerated.v \
	  --top tc_board_netlist --part-report $@ $(GEN_tc)

out:
	@mkdir -p out

$(BACKPLANE_VH): contracts/backplane_224x.json tools/gen_backplane_contract.py | out
	python3 tools/gen_backplane_contract.py $< $@

compile: $(GENERATED) | out
	@for b in $(BOARDS); do \
	  printf "%-8s " $$b; \
	  iverilog -g2012 -o out/structural_$${b}.vvp generated/$${b}_board.v $(PRIMS) \
	    && echo OK || exit 1; \
	done

lint:
	python3 tools/lint_netlists.py

# Each check: build a testbench against the generated boards, run it, and let
# a Python checker grade the log. Every checker prints its rules and a total.
check: compile
	@failed=""; for t in $(CHECKS); do \
	  $(MAKE) --no-print-directory $$t || failed="$$failed $$t"; \
	done; \
	if [ -n "$$failed" ]; then echo "FAILED:$$failed"; exit 1; else echo "all $(words $(CHECKS)) checks passed"; fi

check-dmem-io-timing: generated/dmem_io_board.v | out
	iverilog -g2012 -o out/tb_dmem_io_timing.vvp tb/tb_dmem_io_timing.v generated/dmem_io_board.v $(PRIMS)
	vvp out/tb_dmem_io_timing.vvp > out/fig33_run.log 2>&1 || true
	python3 tools/check_dmem_io_timing.py out/fig33_run.log

check-dmem-read-write: generated/dmem_io_board.v generated/dmem_board.v | out
	iverilog -g2012 -o out/tb_dmem_read_write.vvp tb/tb_dmem_read_write.v generated/dmem_io_board.v generated/dmem_board.v $(PRIMS)
	vvp out/tb_dmem_read_write.vvp > out/dmem_rw_run.log 2>&1 || true
	python3 tools/check_dmem_read_write.py out/dmem_rw_run.log

check-tc-halt: generated/tc_board.v | out
	iverilog -g2012 -o out/tb_tc_halt.vvp tb/tb_tc_halt.v generated/tc_board.v $(PRIMS)
	vvp out/tb_tc_halt.vvp > out/tc_halt_probe_run.log 2>&1 || true
	python3 tools/check_tc_halt.py out/tc_halt_probe_run.log

check-tc-single-step: generated/tc_board.v | out
	iverilog -g2012 -o out/tb_tc_single_step.vvp tb/tb_tc_single_step.v generated/tc_board.v $(PRIMS)
	vvp out/tb_tc_single_step.vvp > out/tc_single_step.log 2>&1 || true
	python3 tools/check_tc_single_step.py out/tc_single_step.log

check-tc-run-latch: generated/tc_board.v generated/dmem_io_board.v | out
	iverilog -g2012 -o out/tb_tc_run_latch.vvp tb/tb_tc_run_latch.v \
	  generated/tc_board.v generated/dmem_io_board.v $(PRIMS)
	vvp out/tb_tc_run_latch.vvp > out/tc_run_latch.log 2>&1 || true
	python3 tools/check_tc_run_latch.py out/tc_run_latch.log

check-tc-dmem-single-step: generated/tc_board.v generated/dmem_io_board.v generated/dmem_board.v | out
	iverilog -g2012 -o out/tb_tc_dmem_single_step.vvp tb/tb_tc_dmem_single_step.v \
	  generated/tc_board.v generated/dmem_io_board.v generated/dmem_board.v $(PRIMS)
	vvp out/tb_tc_dmem_single_step.vvp > out/tc_e50_dmem_step_probe_run.log 2>&1 || true
	python3 tools/check_tc_dmem_single_step.py out/tc_e50_dmem_step_probe_run.log

check-tc-time-slots: generated/tc_board.v | out
	iverilog -g2012 -o out/tb_tc_time_slots.vvp tb/tb_tc_time_slots.v generated/tc_board.v $(PRIMS)
	vvp out/tb_tc_time_slots.vvp > out/tc_table1_run.log 2>&1 || true
	python3 tools/check_tc_time_slots.py out/tc_table1_run.log

check-tc-control-outputs: generated/tc_board.v | out
	iverilog -g2012 -o out/tb_tc_control_outputs.vvp tb/tb_tc_control_outputs.v generated/tc_board.v $(PRIMS)
	vvp out/tb_tc_control_outputs.vvp > out/tc_e51_control_probe_run.log 2>/dev/null || true
	python3 tools/check_tc_control_outputs.py out/tc_e51_control_probe_run.log

check-tc-aru-register-file: generated/tc_board.v generated/aru_board.v $(BACKPLANE_VH)
	iverilog -g2012 -o out/tb_tc_aru_register_file.vvp tb/tb_tc_aru_register_file.v \
	  generated/tc_board.v generated/aru_board.v $(PRIMS)
	vvp out/tb_tc_aru_register_file.vvp > out/tc_aru_e51_regfile_probe_run.log 2>/dev/null || true
	python3 tools/check_tc_aru_register_file.py out/tc_aru_e51_regfile_probe_run.log

# The five E8x multiply calls of the firmware's ARU diagnostic, each a 4-row WCS
# image loaded as the RAMs hold it, run through the T&C + ARU stitch.
check-tc-aru-multiply: generated/tc_board.v generated/aru_board.v $(BACKPLANE_VH)
	python3 tools/gen_diag_wcs.py
	iverilog -g2012 -o out/tb_stitch.vvp tb/tb_tc_aru_multiply.v generated/tc_board.v generated/aru_board.v $(PRIMS)
	@for c in 1 2 3 4 5; do \
	  python3 tools/split_wcs_hex.py out/diag_call$$c.hex out --physical >/dev/null; \
	  opset=""; if [ $$c = 5 ]; then opset="+OPSET=2"; fi; \
	  vvp out/tb_stitch.vvp $$opset +WRITE_WCSA=3 2>/dev/null | grep "RR f" > out/stitch_call$$c.log || true; \
	  python3 tools/check_tc_aru_multiply.py $$c out/stitch_call$$c.log || exit 1; \
	done
	@rm -f out/wcs_lane_b*.hex

check-tc-fpc-output: generated/tc_board.v generated/fpc_board.v | out
	python3 tools/gen_fpc_wcs.py
	python3 tools/split_wcs_hex.py out/fpc_stitch.hex out >/dev/null
	iverilog -g2012 -o out/tb_tc_fpc_output.vvp tb/tb_tc_fpc_output.v generated/tc_board.v generated/fpc_board.v $(PRIMS)
	vvp out/tb_tc_fpc_output.vvp 2>/dev/null > out/tc_fpc_stitch_run.log || true
	python3 tools/check_tc_fpc_output.py out/tc_fpc_stitch_run.log
	@rm -f out/wcs_lane_b*.hex

check-fpc-bus-interface: generated/fpc_board.v | out
	iverilog -g2012 -o out/tb_fpc_bus_interface.vvp tb/tb_fpc_bus_interface.v generated/fpc_board.v $(PRIMS)
	vvp out/tb_fpc_bus_interface.vvp 2>/dev/null > out/fpc_dab_boundary_run.log || true
	python3 tools/check_fpc_bus_interface.py out/fpc_dab_boundary_run.log

check-fpc-debug-mux: generated/fpc_board.v | out
	iverilog -g2012 -o out/tb_fpc_debug_mux.vvp tb/tb_fpc_debug_mux.v generated/fpc_board.v $(PRIMS)
	vvp out/tb_fpc_debug_mux.vvp 2>/dev/null > out/fpc_debug_mux_run.log || true
	python3 tools/check_fpc_debug_mux.py out/fpc_debug_mux_run.log

check-aru-fig34-drive: generated/aru_board.v | out
	python3 -c "exec(open('tools/gen_e8x_cases.py').read())"
	iverilog -g2012 -o out/tb_aru_fig34_drive.vvp tb/tb_aru_fig34_drive.v generated/aru_board.v $(PRIMS)
	vvp out/tb_aru_fig34_drive.vvp 2>/dev/null | grep ARES > out/aru_e8x_run.log || true
	python3 tools/check_aru_fig34_drive.py out/aru_e8x_run.log

check-aru-multiply: generated/aru_board.v | out
	python3 -c "exec(open('tools/gen_e8x_cases.py').read())"
	iverilog -g2012 -o out/tb_aru_multiply.vvp tb/tb_aru_multiply.v generated/aru_board.v $(PRIMS)
	vvp out/tb_aru_multiply.vvp 2>/dev/null | grep ECTL > out/aru_e8ctl_run.log || true
	python3 tools/check_aru_multiply.py out/aru_e8ctl_run.log

check-result-register-path: generated/tc_board.v generated/aru_board.v reports/tc_part_coverage.md
	python3 tools/check_result_register_path.py

# ---------------------------------------------------------------- HP 5004A
# The service manual's signature tables (224X service manual v8.2.1, chapter 5).
# The T&C program-counter island needs nothing else; the others need the WCS
# images of Diagnostic Programs 3 and 6, captured from your own firmware:
#   python3 ../emulator/diag_captures.py "$LEXICON_FIRMWARE/224X v8_1"

HP := PYTHONPATH=hp5004a python3

hp5004a: hp5004a-tc-pc hp5004a-tc-coeff hp5004a-tc-wcs-lane hp5004a-aru hp5004a-fpc hp5004a-dmem

hp5004a-tc-pc:
	$(HP) hp5004a/check_hp5004a_tc_u1_p14.py

hp5004a-tc-coeff:
	$(HP) hp5004a/check_hp5004a_tc_u11.py

hp5004a-tc-wcs-lane:
	$(HP) hp5004a/check_hp5004a_tc_wcs_lane.py

hp5004a-aru:
	$(HP) hp5004a/check_hp5004a_aru.py

hp5004a-fpc:
	$(HP) hp5004a/check_hp5004a_fpc.py

# About 4 minutes.
hp5004a-dmem:
	$(HP) hp5004a/check_hp5004a_dmem.py

clean:
	rm -rf out reports
