# The emulator's programs, built one way for every script that needs them.
#
#   make                  all of them
#   make build/factory    ROMs, control script and cycle count in; logs out
#
#   factory      runs the firmware (the same logs as the board-level machine's factory)
#   probe        scripts the LARC or front panel and reads the display
#   timeline     the bus-timing pictures (tests/check_timeline.py)
#   diag_state   the diagnostic-program captures (diag_captures.py)

CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O2
PROGRAMS := factory probe timeline diag_state
HEADERS := $(wildcard *.hpp) ../isa-level-cpp/lexicon224x.hpp ../sbc/memory_map.hpp ../sbc/peripherals.hpp ../8080/i8080.h

.PHONY: all clean
all: $(addprefix build/,$(PROGRAMS))

build/%: %.cpp $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -rf build
