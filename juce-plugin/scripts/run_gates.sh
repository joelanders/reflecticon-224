#!/usr/bin/env bash
#
# run_gates.sh — the plugin's validation battery, one command, one scoreboard.
#
#   scripts/run_gates.sh FIRMWARE_ROOT [options]
#
# FIRMWARE_ROOT is the folder with one sub-folder per firmware set, named as
# in the user's collection ("224 v4.3", "224X v8_1", "224XL v8_21", ...). The
# ROM bytes are only read from there, never copied (except by the roms test,
# which builds its fixtures under a scratch folder outside the repo).
#
# Gates (in order):
#   build        configure (if needed) and build the plugin and tests
#   names        the product name agrees in CMakeLists.txt, the .command, rom_library.h
#   sign         codesign --verify --deep --strict on every bundle
#   bridge       bridge_check (the rate bridge alone, no ROM)
#   byteexact    byte_exact on every timeline in tests/timelines/ (plugin vs
#                web-demo/wasm/web.cpp's lex_render loop; ~1 min per timeline)
#   rtalloc      rt_alloc on one timeline: nothing may be allocated or freed
#                in processBlock (patches/ landed 2026-10-14)
#   offline      offline_realtime per set (224XL v8_21, 224X v8_1, 224 v4.3):
#                one automation timeline in five host block schemes, offline
#                and realtime, bit-identical; plus the state round trip and
#                the restore with the set missing (--state-missing)
#   roundtrip    offline_realtime --round-trip on every set folder present
#                (the 224 v4.3/v4.4/v3.2/TEST, 224X, both 224XLs): random
#                program, variation and slider moves, save, restore into a
#                fresh instance, the stored bytes identical
#   stored       tests/stored_tables/run.sh (the measured stored-byte tables
#                in catalogs-extra/stored/ against the catalogs)
#   catalog      tests/catalog/run.sh (catalog model vs the web demo's code)
#   roms         tests/roms/run_tests.sh (needs build-c/ from tests/roms/CMakeLists.txt)
#   pluginval    strictness 5, then 10, VST3 (by bundle path) and AU (only if
#                installed: pluginval and auval find AUs through the system registry)
#   auval        auval -v aufx <PLUGIN_CODE> <MANUFACTURER_CODE> (only if installed)
#   soak         opt-in (--with-soak): tests/soak/run_pair.sh, one seed
#
# Options:
#   --build-dir DIR   build directory (default build-h; configured with Ninja when
#                     ninja is on PATH, else Unix Makefiles, if it has no
#                     CMakeCache.txt)
#   --no-build        use the build directory as it is
#   --only LIST       comma list of gates to run (names above; build is implied
#                     unless --no-build)
#   --skip LIST       comma list of gates to skip
#   --timelines LIST  comma list of timeline names for byteexact (default: all)
#   --seeds LIST      comma list of --round-trip seeds per set (default 1,2,3)
#   --rom-set NAME    the set pluginval/auval boot (default "224XL v8_21")
#   --pluginval-levels LIST  default 5,10
#   --skip-gui-tests  passed to pluginval (headless machines)
#   --install-au      copy the AU into ~/Library/Audio/Plug-Ins/Components first
#                     (explicit opt-in; nothing is ever installed otherwise)
#   --uninstall-au    remove that copy and exit
#   --with-soak       also run one soak seed pair (C++ vs JS operator)
#   --strict          count KNOWN-FAIL gates as failures
#   --out DIR         keep logs in DIR (default: a fresh mktemp folder, kept)
#
# Exit status: 0 if no gate FAILed, 1 otherwise, 2 on usage errors.
# Environment: JUCE_DIR (default /path/to/JUCE) for the
# first configure; set JUCE_DIR= (empty) to fetch JUCE 8.0.14 from GitHub instead.
#
# macOS bash 3.2 compatible (no associative arrays, no mapfile).
set -uo pipefail

PLUGIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PLUGIN_DIR"

