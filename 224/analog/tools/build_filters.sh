#!/bin/sh
# Regenerate filters.hpp (224X) and filters_224.hpp (Model 224) from the
# committed netlists (circuits/*.cir).
set -e
cd "$(dirname "$0")/.."
mkdir -p build
for c in ain_224x aout_224x; do
    python3 tools/mna.py circuits/$c.cir --out out --audio-fit --spice --json build/$c.json
done
python3 tools/gen_header.py build/ain_224x.json build/aout_224x.json filters.hpp \
    --source "circuits/ain_224x.cir + circuits/aout_224x.cir via tools/mna.py --audio-fit"

# The Model 224's boards (the values as drawn; trims at the drawing's nulls).
for c in ain_224 aout_224; do
    python3 tools/mna.py circuits/$c.cir --out out --audio-fit --spice --json build/$c.json \
        --report 1000,2000,8000,10240,11815,19120
done
python3 tools/gen_header.py build/ain_224.json build/aout_224.json filters_224.hpp \
    --source "circuits/ain_224.cir + circuits/aout_224.cir via tools/mna.py --audio-fit" --suffix _224
