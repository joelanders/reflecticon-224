# The Rack plugin (make, make install) and the core check that needs no SDK
# (make check ROMS=<folder of one firmware set>).
RACK_DIR ?= Rack-SDK

FLAGS += -I../juce-plugin/source -I../juce-plugin/plugin -I../analog
SOURCES += src/plugin.cpp src/Reflexion224.cpp src/plugin_sources.cpp
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

ifneq ($(wildcard $(RACK_DIR)/plugin.mk),)
include $(RACK_DIR)/plugin.mk
# After plugin.mk, so these come last on the command line and win: the engine
# needs C++20, and the analog boards' floating point must not be reassociated
# or contracted (the output is compared bit for bit with the web demo's).
CXXFLAGS += -std=c++20
FLAGS += -fno-unsafe-math-optimizations -ffp-contract=off
# Rack targets macOS 10.9; std::filesystem (rom_loading.hpp) needs 10.15.
ifdef ARCH_MAC
FLAGS += -mmacosx-version-min=10.15
LDFLAGS += -mmacosx-version-min=10.15
endif
# MinGW's libgcc and winpthread (std::thread) would otherwise be DLLs next to
# nothing: Rack loads plugin.dll from the plugin's folder, so link them in.
ifdef ARCH_WIN
LDFLAGS += -static-libgcc -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
endif
else
$(info no Rack SDK at $(RACK_DIR): only `make check` is available)
endif

ROMS ?= $(LEXICON224_ROMPATH)
build/core_check: tests/core_check.cpp src/core.hpp src/denormals.hpp src/plugin_sources.cpp
	@mkdir -p build
	clang++ -std=c++20 -O3 -g -DNDEBUG -ffp-contract=off -Wall -Wextra \
		-I../juce-plugin/source -I../juce-plugin/plugin -I../analog \
		$< src/plugin_sources.cpp -o $@

# The core: bit-identical without a catalog, then operated through it.
check: build/core_check
	build/core_check "$(ROMS)" 10 res/catalogs

# The catalogs (program names, pages, slider tables) from the web demo and
# the JUCE plugin, and the sliders' manual help (tooltips), into res/
# (committed; refresh with make catalogs).
catalogs:
	mkdir -p res/catalogs/stored
	cp ../web-demo/page/catalogs/*.json ../juce-plugin/catalogs-extra/*.json res/catalogs/
	cp ../juce-plugin/catalogs-extra/stored/*.json res/catalogs/stored/
	cp ../web-demo/page/param_help.json res/

# The module class itself, linked against the SDK's libRack, without Rack running.
build/module_smoke: tests/module_smoke.cpp src/Reflexion224.cpp src/core.hpp src/denormals.hpp src/plugin_sources.cpp
	@mkdir -p build
	clang++ -std=c++20 -O2 -g -stdlib=libc++ -fno-unsafe-math-optimizations -ffp-contract=off \
		-mmacosx-version-min=10.15 -DARCH_MAC -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		-I../juce-plugin/source -I../juce-plugin/plugin -I../analog -Isrc \
		$< src/plugin_sources.cpp -L$(RACK_DIR) -lRack -o $@

smoke: build/module_smoke
	DYLD_LIBRARY_PATH=$(RACK_DIR) build/module_smoke "$(ROMS)"

.PHONY: check smoke catalogs
