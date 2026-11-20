#!/usr/bin/env python3
"""Check that every slider name in page/catalogs/*.json has a tooltip in param_help.json.

A slider name is looked up after trimming, collapsing runs of spaces and
removing its channel/output suffix (L, R, LR, (L), L)A, R)CB, L+R), ...).
Exits nonzero and lists the names that have no alias or param entry.
"""
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

SUFFIX = re.compile(r"\s+(\([LR]\)|L\+R\)|LR|[LR]\)[A-D]*|[LR])$")


def strip_suffix(name):
    name = " ".join(name.split())
    return SUFFIX.sub("", name)


def main():
    with open(os.path.join(HERE, "param_help.json")) as f:
        help_ = json.load(f)
    params = help_["params"]
    aliases = help_["aliases"]
    bad_alias = {k: v for k, v in aliases.items() if v not in params}
    missing = {}
    names = set()
    for path in sorted(glob.glob(os.path.join(HERE, "catalogs", "*.json"))):
        with open(path) as f:
            catalog = json.load(f)
        for program in catalog["programs"]:
            for page in program.get("pages", []):
                for slider in page.get("sliders", []):
                    raw = slider["name"]
                    key = strip_suffix(raw)
                    names.add(key)
                    if key not in aliases and key not in params:
                        missing.setdefault(key, set()).add(raw)
    for k, v in sorted(bad_alias.items()):
        print(f"alias {k!r} points at unknown param {v!r}")
    for k, raws in sorted(missing.items()):
        print(f"no help for {k!r} (from {sorted(raws)})")
    print(f"{len(names)} distinct slider names, {len(missing)} missing, "
          f"{len(params)} params, {len(aliases)} aliases")
    return 1 if missing or bad_alias else 0


if __name__ == "__main__":
    sys.exit(main())