usage() { sed -n '3,62p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

# ---------------- identity, from CMakeLists.txt (never duplicated here) ----------------
cmake_value() { # cmake_value NAME -> the value of set(NAME value) in CMakeLists.txt
	sed -n "s/^set($1[[:space:]]\{1,\}\"\{0,1\}\([^\")]*\)\"\{0,1\})[[:space:]]*$/\1/p" CMakeLists.txt | head -1
}
PRODUCT="$(cmake_value PLUGIN_PRODUCT_NAME)"
TARGET="$(cmake_value PLUGIN_TARGET)"
MFR_CODE="$(cmake_value PLUGIN_MANUFACTURER_CODE)"
PLUG_CODE="$(cmake_value PLUGIN_CODE)"
if [[ -z "$PRODUCT" || -z "$TARGET" || -z "$MFR_CODE" || -z "$PLUG_CODE" ]]; then
	echo "error: could not read the plugin identity from CMakeLists.txt" >&2
	exit 2
fi
AU_INSTALLED="$HOME/Library/Audio/Plug-Ins/Components/$PRODUCT.component"

# ---------------- arguments ----------------
FW=""
BUILD_DIR="build-h"
DO_BUILD=1
ONLY=""
SKIP=""
TIMELINES=""
SEEDS="1,2,3"
ROM_SET="224XL v8_21"
PV_LEVELS="5,10"
PV_GUI=()
INSTALL_AU=0
WITH_SOAK=0
STRICT=0
OUT=""
while [[ $# -gt 0 ]]; do
	case "$1" in
		--build-dir) BUILD_DIR="$2"; shift 2 ;;
		--no-build) DO_BUILD=0; shift ;;
		--only) ONLY="$2"; shift 2 ;;
		--skip) SKIP="$2"; shift 2 ;;
		--timelines) TIMELINES="$2"; shift 2 ;;
		--seeds) SEEDS="$2"; shift 2 ;;
		--rom-set) ROM_SET="$2"; shift 2 ;;
		--pluginval-levels) PV_LEVELS="$2"; shift 2 ;;
		--skip-gui-tests) PV_GUI=(--skip-gui-tests); shift ;;
		--install-au) INSTALL_AU=1; shift ;;
		--uninstall-au)
			if [[ -d "$AU_INSTALLED" ]]; then
				rm -rf "$AU_INSTALLED" && echo "removed $AU_INSTALLED"
				killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
			else
				echo "nothing to remove: $AU_INSTALLED does not exist"
			fi
			exit 0 ;;
		--with-soak) WITH_SOAK=1; shift ;;
		--strict) STRICT=1; shift ;;
		--out) OUT="$2"; shift 2 ;;
		-h|--help) usage ;;
		-*) echo "unknown option $1" >&2; usage ;;
		*) if [[ -z "$FW" ]]; then FW="$1"; shift; else echo "unexpected argument $1" >&2; usage; fi ;;
	esac
