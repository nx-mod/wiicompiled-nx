# Mario Kart Wii

Requires a clean **PAL `RMCP01`** disc you dumped yourself. Other regions are rejected.

Status: **boots, menus, save data and races are playable**, at roughly a quarter speed, with no audio.
Player-facing setup is in the [repository README](../../README.md#install).

## From your disc to a build

1. Dump your disc (CleanRip), extract it in Dolphin (**Extract Entire Disc**).
2. Copy `DATA/sys/main.dol` and `DATA/files/rel/StaticR.rel` to `Assets/`.
3. Translate with the WiiCompiled translator using `recomp.yml` here (see `translator/README.md`).
4. Build for your platform; for Switch see the repository README.

`recomp.yml` pins both files by SHA-256, so a wrong revision fails immediately rather than translating
into something subtly broken.

## What is specific to this game

These are the parts that belong to the game rather than the engine, and that move into this folder as the
multi-game split lands (see [projects/README.md](../README.md)):

- `MAP.txt` - function starts used as the translator's discovery oracle
- Save handling quirks (RKSYS: the game formats `rksys.dat` by writing zeros, then fills it in)
- The strap screen, and the renderer's post-processing path mask
- Retro Rewind / Pulsar mod support (`profiles:` in `recomp.yml`)

Everything else it uses - the Wii SDK, and the EGG and NW4R middleware - is shared engine code, because
other games use those too.

## Known issues (Switch)

- **Speed**: about 4x too slow; CPU-bound in translated code and the runtime, not the GPU.
- **Audio**: silent. Samples reach the backend but far too slowly, and 32 kHz output still needs
  resampling to the 48 kHz the console expects.
- **Shader cache** does not persist yet, so shaders recompile on every launch.
- Wii system settings fall back to defaults; a generated SYSCONF covers most of them.
