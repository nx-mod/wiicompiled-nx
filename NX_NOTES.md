# wiicompiled-nx

WiiCompiled with a native Nintendo Switch platform. The engine behind
[wii-nx](https://github.com/nx-mod/wii-nx).

A game's PowerPC code is translated to C++ ahead of time and compiled native; this repo supplies the
translator, the Wii runtime the translated code calls into (OS, threads, graphics, audio, disc, saves,
input), and the platform layers - including Switch.

## Status

Mario Kart Wii boots, reaches the menus and races are playable, at roughly a quarter speed and with no
audio yet. See [projects/mkwii](projects/mkwii/README.md).

## Layout

- `translator/` - PowerPC to C++, driven by a per-game `recomp.yml`
- `runtime/` - the Wii runtime, plus `platform/` per host
- `aurora-main/` - vendored Aurora (moving to [aurora-nx](https://github.com/nx-mod/aurora-nx))
- `projects/` - one folder per game; see [projects/README.md](projects/README.md)
- `docs/switch-port-notes.md` - engineering notes from the Switch port: what broke, why, and how it was
  diagnosed

## Where the work is going

Native replacements are still keyed to Mario Kart's addresses (575 of them, plus 314 hardcoded guest
addresses), so today the runtime only fits one game. Binding them by symbol, with a per-game symbol map,
is what makes a second game cheap - see [wii-nx/PLAN.md](https://github.com/nx-mod/wii-nx/blob/main/PLAN.md).

Upstream: [patchzyy/wiicompiled](https://github.com/patchzyy/wiicompiled).
