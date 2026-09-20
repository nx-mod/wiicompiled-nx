# Metroid Prime 3: Corruption

`RM3E01`, NTSC-U, revision 0.

Retro Studios' own engine rather than EAD's, and the heaviest renderer of the set -
useful for finding where Aurora's GX translation is thin.

Bring your own disc. Nothing here contains game code or data.

## Getting the values in recomp.yml

```sh
example-wii-nx/scripts/extract-dol "Metroid Prime 3 - Corruption (USA).iso" main.dol
example-wii-nx/scripts/inspect-dol main.dol
```

That prints what is already recorded here, and confirms your dump matches the
`sha256` in `recomp.yml`. Put the DOL at `Assets/metroidprime3/main.dol`.

| | |
|---|---|
| Entry point | `0x80006320` |
| `_SDA_BASE_` (r13) | `0x806801C0` |
| `_SDA2_BASE_` (r2) | `0x806869C0` |

## Symbols

Partial community symbol work exists (PrimeDecomp), mostly for the GameCube titles. The engine's ~574 native replacements are bound to this game's own
addresses by matching their code:

```sh
example-wii-nx/scripts/resolve-symbols match <signatures.json> Assets/metroidprime3/main.dol \
    --out projects/metroidprime3/bindings.json
```

**394 of 574 (68.6%)** were located that way; `bindings.json` here holds them.
The rest fall back to this game's own translated code, which is correct, only slower.

## Status

Project only: not translated yet.
