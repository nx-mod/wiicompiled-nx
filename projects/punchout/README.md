# Punch-Out!!

`R7PE01`, NTSC-U, revision 1.

A Wii exclusive, never ported. Small and graphically modest, but it leans on tight
input timing, which nothing else here tests.

Bring your own disc. Nothing here contains game code or data.

## Getting the values in recomp.yml

```sh
example-wii-nx/scripts/extract-dol "Punch-Out!! (USA) (Rev 1).iso" main.dol
example-wii-nx/scripts/inspect-dol main.dol
```

That prints what is already recorded here, and confirms your dump matches the
`sha256` in `recomp.yml`. Put the DOL at `Assets/punchout/main.dol`.

| | |
|---|---|
| Entry point | `0x80006124` |
| `_SDA_BASE_` (r13) | `0x804178A0` |
| `_SDA2_BASE_` (r2) | `0x8041C8C0` |

## Symbols

No public symbol map; everything below was found by matching code. The engine's ~574 native replacements are bound to this game's own
addresses by matching their code:

```sh
example-wii-nx/scripts/resolve-symbols match <signatures.json> Assets/punchout/main.dol \
    --out projects/punchout/bindings.json
```

**386 of 574 (67.2%)** were located that way; `bindings.json` here holds them.
The rest fall back to this game's own translated code, which is correct, only slower.

## Status

Project only: not translated yet.
