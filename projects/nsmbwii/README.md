# New Super Mario Bros. Wii

`SMNE01`, NTSC-U, revision 2. The second game tried, and the one that forced the engine to stop being
Mario Kart's: its replacements bind by name here, not by address.

Bring your own disc. Nothing here contains game code or data.

## Getting the values in recomp.yml

```sh
example-wii-nx/scripts/extract-dol "New Super Mario Bros. Wii (USA) (Rev 2).iso" main.dol
example-wii-nx/scripts/inspect-dol main.dol
```

That prints the entry point and the two small-data bases already recorded here, and confirms your dump
matches the `sha256` in `recomp.yml`. Put the DOL at `Assets/nsmbwii/main.dol`.

## Symbols

**342 of 574 (59.6%)** of the engine's native replacements were located in this game by matching code;
`bindings.json` holds them. That is the lowest of the first-party titles bar Wii Sports, because much of
what this game calls is EAD's own engine code rather than the SDK.

## Status

Project only. Translation stops at an address outside the loaded image (`0x00000060`) — this game loads
its code in several REL modules rather than one `StaticR.rel`, and that is the open work here.
