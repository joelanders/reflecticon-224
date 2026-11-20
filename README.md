# reflecticon

Emulations of early digital delay and reverb units that run each machine's own firmware on
models built from the schematics, in your browser or as a plugin. You supply the ROMs: nothing in
this repository or its builds holds ROM bytes.

The 224's web demo: https://joelanders.github.io/reflecticon-224/

## Machines

| Folder | Machine | State |
|---|---|---|
| [`224/`](224/) | Lexicon 224, 224X, 224XL | plugin and web demo released (alpha); gate- and board-level models |

More machines are to follow in their own folders.

Makers and model names are named only to say what is emulated. This project is not affiliated with
or endorsed by any of them or their owners.

## Plugins

All by joelanders, manufacturer code `Rflx`. Hosts key saved sessions on the codes, so they never
change once released.

| Folder | Shown in hosts as | Bundle files | Plugin code | Bundle ID |
|---|---|---|---|---|
| `224/` | Reflecticon 224 | `Reflecticon224` | `Rf24` | `net.joelanders.reflecticon.224` |

The 224 was released as Reflexion224 (same codes, so old sessions open with it); its ROM folder is
still `Application Support/Reflexion224`.

## Building

See [`224/README.md`](224/README.md); its commands run from `224/`.

## License

AGPL-3.0.