done
if [[ -z "$FW" ]]; then usage; fi
if [[ ! -d "$FW" ]]; then echo "error: FIRMWARE_ROOT $FW is not a folder" >&2; exit 2; fi
FW="$(cd "$FW" && pwd)"
case "$BUILD_DIR" in /*) ;; *) BUILD_DIR="$PLUGIN_DIR/$BUILD_DIR" ;; esac

# No trailing slash: the ROM test compares its scratch path by exact prefix,
# and macOS's TMPDIR ends in "/".
TMPDIR_CLEAN="${TMPDIR:-/tmp}"
TMPDIR_CLEAN="$(cd "$TMPDIR_CLEAN" && pwd -P)"
if [[ -z "$OUT" ]]; then
	OUT="$(mktemp -d "${TMPDIR_CLEAN}/lexplug_gates.XXXXXX")"
else
	mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
fi

in_list() { # in_list ITEM COMMA_LIST
	case ",$2," in *",$1,"*) return 0 ;; esac
	return 1
}
wanted() { # wanted GATE -> run it?
	if [[ -n "$ONLY" ]] && ! in_list "$1" "$ONLY"; then return 1; fi
	if [[ -n "$SKIP" ]] && in_list "$1" "$SKIP"; then return 1; fi
	return 0
}

# A per-command time limit (the machine boots for 16 s of machine time per run).
if command -v timeout >/dev/null 2>&1; then
	limit() { timeout "$@"; }
elif command -v gtimeout >/dev/null 2>&1; then
	limit() { gtimeout "$@"; }
else
	limit() { local s="$1"; shift; perl -e 'alarm shift; exec @ARGV' "$s" "$@"; }
fi

# ---------------- scoreboard ----------------
RESULTS=()   # "STATUS|gate|detail"
note() { printf '%s\n' "$*" >&2; }
record() { # record STATUS GATE DETAIL   (STATUS: PASS FAIL KNOWN-FAIL SKIP)
	RESULTS+=("$1|$2|$3")
	printf '  %-10s %-22s %s\n' "$1" "$2" "$3" >&2
}

note "plugin: $PRODUCT (target $TARGET, AU aufx $PLUG_CODE $MFR_CODE)"
note "firmware root: $FW"
note "build dir: $BUILD_DIR"
note "logs: $OUT"

ARTEFACTS="$BUILD_DIR/${TARGET}_artefacts/Release"
VST3="$ARTEFACTS/VST3/$PRODUCT.vst3"
AU="$ARTEFACTS/AU/$PRODUCT.component"
APP="$ARTEFACTS/Standalone/$PRODUCT.app"
BYTE_EXACT="$BUILD_DIR/byte_exact_artefacts/Release/byte_exact"
RT_ALLOC="$BUILD_DIR/rt_alloc_artefacts/Release/rt_alloc"
BRIDGE="$BUILD_DIR/bridge_check"
OFFLINE="$BUILD_DIR/offline_realtime_artefacts/Release/offline_realtime"

# ---------------- build ----------------
if [[ $DO_BUILD == 1 ]]; then
	note "== build =="
	ok=1
	if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
		JUCE_ARG=()
		JUCE_LOCAL="${JUCE_DIR-/path/to/JUCE}"
		if [[ -n "$JUCE_LOCAL" && -d "$JUCE_LOCAL" ]]; then
			JUCE_ARG=(-DFETCHCONTENT_SOURCE_DIR_JUCE="$JUCE_LOCAL")
		fi
		GENERATOR="Unix Makefiles"
		if command -v ninja >/dev/null 2>&1; then GENERATOR="Ninja"; fi
		cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
			${JUCE_ARG[@]+"${JUCE_ARG[@]}"} > "$OUT/configure.log" 2>&1 || ok=0
	fi
	if [[ $ok == 1 ]]; then
		cmake --build "$BUILD_DIR" -j "$(sysctl -n hw.ncpu)" > "$OUT/build.log" 2>&1 || ok=0
	fi
	if [[ $ok == 1 ]]; then record PASS build "$BUILD_DIR"
	else record FAIL build "see $OUT/configure.log / $OUT/build.log"; fi
fi

# ---------------- names: the product name in the places that cannot read CMakeLists.txt ----------------
if wanted names; then
	note "== product name agrees across files =="
	cmd_name="$(sed -n 's/^PRODUCT="\(.*\)"$/\1/p' scripts/macsetup_remove_quarantine.command)"
	lib_name="$(sed -n 's/.*productFolder = "\(.*\)";.*/\1/p' source/roms/rom_library.h)"
	if [[ "$cmd_name" == "$PRODUCT" && "$lib_name" == "$PRODUCT" ]]; then
		record PASS names "$PRODUCT in CMakeLists.txt, macsetup .command, rom_library.h"
	else
		record FAIL names "CMakeLists.txt '$PRODUCT', macsetup .command '$cmd_name', rom_library.h productFolder '$lib_name'"
	fi
fi

# ---------------- signatures ----------------
if wanted sign; then
	note "== codesign --verify --deep --strict =="
	for b in "$VST3" "$AU" "$APP"; do
		name="sign-$(basename "$b")"
		if [[ ! -d "$b" ]]; then record FAIL "$name" "missing: $b"; continue; fi
		if codesign --verify --deep --strict "$b" > "$OUT/$name.log" 2>&1; then
			record PASS "$name" "$(codesign -dv "$b" 2>&1 | grep '^Signature=' | head -1)"
		else
			record FAIL "$name" "$(tail -1 "$OUT/$name.log") (is scripts/sign_bundle.cmake wired into CMakeLists.txt?)"
		fi
	done
fi

