# New Super Mario Bros. Wii

Status: **bring-up** — the second game, used to prove the multi-game engine.

## From your disc to a build

1. **Dump** your own disc with [CleanRip](https://wiibrew.org/wiki/CleanRip) (ISO).
2. **Extract** it in Dolphin: right-click the game → **Properties** → **Filesystem** → right-click the
   disc → **Extract Entire Disc**. Copy `DATA/sys/main.dol` to `Assets/nsmbwii/main.dol`.
3. **Fill in `recomp.yml`** from that DOL:
   - `game_id` / `region`: the first six bytes of the disc header (e.g. `SMNE01`).
   - `entry_points`: the DOL header's entry point.
   - `sda_base` / `sda2_base`: the r13 and r2 constants loaded by `__init_registers`
     (the first function the entry point calls: a `lis`/`ori` pair for each).
4. **Translate** with the WiiCompiled translator (see `translator/README.md`).
5. **Build** for your platform.

## What to expect during bring-up

This is the game that proves the engine is really game-agnostic. Expect to hit, in rough order:

1. **Symbol binding.** The engine's native replacements are still keyed to Mario Kart's addresses
   (575 of them, plus 314 hardcoded guest addresses). Until they bind by symbol, none of them land on
   this game. This is the main work.
2. **REL modules.** Several here rather than one `StaticR.rel`, each with its own load address.
3. **Engine gaps.** SDK or middleware calls Mario Kart never made.
4. **Genuine game quirks.** Anything left goes in `game/` - and should be a short list.

## Notes

- The game loads its code in several REL modules (not one `StaticR.rel` like Mario Kart); how the
  translator should handle those is part of this bring-up.
- Uses Nintendo's SDK plus the NW4R and EGG middleware, so most native replacements should carry over from
  Mario Kart once they bind by symbol.
