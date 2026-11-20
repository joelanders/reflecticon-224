# Build and check every folder. Each keeps its own tool (make, run.py,
# build.py, CMake); this file only says what depends on what.
#
#   make help              the targets
#   make check             every check that needs no firmware
#   make check-firmware    the checks that run your ROMs (see README.md, "Your firmware")
#
# A target whose tool is missing prints "skipped" and succeeds, so each folder
# can be checked with only its own requirements installed.

PYTHON ?= python3
LEXICON_FIRMWARE ?= $(CURDIR)/firmware
KICAD_CLI ?= /Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
export LEXICON_FIRMWARE KICAD_CLI

SHELL := bash

# the four programs the firmware writes into WCS, for the board and ISA-level comparisons
CAPTURED := captures/xl_max_delay_combo.bin captures/m_band_delay.bin captures/chorus_echo.bin captures/call_prog4.bin

# Each recipe is one shell line (this works with the make 3.81 that macOS ships):
# a missing requirement prints "skipped" (and notes it for the summary at the
# end of `make check`) and ends the recipe with success; then `set -ex` shows
# each command and stops at the first failure. `make` here is a task runner:
# each folder's own tool decides what is out of date.
SKIPPED := .make-skipped
skip = { echo "skipped $@: $(1)" | tee -a $(SKIPPED); exit 0; }
need = command -v $(1) >/dev/null || $(call skip,$(1) not found)
need_verilator = command -v verilator >/dev/null || test -x "$${VERILATOR_HOME:-}/bin/verilator" || $(call skip,Verilator 5 not found (PATH or VERILATOR_HOME))
need_firmware = test -d "$(LEXICON_FIRMWARE)/$(1)" || $(call skip,no firmware set '$(1)' in $(LEXICON_FIRMWARE))
need_captured = for f in $(CAPTURED); do test -f $$f || $(call skip,$$f missing (captured programs; see README.md)); done
summary = echo; if test -s $(SKIPPED); then echo "Skipped:"; sed 's/^skipped /  /' $(SKIPPED); else echo "Nothing skipped."; fi

.PHONY: help all check check-firmware clean skip-reset \
  schematics-check gate-level gate-level-check diag-captures gate-level-hp5004a \
  board-level board-level-check board-level-programs board-level-references \
  isa-level isa-level-check isa-level-cpp-check isa-level-programs \
  8080-check 8080-exercisers emulator emulator-check emulator-qualify emulator-qualify-full analog-check \
  web-demo web-demo-soak juce-plugin juce-plugin-check

help:
	@echo "make check            every firmware-free check"
	@echo "make check-firmware   the checks that run your ROMs (LEXICON_FIRMWARE=$(LEXICON_FIRMWARE))"
	@echo "make all              build everything"
	@echo ""
	@echo "schematics-check      the netlists are what KiCad exports from the schematics"
	@echo "gate-level-check      gate-level Verilog from the netlists, and its checks"
	@echo "gate-level-hp5004a    the service manual's signature tables (firmware)"
	@echo "board-level-check     the board-level machine and its example"
	@echo "board-level-programs  the captured programs (firmware captures)"
	@echo "isa-level-check       the ISA-level DSP against the board and gate levels"
	@echo "isa-level-cpp-check   the C++ DSP alone, with only a compiler"
	@echo "isa-level-programs    the captured programs against the board level (firmware captures)"
	@echo "8080-check            the 8080 core's tests and the quick CP/M exercisers (fetched once)"
	@echo "8080-exercisers       all four exercisers, including 8080EXM (about 95 s)"
	@echo "emulator-check        the emulator's diagnostic ports and bus timing"
	@echo "emulator-qualify      the emulator's firmware runs are the ones that qualified (firmware)"
	@echo "emulator-qualify-full the full comparison with the board-level machine (firmware, about 10 min)"
	@echo "analog-check          the analog filters regenerate from their SPICE netlists"
	@echo "web-demo, web-demo-soak (firmware)"
	@echo "juce-plugin, juce-plugin-check (firmware)"