# ---------------- bridge_check ----------------
if wanted bridge; then
	note "== bridge_check =="
	if [[ -x "$BRIDGE" ]] && limit 300 "$BRIDGE" > "$OUT/bridge_check.log" 2>&1; then
		record PASS bridge "$(tail -1 "$OUT/bridge_check.log")"
	else
		record FAIL bridge "see $OUT/bridge_check.log"
	fi
fi

# ---------------- timelines: name prefix -> firmware set folder ----------------
set_for_timeline() {
	case "$1" in
		v43_*) echo "224 v4.3" ;;
		v44_*) echo "224 v4_4" ;;
		x81_*) echo "224X v8_1" ;;
		xl81a_*) echo "224XL v8_1A" ;;
		xl821_*) echo "224XL v8_21" ;;
		*) echo "" ;;
	esac
}
TL_DIR="tests/timelines"
if [[ -z "$TIMELINES" ]]; then
	TIMELINES="$(ls "$TL_DIR"/*.events 2>/dev/null | sed 's#.*/##; s#\.events$##' | paste -sd, -)"
fi

# ---------------- byte_exact ----------------
if wanted byteexact; then
	note "== byte_exact (plugin vs reference loop, ~1 min per timeline) =="
	IFS=, read -r -a tls <<< "$TIMELINES"
	for tl in ${tls[@]+"${tls[@]}"}; do
		set_dir="$(set_for_timeline "$tl")"
		if [[ -z "$set_dir" || ! -d "$FW/$set_dir" ]]; then
			record SKIP "byteexact-$tl" "no firmware folder for this timeline ($set_dir)"; continue
		fi
		log="$OUT/byte_exact_$tl.log"
		if [[ -x "$BYTE_EXACT" ]] && limit 900 "$BYTE_EXACT" "$FW/$set_dir" "$TL_DIR/$tl.events" > "$log" 2>&1 \
			&& tail -1 "$log" | grep -q 'byte_exact: PASS'; then
			record PASS "byteexact-$tl" "$set_dir: $(grep -c 'bit-identical' "$log") channel runs bit-identical"
		else
			record FAIL "byteexact-$tl" "$set_dir: see $log"
		fi
	done
fi

# ---------------- rt_alloc (no allocation in processBlock) ----------------
if wanted rtalloc; then
	note "== rt_alloc (allocations inside processBlock) =="
	tl="xl821_1"
	log="$OUT/rt_alloc_$tl.log"
	if [[ -x "$RT_ALLOC" ]] && limit 900 "$RT_ALLOC" "$FW/224XL v8_21" "$TL_DIR/$tl.events" --tail 10 > "$log" 2>&1; then
		record PASS rtalloc "$tl: nothing allocated in processBlock"
	else
		counts="$(grep -iE '^ *(new|delete|operator new|total)' "$log" | head -2 | tr -s ' ' | paste -sd';' -)"
		if [[ -x "$RT_ALLOC" ]] && tail -1 "$log" | grep -q 'rt_alloc: FAIL'; then
			record FAIL rtalloc "$tl: allocates in processBlock ${counts:+($counts)} — $log"
		else
			record FAIL rtalloc "did not run to a verdict: see $log"
		fi
	fi
fi

# ---------------- offline == realtime (and the state checks) ----------------
if wanted offline; then
	note "== offline_realtime (block schemes, offline and realtime, bit-identical; state round trip) =="
	nowhere="$(mktemp -d "${TMPDIR_CLEAN}/lexplug_noroms.XXXXXX")"   # a ROM location without the set
	for set_dir in "224XL v8_21" "224X v8_1" "224 v4.3"; do
		name="offline-$(echo "$set_dir" | tr ' ' '_')"
		if [[ ! -d "$FW/$set_dir" ]]; then record SKIP "$name" "no firmware folder $set_dir"; continue; fi
		log="$OUT/$name.log"
		if [[ -x "$OFFLINE" ]] && limit 900 "$OFFLINE" "$FW/$set_dir" --state-missing "$nowhere" > "$log" 2>&1 \
			&& tail -1 "$log" | grep -q 'PASS offline == realtime'; then
			record PASS "$name" "$(grep -c ' identical$' "$log") scheme runs identical; $(grep -m1 'state: restored' "$log" | sed 's/^state: //')"
		else
			record FAIL "$name" "$(grep -m1 -E 'DIFFERS|DIFFER|NO$|timed out|no machine' "$log" | cut -c1-100) — $log"
		fi
	done
	rm -rf "$nowhere"
