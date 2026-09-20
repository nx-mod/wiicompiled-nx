# Wii Sports + Wii Sports Resort

`SP2E01`, NTSC-U, revision 0.

The pack-in disc, and the lowest match of any game tried: it is older code, built
with an SDK a generation away from Mario Kart's, so it is the honest test of how far
signature matching carries.

It also needs Wii Remote motion, which the runtime does not have yet.

Bring your own disc. Nothing here contains game code or data.

## Getting the values in recomp.yml

```sh
example-wii-nx/scripts/extract-dol "Wii Sports + Wii Sports Resort (USA).iso" main.dol
example-wii-nx/scripts/inspect-dol main.dol
```

That prints what is already recorded here, and confirms your dump matches the
`sha256` in `recomp.yml`. Put the DOL at `Assets/wiisports/main.dol`.

| | |
|---|---|
| Entry point | `0x80004050` |
| `_SDA_BASE_` (r13) | `0x801FBAC0` |
| `_SDA2_BASE_` (r2) | `0x801FCEC0` |

## Symbols

No public symbol map. The engine's ~574 native replacements are bound to this game's own
addresses by matching their code:

```sh
example-wii-nx/scripts/resolve-symbols match <signatures.json> Assets/wiisports/main.dol \
    --out projects/wiisports/bindings.json
```

**321 of 574 (55.9%)** were located that way; `bindings.json` here holds them.
The rest fall back to this game's own translated code, which is correct, only slower.

## Status

Project only: not translated yet.
