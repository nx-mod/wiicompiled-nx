# Super Paper Mario

`R8PE01`, NTSC-U, revision 2. Intelligent Systems' own engine on the Wii SDK,
which is why it is a useful third game: it uses little of the EGG/NW4R middleware
Mario Kart Wii and New Super Mario Bros. Wii share, so it tests that the SDK
layer stands on its own.

Bring your own disc. Nothing here contains game code or data.

## Getting the values in recomp.yml

```sh
example-wii-nx/scripts/extract-dol "Super Paper Mario (USA) (Rev 2).iso" main.dol
example-wii-nx/scripts/inspect-dol main.dol
```

That prints the entry point and the two small-data bases already recorded here,
and confirms your dump matches (`sha256` above).

## Symbols

The [spm-decomp](https://github.com/SeekyCt/spm-decomp) project has symbol maps
for this game, but mainly for the PAL versions (plus partial NTSC-U rev 0), and
it deliberately excludes the SDK, NW4R and MSL libraries. So it can name some
game code, while the SDK functions the runtime replaces are found by signature
matching instead: **403 of 574 (70.2%)** were located that way, the highest of any
game tried, and `bindings.json` holds them.

## Status

Project only: not translated yet.