fi

# ---------------- state round trip with slider moves, every set ----------------
if wanted roundtrip; then
	note "== state round trip (random program, variation, slider moves; save; restore; same stored bytes) =="
	seed_args=()
	IFS=, read -r -a seeds <<< "$SEEDS"
	for seed in ${seeds[@]+"${seeds[@]}"}; do seed_args+=(--round-trip "$seed"); done
	for set_dir in "224 v4.3" "224 v4_4" "224 v3_2" "224 TEST" "224X v8_1" "224XL v8_1A" "224XL v8_21"; do
		name="roundtrip-$(echo "$set_dir" | tr ' ' '_')"
		if [[ ! -d "$FW/$set_dir" ]]; then record SKIP "$name" "no firmware folder $set_dir"; continue; fi
		log="$OUT/$name.log"
		if [[ -x "$OFFLINE" ]] && limit 900 "$OFFLINE" "$FW/$set_dir" --skip-schemes ${seed_args[@]+"${seed_args[@]}"} > "$log" 2>&1 \
			&& tail -1 "$log" | grep -q 'PASS offline == realtime'; then
			record PASS "$name" "$(grep -c 'round trip seed .*: PASS' "$log") seeds: stored bytes identical after restore"
		else
			record FAIL "$name" "$(grep -m1 -E 'DIFFER|FAIL|did not' "$log" | cut -c1-110) — $log"
		fi
	done
fi

# ---------------- measured stored-byte tables ----------------
if wanted stored; then
	note "== stored-byte tables (catalogs-extra/stored/) vs the catalogs =="
	if limit 300 sh tests/stored_tables/run.sh > "$OUT/stored.log" 2>&1 && tail -1 "$OUT/stored.log" | grep -q 'PASS stored tables'; then
		record PASS stored "$(grep -c 'sliders attached' "$OUT/stored.log") sidecars; $(tail -2 "$OUT/stored.log" | head -1)"
	else
		record FAIL stored "see $OUT/stored.log"
	fi
fi

# ---------------- catalog ----------------
if wanted catalog; then
	note "== catalog model vs the web demo =="
	if limit 600 sh tests/catalog/run.sh > "$OUT/catalog.log" 2>&1; then
		record PASS catalog "$(grep -iE 'pass|ok' "$OUT/catalog.log" | tail -1)"
	else
		record FAIL catalog "see $OUT/catalog.log"
	fi
fi

# ---------------- roms ----------------
if wanted roms; then
	note "== ROM discovery and recognition =="
	if [[ ! -x build-c/test_roms_core || ! -x build-c/test_rom_library_artefacts/Release/test_rom_library ]]; then
		record SKIP roms "build-c/ not built: cmake -S tests/roms -B build-c && cmake --build build-c"
	else
		scratch="$(mktemp -d "${TMPDIR_CLEAN}/lexplug_roms.XXXXXX")"   # fixtures (copies of chips) stay outside the repo
		if limit 900 sh tests/roms/run_tests.sh "$FW" "$scratch" > "$OUT/roms.log" 2>&1; then
			record PASS roms "$(grep '== web findSets' "$OUT/roms.log" | tr -s ' ' | paste -sd';' -)"
		else
			record FAIL roms "see $OUT/roms.log"
		fi
		rm -rf "$scratch"
	fi
fi

# ---------------- AU install (explicit opt-in only) ----------------
if [[ $INSTALL_AU == 1 ]]; then
	note "== install AU (--install-au) =="
	mkdir -p "$(dirname "$AU_INSTALLED")"
	rm -rf "$AU_INSTALLED"
	if ditto "$AU" "$AU_INSTALLED"; then
		killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
		note "  installed $AU_INSTALLED (remove with --uninstall-au)"
	else
		record FAIL install-au "could not copy $AU"
	fi
