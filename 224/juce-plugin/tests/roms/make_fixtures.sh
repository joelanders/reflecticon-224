#!/bin/sh
# Build ROM-recognition fixtures OUTSIDE the repo from the user's own chips:
#   sh tests/roms/make_fixtures.sh FIRMWARE_DIR OUT_DIR
# OUT_DIR must not be inside the repository (bring your own ROM: no chip bytes
# in the repo, ever).
set -e
FW="$1"; OUT="$2"
case "$(cd "$(dirname "$OUT")" && pwd)/" in
  */juce-plugin/*) echo "refusing to write fixtures inside the repo: $OUT"; exit 1;;
esac
rm -rf "$OUT"; mkdir -p "$OUT"

# A zip of one set, the chips two folders deep, plus macOS junk entries.
mkdir -p "$OUT/stage/a/b/224XL v8_21" "$OUT/stage/__MACOSX/a/b/224XL v8_21"
cp "$FW/224XL v8_21/"*.BIN "$OUT/stage/a/b/224XL v8_21/"
printf 'junk' > "$OUT/stage/__MACOSX/a/b/224XL v8_21/._SBC1 2716.BIN"
(cd "$OUT/stage" && zip -qr "$OUT/xl821_nested.zip" a __MACOSX)
rm -rf "$OUT/stage"

# A folder holding a zip (v8.1A and v8.21 together: they share chips) and loose v4.3 chips.
mkdir -p "$OUT/mixed/stage"
cp "$FW/224XL v8_1A/"*.BIN "$OUT/mixed/stage/"
mkdir -p "$OUT/mixed/stage/v821"; cp "$FW/224XL v8_21/"*.BIN "$OUT/mixed/stage/v821/"
(cd "$OUT/mixed/stage" && zip -qr "$OUT/mixed/two_xl.zip" .)
rm -rf "$OUT/mixed/stage"
cp "$FW/224 v4.3/"*.BIN "$OUT/mixed/"

# Duplicate chips: every v4.3 chip twice under other names, a copy in a subfolder,
# and a stray duplicate of a 224X chip whose set is incomplete.
mkdir -p "$OUT/dups/backup"
for n in 1 2 3 4; do
  cp "$FW/224 v4.3/ROM$n 2716.BIN" "$OUT/dups/ROM$n 2716.BIN"
  cp "$FW/224 v4.3/ROM$n 2716.BIN" "$OUT/dups/copy of chip $n.bin"
  cp "$FW/224 v4.3/ROM$n 2716.BIN" "$OUT/dups/backup/ROM$n 2716.BIN"
done

# A wrong chip: v8.21 with one byte of NVS3 changed. No known set is complete,
# so the page (and we) place the chips by name. Plus a second SBC1 (the first
# by path wins).
mkdir -p "$OUT/wrong"
cp "$FW/224XL v8_21/"*.BIN "$OUT/wrong/"
python3 - "$OUT/wrong/NVS3 2732.BIN" <<'PY'
import sys
p = sys.argv[1]; b = bytearray(open(p, 'rb').read()); b[100] ^= 0xFF; open(p, 'wb').write(b)
PY
cp "$FW/224 v4_4/ROM1 2716.BIN" "$OUT/wrong/SBC1 zz other.BIN"

# The 224 v2.2 set alone (recognised, not supported).
mkdir -p "$OUT/v22"; cp "$FW/224 v2_2/"*.BIN "$OUT/v22/"

# A folder with nothing Lexicon in it.
mkdir -p "$OUT/nothing"; printf 'hello' > "$OUT/nothing/readme.txt"
echo "fixtures in $OUT"
