#!/usr/bin/env python3
"""Development server: the page, plus read-only access to a local ROM folder.

  python3 page/serve.py --roms "$LEXICON_FIRMWARE"

then open, for example,
  http://127.0.0.1:8000/?rom=224XL%20v8_21&boot=1

`--sound FILE` serves one audio file at /sound; the page loads it at
startup and makes it the default input.

`--midiverb DIR` serves the MIDIVerb DSP ROMs in DIR (files of 16 or 32 KB)
the same way, at /mvroms/index.json and /mvroms/FILE, for compare.html.

`--boot SET` redirects the bare URL to ?rom=SET&boot=1, so a shared link
boots that set without query parameters.

`--roms` is a folder of firmware sets (one subfolder per set, like the
owned "Lexicon 224 and 224X Firmware" folder), or a single set's folder.
The page lists the sets from /roms/index.json and fetches chips from
/roms/SET/FILE. Only loopback is served; nothing is uploaded anywhere.
"""
from __future__ import annotations

import argparse
import http.server
import json
import re
import urllib.parse
from pathlib import Path

WEB = Path(__file__).resolve().parent
CHIP = re.compile(r"((SBC|NVS)\s*\d|ROM\s*[1-4](?!\d)).*\.bin$", re.IGNORECASE)   # 224X/XL: SBCn, NVSn; the 224: ROM1-4


def rom_sets(root: Path) -> dict[str, list[str]]:
    folders = [root] + sorted(p for p in root.iterdir() if p.is_dir())
    sets = {}
    for folder in folders:
        chips = sorted(p.name for p in folder.iterdir() if p.is_file() and CHIP.search(p.name))
        if chips:
            sets["." if folder == root else folder.name] = chips
    return sets


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--roms", type=Path, required=True)
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--boot", metavar="SET", help="redirect / to ?rom=SET&boot=1")
    parser.add_argument("--sound", type=Path, help="an audio file to serve at /sound (the default input)")
    parser.add_argument("--midiverb", type=Path, help="a folder of MIDIVerb DSP ROMs for compare.html")
    args = parser.parse_args()
    root = args.roms.resolve()
    sets = rom_sets(root)
    if not sets:
        raise SystemExit(f"no SBCn/NVSn/ROMn chip files under {root}")
    midiverb = []
    if args.midiverb:
        midiverb = sorted(p.name for p in args.midiverb.iterdir() if p.is_file() and p.stat().st_size in (16384, 32768))
    if args.boot and args.boot not in sets:
        raise SystemExit(f"--boot {args.boot!r} is not one of: {', '.join(sets)}")

    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **k):
            super().__init__(*a, directory=str(WEB), **k)

        def do_GET(self):
            url = urllib.parse.urlparse(self.path)
            path = urllib.parse.unquote(url.path)
            if args.boot and path == "/" and not url.query:
                self.send_response(302)
                self.send_header("Location", "/?" + urllib.parse.urlencode({"rom": args.boot, "boot": 1}, quote_via=urllib.parse.quote))
                self.end_headers()
                return
            if path == "/sound":
                if not args.sound:
                    return self.send_error(404)
                return self.reply(args.sound.read_bytes(), "application/octet-stream")
            if path == "/roms/index.json":
                return self.reply(json.dumps(sets).encode(), "application/json")
            if path == "/mvroms/index.json":
                return self.reply(json.dumps(midiverb).encode(), "application/json")
            if path.startswith("/mvroms/"):
                name = path[len("/mvroms/"):]
                if name in midiverb:
                    return self.reply((args.midiverb / name).read_bytes(), "application/octet-stream")
                return self.send_error(404)
            if path.startswith("/roms/"):
                parts = path[len("/roms/"):].split("/")
                if len(parts) == 2 and parts[0] in sets and parts[1] in sets[parts[0]]:
                    folder = root if parts[0] == "." else root / parts[0]
                    return self.reply((folder / parts[1]).read_bytes(), "application/octet-stream")
                return self.send_error(404)
            return super().do_GET()

        def reply(self, body, kind):
            self.send_response(200)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    print("firmware sets:", ", ".join(sets))
    if args.sound:
        print("default input:", args.sound.name)
    print(f"open http://127.0.0.1:{args.port}/?rom={urllib.parse.quote(next(iter(sets)))}&boot=1")
    http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