fi
au_available() { [[ -d "$AU_INSTALLED" ]]; }
if au_available && [[ $INSTALL_AU == 0 ]]; then
	if ! diff -rq "$AU" "$AU_INSTALLED" >/dev/null 2>&1; then
		note "  WARN  an AU is installed at $AU_INSTALLED but differs from $AU; AU gates test the INSTALLED copy"
	fi
fi

export LEXICON224_ROMPATH="$FW/$ROM_SET"

# ---------------- pluginval ----------------
PV=/Applications/pluginval.app/Contents/MacOS/pluginval
if wanted pluginval; then
	IFS=, read -r -a levels <<< "$PV_LEVELS"
	for fmt in VST3 AU; do
		if [[ "$fmt" == "VST3" ]]; then path="$VST3"; else path="$AU_INSTALLED"; fi
		for level in "${levels[@]}"; do
			name="pluginval-$fmt-s$level"
			if [[ ! -x "$PV" ]]; then record SKIP "$name" "pluginval not found at $PV"; continue; fi
			if [[ "$fmt" == "AU" ]] && ! au_available; then
				record SKIP "$name" "AU not installed (pluginval finds AUs via the registry): rerun with --install-au"; continue
			fi
			note "== $name (LEXICON224_ROMPATH=$LEXICON224_ROMPATH) =="
			log="$OUT/$name.log"
			if limit 3600 "$PV" --strictness-level "$level" --timeout-ms 600000 \
				${PV_GUI[@]+"${PV_GUI[@]}"} --validate "$path" > "$log" 2>&1 \
				&& tail -1 "$log" | grep -q SUCCESS; then
				record PASS "$name" "SUCCESS"
			else
				record FAIL "$name" "$(grep -m1 -E '!!!|FAILED|\*\*\*' "$log" | cut -c1-120) — $log"
			fi
		done
	done
fi

# ---------------- auval ----------------
if wanted auval; then
	if ! au_available; then
		record SKIP auval "AU not installed: rerun with --install-au (then --uninstall-au)"
	else
		note "== auval -v aufx $PLUG_CODE $MFR_CODE =="
		if limit 1200 auval -v aufx "$PLUG_CODE" "$MFR_CODE" > "$OUT/auval.log" 2>&1 \
			&& grep -q 'AU VALIDATION SUCCEEDED' "$OUT/auval.log"; then
			record PASS auval "AU VALIDATION SUCCEEDED"
		else
			record FAIL auval "see $OUT/auval.log"
		fi
	fi
fi

# ---------------- soak (opt-in) ----------------
if [[ $WITH_SOAK == 1 ]] && wanted soak; then
	note "== soak: one seed, C++ operator vs JS =="
	if limit 600 sh tests/soak/build.sh > "$OUT/soak_build.log" 2>&1 \
		&& limit 1800 sh tests/soak/run_pair.sh "224XL v8_21" gates 1 100 > "$OUT/soak.log" 2>&1 \
		&& grep -q 'traces: identical' "$OUT/soak.log"; then
		record PASS soak "$(grep 'traces:' "$OUT/soak.log")"
	else
		record FAIL soak "see $OUT/soak.log"
	fi
fi

# ---------------- scoreboard ----------------
note ""
note "================ GATE SCOREBOARD ================"
nfail=0; nknown=0; npass=0; nskip=0
for r in ${RESULTS[@]+"${RESULTS[@]}"}; do
	status="${r%%|*}"; rest="${r#*|}"; gate="${rest%%|*}"; detail="${rest#*|}"
	printf '  %-10s %-22s %s\n' "$status" "$gate" "$detail" >&2
	case "$status" in
		PASS) npass=$((npass + 1)) ;;
		FAIL) nfail=$((nfail + 1)) ;;
		KNOWN-FAIL) nknown=$((nknown + 1)) ;;
		SKIP) nskip=$((nskip + 1)) ;;
	esac
done
note "================================================="
note "pass $npass, fail $nfail, known-fail $nknown, skipped $nskip — logs in $OUT"
if [[ $STRICT == 1 ]]; then nfail=$((nfail + nknown)); fi
if [[ $nfail -gt 0 ]]; then note "RESULT: FAILED"; exit 1; fi
if [[ $nknown -gt 0 ]]; then note "RESULT: passed, with $nknown known-failing gate(s)"; else note "RESULT: passed"; fi
exit 0
