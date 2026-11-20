# reflecticon

Emulations of early digital delay and reverb units that run each machine's own firmware (or
PROMs) on models built from the schematics, in your browser or as a plugin. You supply the ROMs:
nothing in this repository or its builds holds ROM bytes.

The 224's web demo: https://joelanders.github.io/reflecticon-224/

## Machines

| Folder | Machine | State |
|---|---|---|
| [`224/`](224/) | Lexicon 224, 224X, 224XL | plugin and web demo released (alpha); gate- and board-level models |
| [`rmx16/`](rmx16/) | AMS RMX16 (v3.2) | plugin and web build; hardware panel |
| [`dmx1580/`](dmx1580/) | AMS DMX 15-80S | plugin and web build; hardware panel |
| [`dn780/`](dn780/) | Klark Teknik DN780 (v2.2) | plugin and web build |
| [`sg323/`](sg323/) | Ursa Major StarGate 323 | plugin and web build |

Makers and model names are named only to say what is emulated. This project is not affiliated with
or endorsed by any of them or their owners.

## Plugins

All by joelanders, manufacturer code `Rflx`. Hosts key saved sessions on the codes, so they never
change once released.

| Folder | Shown in hosts as | Bundle files | Plugin code | Bundle ID |
|---|---|---|---|---|
| `224/` | Reflecticon 224 | `Reflecticon224` | `Rf24` | `net.joelanders.reflecticon.224` |
| `rmx16/` | Reflecticon RMX16 | `ReflecticonRMX16` | `Rx16` | `net.joelanders.reflecticon.rmx16` |
| `dmx1580/` | Reflecticon DMX 15-80 | `ReflecticonDMX1580` | `Dm15` | `net.joelanders.reflecticon.dmx1580` |
| `dn780/` | Reflecticon DN780 | `ReflecticonDN780` | `Dn78` | `net.joelanders.reflecticon.dn780` |
| `sg323/` | Reflecticon StarGate 323 | `ReflecticonSG323` | `Sg32` | `net.joelanders.reflecticon.sg323` |

The 224 was released as Reflexion224 (same codes, so old sessions open with it); its ROM folder is
still `Application Support/Reflexion224`. Only the 224 has been released.

## Layout

```
224/            the 224: everything it had as the reflecticon-224 repo (its own plugin and web demo)
rmx16/  dmx1580/  dn780/  sg323/
  core/         the emulator
  plugin/       the plugin's machine definition and front panel
  tests/        check_machine: the machine against its ROMs
shared/         code the machines above share: the plugin framework (JUCE), ROM recognition,
                threading, the hardware-panel API, host checks
web/            one web page for rmx16, dmx1580, dn780 and sg323
notes/          notes that span machines
```

A machine keeps its own copy of anything only it uses, CPU cores included; code moves to
`shared/` once a second machine needs it.

## Building

The 224: see [`224/README.md`](224/README.md).

The others, from this folder (JUCE 8 is fetched unless you point at a copy):

```
cmake -S . -B build -G Ninja [-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE]
cmake --build build
build/rmx16/rmx16_check_machine <folder with your RMX16 ROMs>
sh web/build.sh && python3 -m http.server -d web/page 8323
```

See [`shared/README.md`](shared/README.md) and [`web/README.md`](web/README.md).

## License

AGPL-3.0. Third-party code keeps its own licence (see the `LICENSE-*` files beside it).
