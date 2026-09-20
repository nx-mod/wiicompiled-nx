# Game projects

One folder per game. The engine — translator, runtime, platform layers — is shared; a folder here
holds only what is specific to that game.

| Folder | Game | Engine replacements found in it |
|---|---|---|
| `mkwii/` | Mario Kart Wii (PAL `RMCP01`) | reference: the addresses the engine registers are its own |
| `spmwii/` | Super Paper Mario (`R8PE01`) | 403 / 574 (70.2%) |
| `pikmin2/` | Pikmin 2 (`R92E01`) | 396 / 574 (69.0%) |
| `metroidprime3/` | Metroid Prime 3 (`RM3E01`) | 394 / 574 (68.6%) |
| `punchout/` | Punch-Out!! (`R7PE01`) | 386 / 574 (67.2%) |
| `nsmbwii/` | New Super Mario Bros. Wii (`SMNE01`) | 342 / 574 (59.6%) |
| `wiisports/` | Wii Sports + Resort (`SP2E01`) | 321 / 574 (55.9%) |
| `examples/` | templates for starting a new game | |

Those figures count the 574 replacement *names* the matcher knows. The translator's own
`native-index` counts every registration instead (785, including ABI variants and addresses hardcoded
for Mario Kart), so its percentage is lower for the same table.

Mario Kart Wii is the only one built end to end so far; the rest are projects with everything their
disc told us, matched against the engine, waiting on translation.

These are the engine's own project files, driven from a checkout of this repository alone. The same
games also appear in [wii-nx/wiigames-nx](https://github.com/nx-mod/wii-nx/tree/main/wiigames-nx),
which adds the SD-card layout, per-game native code and the one-command build.

## What a game folder contains

| File | What it is |
|---|---|
| `recomp.yml` | Translator project: the DOL (and any RELs), memory layout, SDA bases, entry points, output folder |
| `bindings.json` | Where this game keeps each function the runtime replaces — found by matching code, not by address |
| `MAP.txt` | Function start addresses, when a symbol map exists; the translator's discovery oracle |
| `README.md` | What came off this game's disc, and what is left to do |

No game code or data lives here. The DOL you point `recomp.yml` at comes from your own disc and goes
in `Assets/<game>/`, which is not tracked.

## How binding by name works

A native replacement declares the function it implements (`OSReceiveMessage`), not an address. Each
game's `bindings.json` says where that function lives in *that* game, so one replacement serves every
game. Anything not found simply is not bound: the game's own translated code runs instead, which is
correct, only slower.

```sh
example-wii-nx/scripts/resolve-symbols match <signatures.json> Assets/<game>/main.dol \
    --out projects/<game>/bindings.json
example-wii-nx/scripts/manual-adds <game>      # what is left for this game
```

## Adding a game

```sh
example-wii-nx/scripts/new-game "/path/to/Your Game.iso" yourgame
```

That reads the disc and writes the whole project — ID, entry point, SDA bases, checksums. By hand:
copy `examples/generic-dol.yml`, then fill it from `inspect-dol`.