all: gate-level board-level isa-level emulator web-demo juce-plugin

check: skip-reset schematics-check gate-level-check board-level-check isa-level-check isa-level-cpp-check \
  8080-check emulator-check analog-check web-demo
	@$(summary)

check-firmware: skip-reset gate-level-hp5004a board-level-programs isa-level-programs emulator-qualify \
  web-demo-soak juce-plugin-check
	@$(summary)

skip-reset:
	@rm -f $(SKIPPED)

# ---- schematics -> netlists

schematics-check:
	@test -x "$(KICAD_CLI)" || { echo "skipped $@: kicad-cli not found (set KICAD_CLI)"; exit 0; }; \
	set -ex; \
	cd schematics && $(PYTHON) check_netlists.py

# ---- netlists -> gate-level Verilog (gate-level-verilog/Makefile has the file rules)

gate-level:
	@$(call need,iverilog); \
	set -ex; \
	$(MAKE) -C gate-level-verilog all

# gate-level-check ends by proving the committed Verilog is exactly what the
# netlists generate (in a git checkout)
gate-level-check: gate-level
	@$(call need,iverilog); \
	set -ex; \
	$(MAKE) -C gate-level-verilog check lint; \
	if git rev-parse --git-dir >/dev/null 2>&1; then \
	  $(MAKE) -B -C gate-level-verilog generate >/dev/null; \
	  git diff --exit-code --stat -- gate-level-verilog/generated; \
	  echo "gate-level-verilog/generated reproduces from the netlists"; \
	fi

# The signature tables were measured with the firmware running a diagnostic
# program; the emulator runs your firmware to make those captures.
diag-captures: emulator
	@$(call need_firmware,224X v8_1); \
	set -ex; \
	$(PYTHON) emulator/diag_captures.py "$(LEXICON_FIRMWARE)/224X v8_1"

gate-level-hp5004a: gate-level diag-captures
	@$(call need,iverilog); \
	$(call need_firmware,224X v8_1); \
	set -ex; \
	$(MAKE) -C gate-level-verilog hp5004a

# ---- the board-level machine

board-level:
	@$(call need_verilator); \
	set -ex; \
	cd board-level-verilog; \
	$(PYTHON) run.py build-static; \
	$(PYTHON) run.py build-machine --grade 1

board-level-check: board-level
	@$(call need_verilator); \
	set -ex; \
	cd board-level-verilog && $(PYTHON) run.py example

# The board-level runs the emulator's full qualification compares against
# (about 10 minutes).
board-level-references: board-level
	@$(call need_verilator); \
	$(call need_firmware,224X v8_1); \
	set -ex; \
	cd board-level-verilog; \
	$(PYTHON) run.py build-machine; \
	build/machine-0/Vmachine_host factory "$(LEXICON_FIRMWARE)/224X v8_1" 18000000 build/v81-boot-final > build/v81-boot-final.log; \
	build/machine-0/Vmachine_host factory "$(LEXICON_FIRMWARE)/224X v8_1" 5000000 build/v81-max-delay-final examples/v81-max-delay.controls > build/v81-max-delay-final.log

board-level-programs: board-level
	@$(call need_verilator); \
	$(call need_captured); \
	set -ex; \
	cd board-level-verilog && $(PYTHON) run.py run-programs --images ../captures

# ---- the ISA-level DSP (SystemVerilog and C++), checked against the
# board-level machine and the gate-level netlists

isa-level:
	@$(call need_verilator); \
	set -ex; \
	cd isa-level-verilog && $(PYTHON) run.py build

isa-level-check: isa-level board-level gate-level
	@$(call need_verilator); \
	$(call need,iverilog); \
	set -ex; \
	cd isa-level-verilog; \
	$(PYTHON) tests/arith_exhaustive.py; \
	$(PYTHON) tests/fuzz.py --programs 20 --rows 65536 --seed 1; \
	$(PYTHON) tests/gate_level.py --random 10 --netlist-xreg-race; \
	for p in products chains shifts; do $(PYTHON) tests/gate_arith.py --program $$p --operands edges; done

