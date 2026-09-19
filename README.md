# mkwii-nx

Mario Kart Wii running natively on the Nintendo Switch — a port of
[WiiCompiled](https://github.com/patchzyy/wiicompiled)'s static recompilation to Horizon (libnx), with
graphics through Aurora → Dawn (WebGPU) → NVK (Vulkan).

It boots, reaches the menus, and races are playable. It is **not full speed yet**.

## To do

- **Speed** — runs roughly 4× slower than full speed. It is CPU-bound in the recompiled game code, not the GPU.
- **Audio** — silent. The mixer is starved by the speed deficit, and output still needs 32 kHz → 48 kHz resampling.
- **Shader cache** — does not persist yet, so shaders recompile on every launch.
- **On-screen menu** — the settings/debug overlay exists but has no renderer on Switch yet.
- **Online** — disabled.

## Install

The release contains **no game code**. Like [WiiCompiled](https://github.com/patchzyy/wiicompiled), the
game is translated from your own disc, so you build `mkwii-nx.nro` yourself (step 2).

### 1. Extract your game

You need your own **PAL** Mario Kart Wii disc (**RMCP01**). Other regions will not work.

1. Dump the disc with [CleanRip](https://wiibrew.org/wiki/CleanRip) on a Wii.
2. In [Dolphin](https://dolphin-emu.org), right-click the game → **Properties** → **Filesystem** →
   right-click the disc → **Extract Entire Disc**.
3. You get a `DATA` folder containing `files` and `sys`.

### 2. Build `mkwii-nx.nro`

On Linux or WSL, with [devkitPro](https://devkitpro.org/wiki/Getting_Started) (devkitA64 + libnx),
.NET 8 SDK, CMake, Ninja and Python 3. The final link needs about 6 GB of RAM.

1. Install [NXVK](https://github.com/nx-mod/nxvk) as a devkitPro portlib — see its
   [README](https://github.com/nx-mod/nxvk/blob/switch/switch/README.md) (`make image && make && sudo make install`).
2. Clone the two repos side by side:

   ```sh
   git clone -b switch https://github.com/nx-mod/mkwii-nx
   git clone -b switch https://github.com/nx-mod/dawn
   python3 dawn/tools/fetch_dawn_dependencies.py
   ```

3. Translate your game into `mkwii-nx/generated/` from `DATA/sys/main.dol` and
   `DATA/files/rel/StaticR.rel` — see [`translator/README.md`](translator/README.md).
4. Build and package:

   ```sh
   cmake -S dawn -B dawn/build-switch -G Ninja -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_TOOLCHAIN_FILE=dawn/build-switch-toolchain.cmake \
     -DCMAKE_EXE_LINKER_FLAGS_RELEASE="-Wl,--strip-debug -Wl,--no-keep-memory"
   ninja -C dawn/build-switch WiiCompiled
   nacptool --create mkwii-nx nx-mod 1.0.0 mkwii-nx.nacp
   elf2nro dawn/build-switch/WiiCompiled.elf mkwii-nx.nro --nacp=mkwii-nx.nacp
   ```

### 3. Set up the Switch

Requires a Switch running [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere).

1. Download `mkwii-nx-v1.0.0.zip` from [Releases](https://github.com/nx-mod/mkwii-nx/releases) and
   extract it to the root of your SD card.
2. Copy your `mkwii-nx.nro` to `sdmc:/switch/mkwii-nx/`.
3. Copy the `DATA` folder from step 1 into `sdmc:/WiiCompiled/`, so you have:

   ```
   sdmc:/WiiCompiled/DATA/files/
   sdmc:/WiiCompiled/DATA/sys/
   ```

4. Launch in **full-memory mode**: hold **R** while opening any installed game to get the
   Homebrew Menu, then pick **mkwii-nx**. Launching from the Album does not have enough memory.

The first boot takes a few seconds on the banner while it indexes the game files.

## Repos

| Repo | What it is |
|---|---|
| [nx-mod/mkwii-nx](https://github.com/nx-mod/mkwii-nx) | this repo: the runtime, the Switch port, and Aurora |
| [nx-mod/dawn](https://github.com/nx-mod/dawn) | Dawn (WebGPU) ported to Horizon |
| [nx-mod/nxvk](https://github.com/nx-mod/nxvk) | NVK, the Mesa Vulkan driver for the Switch GPU (used unmodified) |

Engineering notes for the port are in [`docs/switch-port-notes.md`](docs/switch-port-notes.md).

## Credits

- [WiiCompiled](https://github.com/patchzyy/wiicompiled) by patchzyy and contributors — the recompilation
  this is built on ([original README](docs/WIICOMPILED_README.md))
- [Aurora](https://github.com/encounter/aurora) by encounter — GX on WebGPU
- [Dawn](https://dawn.googlesource.com/dawn) — WebGPU
- [NXVK](https://github.com/PalindromicBreadLoaf/nxvk) by PalindromicBreadLoaf — NVK on Switch
- [devkitPro](https://devkitpro.org) and libnx

## License

GPL-3.0, like WiiCompiled. The NVK driver is linked statically, so builds are GPL-covered; this repo is
their source. Mario Kart Wii is © Nintendo — no Nintendo code, assets or game data are included in this
repo or its releases.
