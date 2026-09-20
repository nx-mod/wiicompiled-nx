# wiicompiled-nx

The engine behind [wii-nx](https://github.com/nx-mod/wii-nx): Wii games recompiled to run natively on
Nintendo Switch, not emulated. A port of [WiiCompiled](https://github.com/patchzyy/wiicompiled) to
Horizon, with graphics through Aurora → Dawn (WebGPU) → NVK (Vulkan).

- **`translator/`** turns a game's PowerPC code into C++ ahead of time.
- **`runtime/`** answers what the game asks of the Wii — its OS, disc, graphics, sound, controllers and
  saves — natively on the Switch.

Mario Kart Wii boots, plays and saves, at roughly a quarter of real time. Audio is silent.

## Any game, not one

The ~580 functions the runtime replaces are almost all Nintendo's SDK, which every game carries at its
own addresses. So they bind **by name**: a game supplies a table of where each one lives in it, built by
matching code ([resolve-symbols](https://github.com/nx-mod/wii-nx/tree/main/example-wii-nx)), and
anything not found falls back to that game's own translated code. Six games besides Mario Kart have
tables today, holding 56-70% of the replacements each (`projects/`). Mario Kart Wii needs no table, being
the game those addresses came from. What one game alone does (two functions, for Mario Kart) lives with
that game, not here.

```sh
dotnet translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll \
    native-index --project <game>/recomp.yml     # what a game bound, and what it did not
```

## Building

Through [wii-nx](https://github.com/nx-mod/wii-nx), which composes this engine, the libraries and a
game from your own disc:

```sh
git clone --recursive https://github.com/nx-mod/wii-nx && cd wii-nx
wiigames-nx/mkwii-nx/scripts/extract "/path/to/your/disc.iso"
example-wii-nx/scripts/translate mkwii-nx
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake -DWIINX_GAME=mkwii-nx
cmake --build build
```

Needs devkitPro (devkitA64 + libnx), [nxvk](https://github.com/nx-mod/nxvk/releases) as a portlib,
.NET 8, CMake, Ninja, Python 3, and about 6 GB of RAM for the link. No game code or data is in this
repository or its releases.

## Layout

```
translator/          PowerPC -> C++, and the project format (recomp.yml)
runtime/src/hle/     the Wii answered natively: OS, DVD, GX, AX, VI, PAD, NAND
runtime/platform_switch/  Horizon: threads, the SD card layout, SDL gaps
aurora-main/         Aurora (GX on WebGPU), vendored until it moves to aurora-nx
projects/            one folder per game: seven so far, all sharing this engine
docs/switch-port-notes.md   engineering notes and measurements
```

## Where the work is

- **Speed**: four times too slow, and the frame is in the game's own code — two Wii middleware routines
  alone are 10.9% and 10.0%, while our runtime is a few percent. Compiler-wide optimisation and native
  versions of those routines are the levers.
- **A second game end to end**: everything up to translation works for any game.
- **Audio**: silent; needs mixing that keeps up and 32 → 48 kHz resampling.

Shader caches persist through [sqlite-nx](https://github.com/nx-mod/sqlite-nx), so a second launch does
not recompile them.

## Credits and license

Built on [WiiCompiled](https://github.com/patchzyy/wiicompiled) by patchzyy and contributors
([original README](docs/WIICOMPILED_README.md)), [Aurora](https://github.com/encounter/aurora) by
encounter, [Dawn](https://dawn.googlesource.com/dawn),
[NXVK](https://github.com/PalindromicBreadLoaf/nxvk) by PalindromicBreadLoaf, and devkitPro.

GPL-3.0, like WiiCompiled; NVK links statically, so builds are GPL-covered and this repository is their
source. The games are © Nintendo — no Nintendo code, assets or data here or in any release.