isa-level-cpp-check:
	@$(call need,clang++); \
	set -ex; \
	cd isa-level-cpp && $(PYTHON) tests/check_programs.py

isa-level-programs: isa-level board-level-programs
	@$(call need_verilator); \
	$(call need_captured); \
	set -ex; \
	cd isa-level-verilog && $(PYTHON) run.py check

# ---- the 8080 core

8080-check:
	@$(call need,cc); \
	set -ex; \
	cd 8080 && mkdir -p build; \
	for t in smoke interrupts dasm; do cc -std=c11 -Wall -Wextra -I. -o build/$$t tests/$$t.c && build/$$t; done; \
	$(PYTHON) tests/exercisers.py

8080-exercisers:
	@$(call need,cc); \
	set -ex; \
	cd 8080 && $(PYTHON) tests/exercisers.py --full

# ---- the emulator: the whole machine in C++

emulator:
	@set -ex; \
	$(MAKE) -C emulator

emulator-check: emulator board-level
	@$(call need_verilator); \
	set -ex; \
	cd emulator; \
	$(PYTHON) tests/check_dports.py ../board-level-verilog/build/machine-1/Vmachine_host --seeds 2; \
	$(PYTHON) tests/check_timeline.py

emulator-qualify: emulator
	@$(call need_firmware,224X v8_1); \
	set -ex; \
	cd emulator && $(PYTHON) tests/check_qualified.py

emulator-qualify-full: emulator board-level-references
	@$(call need_firmware,224X v8_1); \
	set -ex; \
	cd emulator; \
	$(PYTHON) tests/qualify_firmware.py boot; \
	$(PYTHON) tests/qualify_firmware.py max-delay

# ---- the analog boards

analog-check:
	@$(call need,ngspice); \
	set -ex; \
	cd analog && sh tools/build_filters.sh; \
	if git rev-parse --git-dir >/dev/null 2>&1; then \
	  git diff --exit-code --stat -- filters.hpp filters_224.hpp; \
	  echo "analog filters reproduce from their SPICE netlists"; \
	fi

# ---- the web demo

web-demo:
	@$(call need,emcc); \
	set -ex; \
	cd web-demo && $(PYTHON) build.py

web-demo-soak: web-demo
	@$(call need,emcc); \
	$(call need,node); \
	$(call need_firmware,224XL v8_21); \
	set -ex; \
	cd web-demo && node tests/soak.mjs "$(LEXICON_FIRMWARE)/224XL v8_21" --actions 20 --seed 1

# ---- the JUCE plugin (work in progress; JUCE is fetched unless JUCE_DIR is set)

juce-plugin:
	@$(call need,cmake); \
	set -ex; \
	cd juce-plugin; \
	cmake -S . -B build -G "Unix Makefiles" -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
	  $(if $(JUCE_DIR),-DFETCHCONTENT_SOURCE_DIR_JUCE="$(JUCE_DIR)"); \
	cmake --build build -j 8

juce-plugin-check: juce-plugin
	@$(call need,cmake); \
	$(call need_firmware,224XL v8_21); \
	set -ex; \
	cd juce-plugin && build/byte_exact_artefacts/Release/byte_exact "$(LEXICON_FIRMWARE)/224XL v8_21" tests/timelines/xl821_1.events

clean:
	@set -ex; \
	$(MAKE) -C gate-level-verilog clean; \
	$(MAKE) -C emulator clean; \
	rm -f $(SKIPPED); \
	rm -rf board-level-verilog/build isa-level-verilog/build isa-level-cpp/build 8080/build analog/build web-demo/build juce-plugin/build; \
	rm -f web-demo/page/lexicon224x.js web-demo/page/lexicon224x.wasm
