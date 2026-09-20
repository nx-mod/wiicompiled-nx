# Pikmin 2 (Wii)

`R92E01`, NTSC-U, revision 0.

A New Play Control! port: GameCube code rebuilt for the Wii, so its SDK is the Wii's
but its game code is a generation older. The highest automatic match of any game tried.

Bring your own disc. Nothing here contains game code or data.

## Getting the values in recomp.yml

```sh
example-wii-nx/scripts/extract-dol "Pikmin 2 (USA).iso" main.dol
example-wii-nx/scripts/inspect-dol main.dol
```

That prints what is already recorded here, and confirms your dump matches the
`sha256` in `recomp.yml`. Put the DOL at `Assets/pikmin2/main.dol`.

| | |
|---|---|
| Entry point | `0x80006124` |
| `_SDA_BASE_` (r13) | `0x80672800` |
| `_SDA2_BASE_` (r2) | `0x806754E0` |

## Symbols

The pikmin2 decompilation project has extensive symbols for the GameCube release. The engine's ~574 native replacements are bound to this game's own
addresses by matching their code:

```sh
example-wii-nx/scripts/resolve-symbols match <signatures.json> Assets/pikmin2/main.dol \
    --out projects/pikmin2/bindings.json
```

**396 of 574 (69.0%)** were located that way; `bindings.json` here holds them.
The rest fall back to this game's own translated code, which is correct, only slower.

## Status

Project only: not translated yet.
