# Game projects

Each folder here is one game. The engine (translator, runtime, platform layers) is shared; a game
folder holds only what is specific to that game.

```
projects/
├── mkwii/      Mario Kart Wii (PAL RMCP01)
├── nsmbwii/    New Super Mario Bros. Wii
└── examples/   templates for starting a new game
```

## What a game folder contains

| File | What it is |
|---|---|
| `recomp.yml` | Translator project: the DOL (and any RELs), memory layout, SDA bases, entry points, symbol map, output folder |
| `MAP.txt` | Function start addresses; the translator's discovery oracle |
| `symbols.txt` | Where the SDK and middleware functions live in this game (generated; see below) |
| `game/` | Native code only this game needs — kept as small as possible |
| `README.md` | Steps for this game, from your own disc to a running build |

## What stays in the engine

Everything shared between games lives outside `projects/`:

- **SDK** — OS, GX, AX, VI, DVD, NAND, WPAD/KPAD, SC. Every Wii game.
- **Shared middleware** — EGG (Nintendo EAD's engine library) and NW4R. Many first-party games.
- **Platforms** — Windows, Linux, macOS, Switch.

Native replacements bind to functions **by symbol**, not by address: a replacement declares the SDK or
middleware function it implements (`OSReceiveMessage`), and the game's `symbols.txt` says where that
function lives in that game's binary. That is what lets one replacement serve every game.

## Adding a game

1. Dump your own disc (CleanRip) and extract it with Dolphin (**Extract Entire Disc**).
2. Copy `examples/generic-dol.yml` to `projects/<game>/recomp.yml` and fill in the DOL path, entry point
   and SDA bases (the r13/r2 constants set in `__init_registers`).
3. Generate `symbols.txt` by signature-matching the SDK and middleware in the DOL.
4. Translate, build, run. Anything that breaks is either an engine gap (fix it in the engine) or a genuine
   game quirk (goes in `game/`).
