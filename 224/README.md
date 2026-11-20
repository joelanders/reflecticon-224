# 224

Everything for the 224, 224X and 224XL: the emulator, the gate-, board- and ISA-level models,
the schematics, the plugin (`juce-plugin/`) and the web demo (`web-demo/`). Commands run from
this folder; your firmware goes in `224/firmware/`.

An emulation of the Lexicon 224, 224X and 224XL digital reverbs that runs
the machines' own firmware, in your browser or as a plugin. You supply the
ROMs.

[Biography of the machine's designer](https://www.davidgriesinger.com/biographies/) (with photo of him prototyping on the bench!).

I'm still not sure what's more impressive: designing the reverb algorithms (initially without realtime rendering hardware) or
designing and building the machine that runs the algorithms. 1975-1978 predates DSP chips by a few years, so David Griesinger built a
programmable DSP out of a few boards of 7400 series TTL logic chips. An 8080 host computer writes ~100 32-bit instructions to a shared
Writable Control Store, and these run in a straight line once per audio frame. Throughput is one Delay Memory access and one MAC per microinstruction.

The web demo, at https://joelanders.github.io/reflecticon-224/:

<img width="750" alt="The web page" src="https://github.com/user-attachments/assets/c0db1bfa-9a64-4a50-8ab3-c72e130a686f" />

The plugin:

<img width="560" alt="The plugin" src="https://github.com/user-attachments/assets/230c2c7c-5f06-43e8-8b63-90de70f8466e" />

Lexicon, 224, 224X and 224XL are named only to say what is emulated. This
project is not affiliated with or endorsed by Lexicon or its owners.

