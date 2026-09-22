# WiiCompiled → Nintendo Switch (Mario Kart Wii, RMCP01) Port Notes

Working notes for the on-device Switch port of the WiiCompiled runtime + the
translated MKW function graph. Target: devkitA64 (GCC 16.1.0), Release only,
packaged as an NRO and booted on a modified Atmosphere (FW 16.0.5).

## Goal

Get translated MKW running on-device. First milestone is a **headless boot**:
translated MKW reaches a readable log on `sdmc:`, not a rendered race.

## Hard constraints / environment facts

- No real root; FS owned by `proot-dev`. `fakeroot apt-get` works; `/usr/bin` writable.
- devkitA64 GCC only (no clang). Clang-only flags (e.g. `-fno-slp-vectorize`) must be dropped.
- Termux/proot unstable: foreground builds only, small `-j`, `timeout`-bounded (no background `nohup &`).
  Practical per-command budget ≈ 1400 s; chunk long builds.
- SDL3 has **no Switch backend**. SDL3 is headers-only; audio→audout, input→hid.
  Aurora + SDL surface layers must be stubbed.
- libnx provides: `audout*`, `virtmemFindAslr`, `socket`/`bind`/`connect`/`getaddrinfo`/`freeaddrinfo`.
  libnx does NOT provide: `nanosleep`, `sin`, `cos` (those come from newlib/libm).

## Build layout

Everything builds out-of-tree in `/tmp/opencode/mkwwitch/`:

- `Makefile2` — object build (obj dir `obj2/`), Switch flags.
- `obj2/libtranslated.a` — 89 translated shard objects (72 base_common + dispatch + 16 registration).
- `obj2/libruntime.a` — 82 objects (78 runtime + 2 generated + 2 asm).
- `obj2/libswitchext.a` — 187 objects (hand stubs + bulk GX stubs + third-party).

Flags (final, PIC): `-D__SWITCH__ -DTARGET_PC -fPIE -ftls-model=local-exec
-march=armv8-a+crc+crypto -O2 -fno-fast-math -ffp-contract=off -w -pipe`.
Shards are C++17; runtime is C++20.

Makefile exclusions (do not compile): `guest_flat_memory_macos.cpp`,
`product/retro_rewind_product.cpp`, `host_cpu_baseline.cpp`.

### Chunked build recipe (per-command time budget)

`make` skips existing objects, so call it repeatedly with explicit absolute
target lists. Pattern-rule targets use the absolute `OBJ` path — relative
targets will NOT match. Shards: 4 batches of 18 base_common + 1 batch of the
17 dispatch/registration (targets must include the `base_dispatch/` /
`base_registration/` subdir). Runtime/gen/asm and archives fit in one call each.

## Stubbing strategy

`-fno-slp-vectorize` is Clang-only (dropped). `switch_stubs.cpp` holds
hand-typed stubs; `switch_stubs_gx.cpp` holds 136 auto-generated bulk GX no-ops.

Key stub notes:

- Aurora API lives in `aurora-main/include/aurora/aurora.h`
  (`AuroraBackend` = AUTO/D3D11/D3D12/METAL/VULKAN/OPENGL/OPENGLES/WEBGPU/NULL;
  `aurora_initialize(int,char**,const AuroraConfig*)`), `aurora/gfx.h`
  (`AuroraPresentTiming`, `AuroraStats`).
- `aurora_set_guest_write_hooks` takes
  `AuroraGuestWriteGenerationCallback, AuroraGuestWriteNotifyCallback`.
- `alignas` must follow the type for static arrays.
- Mangled symbols supplied by hand:
  `_ZN6aurora2gx4fifo15submit_raw_drawE11GXPrimitive8GXVtxFmtPKhtj` → returns `true`;
  `_ZTHN8AxDspHle13t_onMixWorkerE` (TLS init guard for
  `thread_local bool AxDspHle::t_onMixWorker`, defined `hle/audio/ax_memory.cpp:30`).
- Real GX getters hand-written (not no-ops): `GXGetCPUFifo`, `GXGetVtxDesc`,
  `GXGetTexBufferSize` (copied from `aurora-main/lib/dolphin/gx/GXTexture.cpp:315`).
  Bulk stubs return `uintptr_t` 0.

Why stubbing keeps the game alive: the GX FIFO loop in `hle/gx/gx_fifo.cpp`
advances via `consumeBytes` regardless of `submit_raw_draw`'s result, and VI
retrace is wall-clock-driven (`VI_HLE_PollRetrace`), so the frame loop ticks
without a real GPU. `InstallAuroraHooks` safely no-ops when flat aliases are
absent.

### Rebuild flags from the old (non-PIC) build

Objects were first built without `-fPIE -ftls-model=local-exec`, producing
"read-only segment has dynamic relocations" at link. The `obj2` build is the
final PIC rebuild; prefer it exclusively.

## CryptoPP (bundled, required)

`runtime/third_party/cryptopp` (202 `.cpp`). `wii_es_crypto.h` (reached via
`nand_internal.h`) uses real ECDSA/EC2N/SHA — do NOT stub it. All 42 CryptoPP
refs come from `nand_isfs.o`.

- `eccrypto.cpp` / `eprecomp.cpp` are pure templates: `cp_eccrypto_inst.cpp`
  provides explicit instantiations (523 symbols).
- Include path needs BOTH `-I runtime/third_party` and
  `-I runtime/third_party/cryptopp` (for the `cryptopp/` prefix).
- Built the **full** library object set (178 objects) rather than playing
  whack-a-mole with template symbols like `AbstractEuclideanDomain<...>::Gcd`.
- Excluded from the full build: bench1-3, validat0-10, cryptest, test.cpp,
  fips* (`fips140`/`fipsalgt`/`fipstest`), dlltest, regtest1-4, datatest,
  nativecrypto. Kept `esign.cpp` (legit algorithm object).
- Also needed real `cp_queue.o` (ByteQueue) and `cp_filters.o` (Store).

## Third-party objects

- `xxhash.c` → `XXH3_64bits` emitted via `xxhash.h` inline (referenced by `gx_dl.o`).
- `pugixml.cpp`, `imgui*.cpp` (`imgui`, `imgui_draw`, `imgui_tables`, `imgui_widgets`).

## Symbol surface

`nm -A` over the full graph: defined 35713, undefined 30371 → **647 true
externals** (`ext_now.txt`; `ext_needed.txt` is stale). Referencing objects:

- `aurora_initialize` → `main.o`
- `GXSetTevKColor` → `gx_egg.o`, `gx_tev.o`
- `GXBegin` → `gx_dl.o`, `gx_fifo.o`, `gx_vertex.o`
- `PADRead`, `SDL_GetGamepadButton` → `pad.o` (+ input/settings objs)
- `VIConfigure` → `vi.o`
- `XXH3_64bits` → `gx_dl.o`
- `CryptoPP::Integer` → `nand_isfs.o`
- `ImGui::End` → `controller_mapping_wizard.o`, `settings_overlay.o`
- `AuroraGetSurfaceSize` → `gx_frame.o`, `vi.o`
- `AuroraSetViewportPolicy` → `dynamic_aspect.o`
- `aurora_get_present_timing` → `settings_overlay.o`

## Link command shape

```
aarch64-none-elf-g++ -specs=/opt/devkitpro/libnx/switch.specs ... \
  libtranslated.a libruntime.a libswitchext.a \
  -Wl,--start-group \
    -L/opt/devkitpro/libnx/lib \
    -L/opt/devkitpro/devkitA64/lib/gcc/aarch64-none-elf/16.1.0 \
    -L/opt/devkitpro/devkitA64/aarch64-none-elf/lib \
    -lstdc++ -lm -lnx -lc -lgcc \
  -Wl,--end-group
```

Explicit `-L` flags are required or the link fails with `cannot find -lnx`.

## Address space / memory model

- FW 16.0.5: heap top ≈ 33 GiB; `svcMapMemory` only in the Stack region; no
  self-alias. Runtime stays checked-path-only (`g_requiresCheckedAccess=true`).
- Flat memory alias path abandoned on Switch.
- `MkwStateFreeResult2` built as `{lo, hi}`, consumed `v[0]/v[1]` — ABI-identical
  under AAPCS64 (x0/x1). Fix in `runtime/include/isa/ppc_isa_config.h`
  (devkitA64 drops `ext_vector_type(2)`; `MKW_PPC_*` macros must precede the
  struct branch).

## Config / online

- Wiimmfi disabled at the config layer (not patched). Shipped
  `sdmc:/WiiCompiled/Config.toml` = `[network] enabled=false`,
  `[discord] enabled=false` (`runtime_config.h:487`), uploaded to
  `ftp://10.109.156.168:5000/WiiCompiled/Config.toml`.
- Network gate: `hle/net/network_core.cpp:651`.
- Account shim (libnx `accountInitialize`/`accountGetProfile`; Wii account =
  NAND save + FC) planned, not built — revisit after core boot.

## Status

- [x] Full Switch object graph builds clean (PIC flags).
- [x] CryptoPP full lib (178) + third-party + stubs archived.
- [x] `libtranslated.a` (89) / `libruntime.a` (82) / `libswitchext.a` (189).
- [x] Link NRO/ELF; only libc/libm/libstdc++/libnx remain undefined. Final
  fixes: dropped a duplicate `PADCount` (defined in both `switch_stubs.cpp`
  and `switch_stubs_dev.cpp`), added real bodies for `VIConfigure` /
  `VISetFrameBufferScale` / `VILockAspectRatio` / `VIUnlockAspectRatio` (only
  forward-declared before) and a `waitpid` stub, rebuilt
  `data_sections_init_blobs.S` / `co_switch.S` with `-fPIE
  -march=armv8-a+crc+crypto` (Makefile2's `.S` rule was missing them, the
  source of a "read-only segment has dynamic relocations" error).
- [x] Remaining TEXTREL after the above traced (RELR decode, see
  `decode_relr.py`) to `_ZTISt9bad_alloc`'s typeinfo vtable pointer landing in
  `.rodata` — that symbol comes from devkitA64's static `libstdc++.a`
  (libsupc++ `eh_alloc`/`bad_alloc` object), not our code, and isn't
  rebuildable here. This is a known devkitA64 homebrew issue with
  exception/RTTI-heavy C++; the accepted workaround is linking with
  `-Wl,-z,notext` (kept `-z text` everywhere else via `switch.specs`; only
  this override is added on the final link line).
- [x] Produced `obj2/mkw_dev.elf` → `obj2/mkw_dev.nro` (`elf2nro`, ~101 MB,
  no icon/nacp — plain conversion, fine for a dev boot test).
- [ ] Upload to `ftp://10.109.156.168:5000/switch/`, boot, pull log — **not
  reachable from this sandbox right now** (port 5000 connection refused/no
  route). Needs the console powered on, on the same LAN, with its FTP
  homebrew server running; re-run from `/tmp/opencode/mkwwitch/obj2/mkw_dev.nro`
  once it's up, following the same curl/ftp + relaunch pattern already
  proven in `/tmp/opencode/mkwsmoke/` (see `mkw_smoke.log` for what a good
  boot log looks like).
- [ ] Revisit account shim + Wiimmfi only after translated boot is proven.

## Video: building nxvk (NVK Vulkan driver ported to Switch)

`aurora_initialize()` is currently a full stub (`switch_stubs.cpp`) - no real
GPU backend runs on Switch yet, so a perfect boot still shows no picture.
`/home/proot-dev/switch/nxvk` (a fork of Mesa porting the NVK Vulkan driver +
Zink/GL to the Switch's Tegra X1/GM20B) is sitting in the workspace unbuilt
and unwired - this is the intended path to real video, via a new aurora
Vulkan backend written against it.

nxvk's own README assumes a podman/docker toolchain image; this sandbox has
neither. Built it natively instead, since the image is just
`devkitpro/devkita64` (already installed here at `/opt/devkitpro`) plus a
pile of apt/cargo packages, and this sandbox happens to already be the same
OS as what the Dockerfile assumes:

- Installed via `fakeroot apt-get`: `meson bison libdrm-dev build-essential
  cmake llvm-18-dev clang-18 libclang-18-dev libclc-18-dev libclc-18
  llvm-spirv-18 libllvmspirvlib-18-dev glslang-tools spirv-tools rustup`.
  The Dockerfile pins LLVM **15** (only available on whatever older Debian
  its base image used); this sandbox is Debian 13 (trixie), which only ships
  LLVM 17/18/19/22. Went with **18** - `mesa_clc`'s own wrapper script
  already probes clang-15 through clang-18 plus a glob fallback, so this
  wasn't even a real risk in practice.
- `python3 -m pip install --break-system-packages mako pyyaml` (Meson's
  Mesa integration needs Python `mako`, missing from the base install).
- Rust: `rustup toolchain install nightly-2026-07-02 --profile minimal
  --component rust-src`, `rustup component add rustfmt`,
  `cargo install --locked bindgen-cli cbindgen`. `RUSTUP_HOME=/opt/rust/rustup`
  and `CARGO_HOME=/opt/rust/cargo` persisted in `~/.bashrc` (a plain `export`
  in one shell doesn't survive to the next tool call here) + `rustup default
  nightly-2026-07-02` so meson's `rustc-switch.sh` wrapper (which just calls
  bare `rustc`) resolves to it.
- Replicated the Dockerfile's cross-build plumbing directly against
  `/opt/devkitpro` instead of a container layer: copied `libdrm`/`xf86drm*`
  headers plus the repo's own `switch/docker/cross-include/` overlay into
  `/opt/switch-cross-include/`; baked `libclc.pc`/`LLVMSPIRVLib.pc`/
  `SPIRV-Tools.pc` into `/opt/devkitpro/portlibs/switch/lib/pkgconfig/`
  (`SPIRV-Tools-shared.pc`/`SPIRV-Headers.pc` don't exist as separate files
  in this Debian package layout - not yet clear if that matters); created an
  empty `libdl.a` stub there for static dlopen fallbacks.
- `switch/crossfiles/native.txt` hardcoded `llvm-config-15`; changed to
  `llvm-config-18`.
- `switch/crossfiles/rust.cross` and `switch/rust/rustc-switch.sh` hardcode
  `/work/switch/...` (the container bind-mount path). Rather than patch every
  reference, symlinked `/work -> /home/proot-dev/switch/nxvk` so every
  hardcoded path in the repo resolves as if it were still running in the
  container.
- Build order actually run: `build-native-tools.sh` (needs `mako` fix first)
  → succeeded, produced host `mesa_clc`/`vtn_bindgen2`. →
  `rust/build-std-sysroot.sh` → succeeded (`SYSROOT TEST: OK`). →
  `build/configure-mesa.sh` → succeeded once the sysroot existed (it failed
  first with "Unknown compiler(s): rustc-switch.sh" before the `/work`
  symlink existed). → `ninja -j4 -C switch/build/cross` on `libnvk.a` + the
  support archives (`-j4`, not the full 8 cores, to leave the sandbox
  usable) - **in progress** as of this note.

Next once the driver archives build: stage them (`Makefile`'s `package`
target logic - `ar -M` bundling into `libnvk.a`/`libnvk_support.a` +
`nxvk.pc`), install into `/opt/devkitpro/portlibs/switch`, then the actual
new work - an aurora backend that talks to it. Nothing in `aurora-main`
currently calls libnvk; that backend does not exist yet.

**nxvk build: done.** `libnvk.a` + `libnvk_support.a` + `nxvk.pc` built and
installed into `/opt/devkitpro/portlibs/switch` (see the earlier section
above for the exact native-dep/build steps).

## Video: porting Dawn (WebGPU) to Horizon

Chose to port Dawn rather than write a from-scratch Vulkan renderer:
`aurora-main/lib/gx/` (command_processor.cpp, gx.cpp, shader.cpp,
frame_interpolation.cpp, ~11.2K lines total) is a mature, already-working
Dolphin-GX-to-WebGPU translator sitting on Dawn via
`aurora-main/lib/dawn/BackendBinding.cpp`. Reimplementing that against raw
Vulkan would likely be *more* work than porting Dawn, and far riskier (new
untested rendering code vs. porting something already proven). KartPad (the
sibling Android/iOS/macOS port of this same wiicompiled codebase, at
`/home/proot-dev/switch/kartpad`) didn't have to solve this: Android and
Apple are both first-party Dawn platforms already, so their port never had
to teach Dawn about a brand-new OS. Ours does - checked their `patches/`
dir and there's nothing platform-detection-related to borrow.

Build setup: `/home/proot-dev/switch/dawn/build-switch`, configured via
`/home/proot-dev/switch/dawn/build-switch-toolchain.cmake` (thin wrapper:
`include(/opt/devkitpro/cmake/Switch.cmake)`) with Vulkan-only options
(`-DDAWN_ENABLE_VULKAN=ON`, every other backend/windowing-system option
OFF, `-DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC`). Also needed
`-DDAWN_BUILD_PROTOBUF=OFF` (no IPC/tracing use case here) and, as a
consequence, `-DTINT_BUILD_IR_BINARY=OFF` (that Tint feature requires
protobuf). Most Vulkan-relevant third_party deps were already vendored
(vulkan-headers, spirv-headers, spirv-tools, abseil-cpp); Tint lives in-tree
at `src/tint`, not third_party. `third_party/spirv-cross` is absent but
appears to only matter for HLSL/MSL cross-compilation, not our Vulkan-only
config.

Porting fixes applied so far (each follows an existing pattern already in
the respective file, rather than inventing something new):

- `src/utils/platform.h`: added `DAWN_PLATFORM_IS_HORIZON`, modeled on the
  existing Fuchsia/Emscripten branches. Deliberately **not** marked POSIX -
  libnx has no `dlopen` and no real `mmap` (see the guest_flat_memory.cpp
  finding earlier in this file), and marking it POSIX would silently let
  code paths assuming those through.
- Abseil `sysinfo.cc`: libnx's `pthread_t` is an opaque pointer, not
  arithmetic, so the generic `pthread_self()`-cast-to-`pid_t` fallback
  doesn't compile. Added a `__SWITCH__` branch using
  `threadGetCurHandle()` (a 32-bit kernel handle, unique per-thread within
  the process) - same reasoning Fuchsia's branch already uses for
  `zx_thread_self()`.
- Abseil `thread_identity.cc`: added `__SWITCH__` to the existing
  `__wasi__ || __EMSCRIPTEN__ || __MINGW32__ || __hexagon__` "this platform's
  pthreads don't support signals" condition (libnx has no POSIX signal
  delivery either).
- Abseil `time_zone_libc.cc`: newlib exposes `_timezone`/`_tzname` (aliased
  to `tzname` under POSIX visibility), which is exactly what the existing
  `__native_client__ || __myriad2__ || __EMSCRIPTEN__` branch already uses -
  just added `__SWITCH__` to that condition, no new code needed.
- Abseil `elf_mem_image.h`: devkitA64 output is ELF (`__ELF__` is defined)
  but there's no glibc-style dynamic linker/VDSO on Horizon (no `<link.h>`,
  no `ElfW()`). Added `!defined(__SWITCH__)` to the exclusion list that
  already carries VXWORKS/hexagon/XTENSA for the same reason.
- Root `CMakeLists.txt`: `add_compile_definitions(_GNU_SOURCE
  _DEFAULT_SOURCE)` gated on `CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch"`,
  placed right after `project()`. newlib gates `_timezone`/`_tzname`/
  `fdopen`/etc behind these. **Learned the hard way**: do NOT set this via
  `-DCMAKE_CXX_FLAGS` on the toolchain file or the `cmake` command line -
  devkitPro's `Switch.cmake` sets the real `CMAKE_CXX_FLAGS` (arch flags +
  `-D__SWITCH__`) via `CMAKE_CXX_FLAGS_INIT` through its own
  `NintendoSwitch.cmake` platform module, which CMake only processes
  *after* the toolchain file's `include()` returns (during its own internal
  per-language setup) - so anything set at the CMAKE_CXX_FLAGS level from
  outside either races that or outright clobbers it, silently dropping
  `-D__SWITCH__`/`-march=`/etc. Setting the define in the project's own
  CMakeLists.txt, after `project()`, sidesteps the ordering problem
  entirely.

As of this note: `dawn_native` (Vulkan-only) building clean past 400+/884
objects, zero failures, deep into SPIRV-Tools/Tint (more portable C++, few
further OS-assumption hits expected, but not guaranteed).

### Presentation: VK_NN_vi_surface (found the hard part is easier than expected)

Was expecting Horizon window-system-integration glue for Dawn's Vulkan
backend to be the one genuinely unprecedented piece of this whole port.
It isn't: `VK_NN_vi_surface` is a real, Khronos-registered Vulkan WSI
extension contributed by Nintendo itself - `vkCreateViSurfaceNN(instance,
&{.window = nwindowGetDefault()}, ...)` - exactly parallel to
`VK_KHR_win32_surface`/`VK_KHR_xlib_surface`/`VK_EXT_metal_surface`. nxvk
already implements it, and has a complete working example at
`nxvk/switch/smoke/nvk_vi_swapchain.c` (instance/device bringup → surface →
standard `VK_KHR_swapchain` acquire/submit/present loop, nothing
Switch-specific past surface creation).

Confirmed Dawn's own `src/dawn/native/vulkan/SwapChainVk.cpp` already has a
clean per-platform switch for exactly this (`Win32Surface`, `MetalSurface`,
`AndroidSurface`, `XlibSurface`, `WaylandSurface`), each a guarded `#if`
block calling one `vkCreate*SurfaceKHR/EXT` with a single window-handle
field; `VulkanExtensions.cpp` registers each as a one-line name-string table
entry. Adding a `ViSurfaceNN` case is the same shape of change as everything
else in this file - not a new pattern - and `nwindowGetDefault()` is the
window handle to plug in, same role as the `HWND`/`ANativeWindow*` in the
existing cases. Still real work (new `Surface::Type` enum value + a Switch
case in whatever constructs a `Surface` from a native window, plus the
`CreateViSurfaceNN` function-pointer loading in `VulkanFunctions.h/.cpp`),
but bounded and pattern-following, not an open unknown anymore.

Remaining sequence once `dawn_native` compiles clean:
1. Link a first test binary against `libdawn_native.a` + `libnvk.a` - surface
   any missing symbols before going further.
2. Patch Dawn's Vulkan backend to link `libnvk.a` statically and call
   `vk_icdGetInstanceProcAddr` directly (no dynamic ICD loading exists on
   Switch) instead of `dlopen`-ing `libvulkan.so`.
3. Add the `ViSurfaceNN` surface-creation path described above.
4. New `aurora-main/lib/dawn/SwitchVulkanBinding.cpp`, same role as the
   existing `MetalBinding.mm`; then replace `switch_stubs.cpp`'s fake
   `aurora_initialize()` with the real call.
5. Full relink of the game NRO with the real renderer, then an actual boot
   test.

Separate, not blocking on the above: the Wiimmfi GPCM/NAS/MASTER/NATNEG
client (fully unbuilt; see the account-linking/Wiimmfi research earlier in
this file).

## Controller input: done

`runtime/src/wii_remote_input.cpp` is the real per-frame input path MKW
reads through KPAD (`ReadKpadSample`) - not the `PAD*` mapping-UI helpers in
`switch_stubs_dev.cpp`, which are only for the settings-overlay remap
screen. It was entirely SDL3-`SDL_Gamepad`-based, and SDL has no Switch
backend, so none of it could function as written.

Added a `#if defined(__SWITCH__)` branch to every function in this file
(the desktop SDL path is untouched in the `#else`), reading real input via
libnx's `PadState` (`padConfigureInput`/`padInitializeWithMask`/`padUpdate`,
one lazily-initialized `PadState` per game port, `HidNpadIdType_No1..No4`) -
the same real, on-hardware-proven API the `/tmp/opencode/mkwsmoke/` smoke
test already validated.

Every connected controller is reported as `Kind::RemoteWithClassic` (Wii
Remote + Classic Controller) rather than attempting Wii-Remote-alone
tilt/motion "Wii Wheel" steering: Switch's own face buttons already share
the Classic Controller's exact layout (A right, B down, X up, Y left), so
button/stick mapping is direct, one-to-one, no cross-referencing needed.
Motion steering is left for later - a real Joy-Con/Pro Controller
accelerometer could drive it via libnx's `hidGetSixAxisSensorStates`, not
wired up yet. `Poll()`'s SDL hotplug-rescan state machine and the
accelerometer calibration wizard are both no-ops on Switch: libnx reports
connection state directly and reliably (no Bluetooth-driver dropouts to
paper over), and there's no remote accelerometer to calibrate against yet.

Compile-checked clean against both the Switch target (`-D__SWITCH__
-DTARGET_PC`) and the desktop path (`-DTARGET_PC` only, plain host g++) to
confirm neither branch broke the other's brace matching. Recompiled,
relinked (zero undefined references), NRO regenerated - this is in
`/tmp/opencode/mkwwitch/obj2/mkw_dev.nro` now, same as the earlier fixes.

## First real device test: crashed before showing a name/version

Uploaded via `curl --user anonymous: -T ... ftp://10.109.156.168:5000/switch/mkw_dev.nro`
(anonymous FTP to the console's CFW-provided sdmc: server - confirmed working,
101036032 bytes, matches local exactly). Launch crashed immediately - no
name/version shown either, same symptom the early smoke-test iterations hit
before they were fixed by always using a hardcoded `sdmc:/...` path instead
of relying on any POSIX-desktop path-resolution primitive.

Root cause: `RuntimeConfigFile::ApplicationDataDirectory()` in
`runtime_config.h` had **no `__SWITCH__` branch at all** - it fell through to
the generic fallback, which tries `$XDG_DATA_HOME`/`$HOME` (unset on Horizon)
and then calls `std::filesystem::current_path()`, which throws on a target
with no real cwd concept. That call happens in `InitializeProcessTranscript`,
**before `RuntimeMain`'s own `try` block starts** - an uncaught exception
there is an immediate `std::terminate()`, before any of our own logging or
crash-artifact code has run. Exactly "no name, no log, nothing."

Confirmed by checking `/tmp/opencode/mkwsmoke/src/main.cpp`: it never touches
`current_path()`/`getenv`/`/proc` at all, just hardcodes `sdmc:/mkw_smoke.log`
directly - the working pattern this file should have followed from the start.

Grepped the whole runtime for the same anti-pattern
(`std::filesystem::current_path()`, `getenv("HOME")`/`getenv("XDG...")`,
`/proc/self/exe`) and fixed every instance actually reachable on Switch:

- `runtime_config.h`: `ExecutableDirectory()` now returns `nullopt`
  immediately on Switch (no `/proc`, matches `platform/host_platform.cpp`'s
  `RuntimePlatform::ExecutableDirectory`, which already did this correctly -
  this file just never had the branch). `ApplicationDataDirectory()` now
  returns a hardcoded `sdmc:/WiiCompiled` (matching the `kApplicationDirectoryName`
  constant and the `sdmc:/WiiCompiled/Config.toml` path already used
  elsewhere in this doc) instead of falling through to the throwing path.
- `nand_path.h`'s `BootstrapPayloadPath()`: same "walk up from cwd" loop,
  reachable from `DiscoverNandRootPath()` - i.e. every boot, since
  `RuntimeConsoleIdentity::Current()` calls it early. Guarded out on Switch.
- `hle/audio/ax_mix.cpp`'s `FindDspCoefficientRom()`: same loop, reachable
  during audio init. Added an app-data-directory (`sdmc:/WiiCompiled/dsp_coef.bin`)
  check for Switch instead.
- `recomp_mod_loader.cpp`'s `RegisterDvdOverlayRoot()`: subtler - it does
  `ExecutableDirectory().value_or(std::filesystem::current_path())`, and
  `value_or`'s argument is evaluated unconditionally as an ordinary function
  argument regardless of whether the optional is engaged, so fixing
  `ExecutableDirectory()` alone did not fix this call site. Fell back to
  `sdmc:/switch` on Switch instead.
- `discord_presence.cpp` also has bare `getenv()` calls (XDG_RUNTIME_DIR/TMPDIR/etc,
  Linux Discord-IPC-socket discovery) - left alone: `getenv` returning null
  is not throwing, and Discord presence is opt-in and non-fatal either way.

Full clean rebuild of `libruntime.a` (not just the touched files) since
`runtime_config.h`/`nand_path.h` are widely-included inline-function headers
and Makefile2 has no header-dependency (`.d` file) tracking to catch
transitive includers automatically - same lesson as the `Ensure()` signature
change earlier tonight. Relinked (zero undefined references).

Also gave the NRO a real NACP/icon this time (`nacptool --create` +
`elf2nro --icon=... --nacp=...`, using libnx's own `default_icon.jpg`) -
the plain `elf2nro obj2/mkw_dev.elf obj2/mkw_dev.nro` used every previous
build had no metadata at all, which is the literal cause of the "no name or
version" symptom (separate from, but reported alongside, the crash above).

Re-uploaded to `sdmc:/switch/mkw_dev.nro` (101042373 bytes, confirmed
matching on the SD card).

## Second device test: crashed again, but with a real crash report this time

Launch still crashed, but this time Atmosphere wrote a real crash report to
`sdmc:/atmosphere/crash_reports/` (same title ID across several attempts:
`05446530aca7e000`). Pulled the latest one via the same anonymous-FTP
connection:

```
Exception Info:
    Type:                        Data Abort
    Fault Address:               0000000000000000
    PC:                          ... (mkw_dev + 0x552da70)
```

Symbolized the PC with `aarch64-none-elf-addr2line`: **`_dynProcessRelr` in
libnx itself** (`nx/source/runtime/dynamic.c`) - the NRO loader's own
self-relocation processor, running at load time. Fault address `0x0` on a
NULL pointer.

This connects directly to the `-Wl,-z,notext` workaround from the original
link (needed to satisfy `_ZTISt9bad_alloc`'s typeinfo vtable pointer landing
in `.rodata` instead of a writable relocatable section - see "nxvk build"
section... no, see the original Switch link notes above). Fetched libnx's
actual `dynamic.c` source to confirm rather than guess: `_dynProcessRelr`
does **no bounds checking at all** and assumes every RELR-encoded relocation
target is writable; the RELRO segment is only locked read-only *after* all
relocations run, so timing isn't the issue - the issue is relocations
landing in genuinely read-only `.rodata` (not the writable-until-locked
relro region) from day one. `-z notext` only silences the *linker's*
complaint about this; it does nothing to make the loader's blind write
succeed.

Traced further: this isn't only `std::bad_alloc`. Every "bad_*" exception
support object in devkitA64's static `libstdc++.a` (`bad_alloc.o`,
`bad_cast.o`, `bad_typeid.o`, `bad_array_new.o` - all providing a
`what()`/destructor pair) was built without `-fPIE`, so each one's
typeinfo/vtable pointer has the same problem. `bad_array_length.o` has it
too, but `std::bad_array_length` isn't in any public header in this GCC
(pre-standard leftover, ABI-compat only) and nothing in this program
references it, so it's never pulled in - safe to ignore.

**Fix attempted**: `/tmp/opencode/mkwwitch/switch_bad_alloc_shim.cpp` -
defines `~bad_alloc()`/`what()`, `~bad_cast()`/`what()`,
`~bad_typeid()`/`what()`, `~bad_array_new_length()`/`what()` ourselves,
compiled with the same `-fPIE` as everything else. Whichever function is
the class's "key function" (first non-inline virtual in declaration order -
the destructor, not `what()` - had to add both once the first attempt hit
"multiple definition" from `what()` still pulling the archive member in) 
anchors where the compiler emits that class's vtable/typeinfo; defining it
ourselves makes it emit here instead of in libstdc++.a's non-PIC copy, so
the linker never needs to extract that archive member at all. Confirmed
each of the four is now gone from the textrel set (relinking without
`-z notext` no longer complains about them).

**Not fully fixed**: one non-PIC textrel remains, from something deeper -
`__cxxabiv1::__class_type_info`'s own typeinfo (`_ZTIN10__cxxabiv1...`) -
core RTTI machinery used by every polymorphic class's compiler-generated
typeinfo, not a convenience wrapper. Shimming this ourselves would mean
reimplementing part of libsupc++'s internal RTTI ABI (`__do_upcast`,
`__do_dynamic_cast`, etc.) - much higher risk of a subtle correctness bug
than the bad_alloc-family fix, so didn't attempt it blind. Looked at a
linker-script fix instead (moving the specific `.rodata._ZTI*`-style input
sections into the already-writable `.data.rel.ro` output section
`switch.ld` already defines) - technically the right fix, since these are
genuine `R_AARCH64_RELATIVE` relocations that only need a writable target,
not any RTTI internals change - but doing it safely means the specific
input-section pattern has to be claimed *before* the broad `*(.rodata.*)`
wildcard already claims it, and the two ways to do that (reordering
section blocks, or GNU ld's `INSERT BEFORE`/`AFTER`) both risk disturbing
the PT_LOAD segments' required ascending-address order if not done
carefully. Deferred rather than risk it unverified.

Relinked with the partial shim + `-z notext` still present (for the one
remaining symbol), repackaged, **re-uploaded** to `sdmc:/switch/mkw_dev.nro`
(101046469 bytes - different size than the previous upload, confirms this
is the new build). Worth testing: the actual on-device crash may have been
hitting one of the four now-fixed relocations rather than the remaining
`__class_type_info` one, since `_dynProcessRelr` iterates all of them
uniformly and any could have been the one that happened to fault. Not yet
re-tested as of this note.

## Bug found while waiting for the device (fixed)

`runtime/src/host_context.cpp` `Create()`, Switch/aarch64 branch: the initial
fiber stack pointer was computed as `context->stack + (stackSize +
guardSize)`. That arithmetic is only correct for the `mmap` branch right
below it, where a *single* guard page sits at the *bottom* of one
`totalSize` mapping. `virtmemFindStack(stackSize, guardSize)` (libnx) returns
a pointer to a `stackSize`-byte usable slice with guard pages *outside* it on
both sides, so the true top of the mapped region is `stack + stackSize`, not
`+ totalSize`. The old code placed the SP `guardSize` (0x1000) bytes into the
trailing *unmapped* guard page, so `mkw_co_init`'s very first register-save
write (`sub x0,x0,#240` then stores) would fault immediately — this would
have hit on every fiber creation, starting with the first one right after
`aurora_initialize()` in `RuntimeMain`. Fixed to use `stack + stackSize` on
Switch; rebuilt `host_context.o`, relinked, regenerated `mkw_dev.nro`.

## Reuse

- `main()` is provided by `runtime/src/main.cpp:1520`; reuse it directly
  (`aurora_initialize(0,nullptr,&auroraConfig)` at `main.cpp:1430`).
- Working NRO/link/upload/log pattern lives in `/tmp/opencode/mkwsmoke/`
  (`src/main.cpp` + `Makefile`; logs `sdmc:/mkw_smoke.log`).

## Crash analysis & RELR bug (Sep 17)

All NROs crash identically at launch in `_dynProcessRelr` — Data Abort,
Fault Address `0x0`, PC always at `mkw_dev + 0x552d910`. Confirmed **pre-existing**
across every tested build:

| Build | Crashes? | Same fault address? |
|-------|----------|---------------------|
| Stock libnx `dynamic.c` (zero changes) | Yes | `+0x552d910` |
| `mkw_dev_notext.elf` (earliest workable) | Yes | `+0x552d910` |
| Non-PIE build (`-fno-pie -no-pie`) | Yes | `+0x552d910` |
| Patched `switch_dynamic_patched.c` (rodata unlock) | Yes | `+0x552d910` |

Same return-address chain every time: `[0xac] → [0xdcc] → [0xcd8] → [0xe058] → [0xf34]`
repeating — classic loop between two translated functions.

### Root cause hypothesis

RELR offset entries store **word offsets** (÷8), but libnx's `_dynProcessRelr` adds
them directly to `base` without multiplying by 8. First entry is `0x5dce9e0` —
with ×8 it would point to `.rodata` (~`0x5dd...`); without ×8 it points to garbage
memory. The decoder reads garbage RELR words, eventually hits a bitmap entry while
`ptr` is still NULL → crashes on `ptr[id] += base`.

Verified with readelf: `.relr.dyn` decodes correctly to ~1739 entries targeting
typeinfo/vtable pointers in `.rodata` (`_ZTI*`, `_ZTV*` symbols). The data is valid;
the decoder logic is wrong.

### Patch attempt: rodata unlock via PT_LOAD scan

Created `/tmp/opencode/mkwwitch/switch_dynamic_patched.c` — copies libnx's
`dynamic.c` and adds:
1. Scan PT_LOAD program headers for read-only segments
2. `svcSetMemoryPermission` to grant write before relocations
3. Restore `Perm_R` after relocations complete

**Never took effect.** Disassembly of final ELF shows stock `__nx_dynamic`
(only 1 `svcSetMemoryPermission` call for relro locking, no rodata unlock loop).

Root cause: archive ordering. Link line has `libswitchext.a` then `libruntime.a`
then `-lnx`. When the linker sees `__nx_dynamic` defined in `libswitchext.a`,
it resolves it — BUT `libnx.a` also contains `__nx_dynamic` and the linker may
pick whichever comes first depending on how `--start-group` iterates archives.
Even swapping order didn't change the disassembly.

Tried removing `dynamic.o` from `libnx.a` entirely — `nm` confirmed `__nx_dynamic`
still present (likely pulled in transitively through another member or from a
different copy).

### Resolved: it was never a RELR decoder bug

The "×8 multiplication" hypothesis above turned out to be wrong, and the
"non-PIE build also crashes identically" data point was a false signal
(devkitA64/switch.specs bakes in `-pie` unconditionally - there was no real
non-PIE variant under test, so of course it matched). The actual RELR
decoding logic - both libnx's C and this session's own from-scratch Python
re-implementation of the same algorithm - was correct the whole time: earlier
manual decodes this session found the *exact* addresses of real, correctly-
named C++ typeinfo/vtable symbols (`_ZTISt9bad_alloc`,
`__cxxabiv1::__class_type_info`, `std::exception`, `std::type_info`, 256
distinct locale/iostream symbols) landing in `.rodata` across seven
independent rounds - garbage from a scaling bug could not do that.

The real bug was in how `switch_dynamic_patched.c` (the rodata-unlock patch)
was being linked in, not in relocation math:

- `__nx_dynamic` is referenced by `switch_crt0.o`, and **both live inside
  `libnx.a`**. When the linker pulls `switch_crt0.o` from that archive, it
  resolves `__nx_dynamic` from `dynamic.o` in the *same archive scan pass* -
  entirely self-contained. Nothing external (our own code, `libswitchext.a`,
  even `-lnx` placed inside `--start-group`/`--end-group`) ever gets a chance
  to intervene, because there's no gap in that resolution for an outside
  definition to fill. Confirmed directly via the link map
  (`grep __nx_dynamic mkw_dev_check.map`): `libnx.a(switch_crt0.o)` needs it,
  `libnx.a(dynamic.o)` satisfies it, every time, regardless of archive order
  or grouping.
- **Fix**: pass the patched object as a plain standalone file directly on the
  link command line (`... obj2/switch_dynamic_patched.o -Wl,--start-group
  ...`), not buried inside an archive. A non-archived object is always
  unconditionally linked - by the time the linker reaches `-lnx`,
  `__nx_dynamic` is already defined, so `dynamic.o` is simply never
  extracted. Verified via `objdump --disassemble=__nx_dynamic`: went from
  138 lines/1 `svcSetMemoryPermission` call (stock behavior) to 216
  lines/3 calls (unlock rodata, restore it, the original relro lock) -
  confirmed via the link map too, `.text __nx_dynamic ... obj2/switch_dynamic_patched.o`,
  no `libnx.a(dynamic.o)` anywhere in the final link.

`switch_dynamic_patched.c`'s actual fix (kept from the collaborative rewrite,
generalized beyond the original `__rodata_start`/`__relro_start`-symbol
approach): scan the ELF's own PT_LOAD program headers at runtime for any
segment without `PF_W` set, `svcSetMemoryPermission` it to `Perm_Rw` before
running `_dynProcessRela`/`_dynProcessRelr`, then restore `Perm_R` after -
before the existing (unmodified) relro-locking code runs. The one part of
that rewrite that was reverted: a change treating `DT_RELA`/`DT_RELR`'s
`d_un.d_ptr` as already-absolute and dropping the `base +` prefix - that
can't be right for a PIE linked at base 0 (`switch.ld`'s
`__start__ = 0x0`), since the runtime base isn't known until ASLR picks it
at load time.

Link recipe going forward (note: patched object *before* `--start-group`,
not inside any `.a`):

```
aarch64-none-elf-g++ -specs=.../switch.specs -Wl,-z,notext -fPIE -pie ... \
  obj2/switch_dynamic_patched.o \
  -L.../libnx/lib -L.../gcc/... -L.../aarch64-none-elf/lib \
  -Wl,--start-group obj2/libtranslated.a obj2/libruntime.a obj2/libswitchext.a \
    -lstdc++ -lm -lnx -lc -lgcc \
  -Wl,--end-group
```

`-z notext` is still needed (the underlying non-PIC libstdc++.a objects
still produce real textrels at link time; the patched loader is what makes
them safe to apply at runtime instead of eliminating them). Verified via
link map + disassembly, not yet re-tested on device as of this note - the
NRO is rebuilt and re-uploaded to `sdmc:/switch/mkw_dev.nro`.

Housekeeping: this section produced a lot of throwaway test variants
(`mkw_stock`, `mkw_notext`, `mkw_nopie`, `mkw_test`, `mkw_dev_check*`,
`mkw_dev_diag*`, a dozen crash reports). Cleaned up locally
(`/tmp/opencode/mkwwitch/obj2/`, kept only the real `mkw_dev.{elf,nro,map,nacp}`
+ archives + object trees) and on the console's SD card (deleted the stray
test NROs from `sdmc:/switch/` and the crash reports from
`sdmc:/atmosphere/crash_reports/`, keeping `mkw_smoke.nro`/`mkw_dev.nro`).

### Round 2: the fix took effect, but had its own bug (PT_LOAD self-introspection doesn't work on NRO)

Re-tested on device after the archive-ordering fix above. New crash, and a
better one: PC moved from deep inside stock `_dynProcessRelr`
(`+0x552da70`) to **inside our own patched `__nx_dynamic`**
(`+0x10efc`, with `__nx_dynamic` itself starting at `+0x10e00`) - confirming
the object-file-ordering fix genuinely worked this time. The bug was in the
patch logic itself.

The PT_LOAD-self-scan approach from the collaborative rewrite doesn't work:
it assumes `base` points to a resident, valid `Elf64_Ehdr` at runtime so it
can read `e_phoff`/`e_phnum` back and walk the program header array. NROs
don't work that way - Horizon's NRO format has its own header (`NroHeader`),
fully consumed by the loader before the process starts; nothing keeps a
valid ELF header/program header table resident in the running image. Traced
the crash precisely: `x19` (meant to be `base + ehdr->e_phoff`, i.e. the
phdr array pointer) came out as `base` plus a huge garbage value
(`0x0556c00000000000` on top of a `0x00000005b5700000` base) - because
reading `e_phoff` from `[base+32]` was really just reading raw `.text`
instruction bytes and interpreting them as an ELF header field. The loop
then walked that "array" and eventually dereferenced a wild pointer.

Reverted to the original `__rodata_start`/`__relro_start` linker-symbol
approach (ordinary link-time constants baked into the binary via
`switch.ld`'s existing `PROVIDE_HIDDEN` symbols - not something read back
from a nonexistent runtime structure). Verified both symbols now resolve
sanely in the link (`__rodata_start = 0x556c000`, `__relro_start =
0x6018000`, ~11 MiB span, safely encompassing the actual `.rodata` output
section within it) and that the disassembly loads them via ordinary
GOT-relative references feeding the `svcSetMemoryPermission` calls, not any
ELF-header/phdr-walking code. Relinked (still as a standalone object before
`--start-group`, same fix as before - confirmed via `nm`/disassembly that
`switch_dynamic_patched.o` still wins), repackaged, **re-uploaded** to
`sdmc:/switch/mkw_dev.nro`. Not yet re-tested on device as of this note.

### Round 3: the whole "unlock :rodata at runtime" strategy is a dead end (kernel W^X)

Tested Round 2's build on device. Different exception this time -
**"User Break"**, not a data abort - meaning the RELR/RELA processing loop
itself never even got that far. Decoded the crash `Result`: `0xD401`
&rarr; module 1 (Kernel), description 106 (`InvalidMemoryState`). That's
`svcSetMemoryPermission(rodata_start, rodata_sz, Perm_Rw)` itself getting
rejected, not any relocation write.

This is a hard Horizon kernel restriction, not a bug in our call: memory
that started out read-only (part of the static code image, like `:rodata`)
can never be granted write permission via `svcSetMemoryPermission` -
that's W^X enforced at the kernel level. It's exactly why the *existing*
GNU-RELRO logic in stock `dynamic.c` only ever goes RW&rarr;R (that
direction is allowed for memory that started out mutable) and never the
other way. There is no syscall or trick available to user code that flips
this - **the entire "temporarily unlock `.rodata`, relocate, re-lock"
approach is impossible**, regardless of how correct the rest of the patch
is.

Also checked whether reordering `switch.ld`'s `:rodata`/`:data` PT_LOAD
segments (so the offending content lands in an already-writable segment
instead) is a viable alternative. It isn't, for a structural reason found
by reading `elf2nro`'s actual source
(`switchbrew/switch-tools`, `elf2nro.c` lines 145-185): it assigns
`NroHeader.Segments[0..2]` to the **first three `PT_LOAD` entries by
table position alone** (`Segments[0]`=text, `[1]`=rodata, `[2]`=data),
never inspecting `p_flags`, and hard-errors ("expected 3 loadable phdrs
and a bss!") on anything else. The NRO loader's own segment permissions
are presumably applied the same positional way. Reordering or merging
PT_LOAD segments in `switch.ld` would not selectively "reclassify" specific
sections as writable - it would scramble which of our segments gets
loader-fixed RX/R/RW treatment, or break elf2nro's segment count
assumption outright. Not viable.

**Conclusion: the "libnx is easier?" direction (patch libnx's relocation
processor to tolerate non-PIC content in `:rodata`) is fully dead.** The
only way forward is to eliminate the non-PIC relocations at the source:
recompile the actual devkitA64 `libstdc++.a` objects that contain them with
real `-fPIE`, using the "provide our own strong symbol to preempt archive
extraction" pattern already proven earlier for 13 libsupc++ RTTI/exception
objects (`class_type_info.cc`, `eh_exception.cc`, etc.) - `switch_dynamic_patched.c`
now goes back to being a byte-for-byte match of stock libnx `dynamic.c`.

**Scope, measured precisely rather than estimated:** decoded the linked
binary's actual `.relr.dyn` section (RELR compact self-relocations) and
attributed every entry landing inside `.rodata`'s address range to its
owning archive member via the link map
(`/tmp/opencode/mkwwitch/obj2/mkw_dev.map`). Result: **1739** such
relocations total, from only **44 distinct `libstdc++.a` objects** (not the
"~256 symbols" figure from earlier, which was counting individual mangled
symbols, not source files) + 5 newlib `libc.a`/`libm.a` objects (72 relocs -
separate source tree, harder, lower priority) + 2 relocations inside our
*own* runtime code (`gx_texture.o`, `os_init.o` - 1 each, needs a look but
tiny). The top 6 `libstdc++.a` objects alone account for 64% of the total
(dominated by locale/iostream facet instantiation files -
`shim_facets`/`locale-inst`/`wlocale-inst`/`codecvt`/`hashtable_c++0x`).

**Important:** this only pays off at zero. A single remaining
`R_AARCH64_RELATIVE` relocation landing in `:rodata` still needs the same
impossible unlock (or silently corrupts/faults without one) - partial
progress doesn't unblock anything by itself until every one of the 44+5+2
objects is replaced.

**Found and fixed a second, unrelated instance of the same archive-ordering
bug** while investigating: all 13 previously-added libsupc++ overrides
(`switch_class_type_info.o`, `switch_eh_exception.o`,
`switch_bad_alloc_shim.o`, etc.) were sitting *inside* `libswitchext.a`,
which itself sits inside the `--start-group` alongside `libstdc++.a` - the
exact same ambiguity fixed for `switch_dynamic_patched.o` specifically, but
never applied to the others. Confirmed via the same reloc-attribution
method that the *original* `libstdc++.a` copies were still the ones
contributing content at final addresses. Fix: extracted all 15 override
objects out of `libswitchext.a` (`ar d`) and pass them standalone on the
link command line before `--start-group`, same as `switch_dynamic_patched.o`.
Relinked clean; rodata relocation count dropped from 1739 &rarr; 1573 (166
fewer, matching the sum of what those objects were actually responsible
for). Confirms the fix is real, but the bulk of the problem (the 6 big
locale/iostream files) was never touched by the earlier round.

**In progress:** identified real GCC 16.1.0 source paths (same
`gcc-mirror/gcc`, `releases/gcc-16` branch used successfully for the first
13 files) for all remaining high-impact objects and fetched them -
`src/c++11/{cxx11,cow}-shim_facets.cc`, `locale-inst.cc`, `wlocale-inst.cc`,
`cxx11-wlocale-inst.cc`, `hashtable_c++0x.cc`, `cxx11-ios_failure.cc`,
`sstream/istream/ostream/streambuf-inst.cc`, `locale_init.cc`, `codecvt.cc`,
`system_error.cc`, `cxx11-locale-inst.cc`, `ctype.cc`, `ios{,-inst}.cc`,
`thread.cc` (+ private headers `locale-inst-{monetary,numeric}.h`,
`facet_inst_macros.h`, `compatibility-ldbl-facets-aliases.h`); plus
`src/c++98/{codecvt,stdexcept,locale_facets,locale}.cc` and
`src/c++17/{fs_dir,fs_path}.cc`. Note `lt1-codecvt.o` (82 hits, libtool's
auto-disambiguation prefix for a basename collision) is `src/c++11/codecvt.cc`
(the `<codecvt>` `codecvt_utf8`/`codecvt_utf16` facets) - distinct from the
plain `codecvt.o` (25 hits), which is `src/c++98/codecvt.cc` (the
`std::codecvt<char>`/`wchar_t` locale facets). Still need real source for
`basic_file.o` (33 hits - newlib I/O backend, likely
`config/io/basic_file_stdio.cc`), `eh_alloc.o` (13 hits, libsupc++,
not yet fetched), and a handful of 1-2-hit tail objects
(`c++locale.o`, `monetary_members_cow.o`, `numeric_members.o`,
`bad_array_new.o`). Compiling and relinking next.

### Round 4: recompiled all 44 `libstdc++.a` objects - zero remaining from that source tree

Fetched and compiled all remaining identified files against the exact
`releases/gcc-16` source tree, `-fPIE`, matching the installed target's
ABI/flags. All but a handful compiled clean on the first try; fixed the
stragglers case by case:
- `locale_init.cc` requires `-std=gnu++11` exactly (`#error` guard checks
  `__cplusplus != 201103L`).
- `thread.cc` needed `bits/cxxabi_forced.h` copied to a flat include dir as
  `cxxabi_forced.h` (installed only under `bits/`, included by the source
  without the prefix).
- `hashtable_c++0x.cc` needed `src/shared/hashtable-aux.cc` fetched
  alongside it (relative `#include "../shared/hashtable-aux.cc"`).
- `istream-inst.cc` explicitly instantiates three deprecated raw-pointer
  `operator>>` overloads (`char*`/`unsigned char*`/`signed char*`,
  `wchar_t*`) guarded by `#if !_GLIBCXX_INLINE_VERSION`. The installed
  target headers have `_GLIBCXX_INLINE_VERSION == 0` (so the guard is true)
  but no longer declare those specific overloads in `bits/istream.tcc`
  (only the `_CharT&` reference overloads remain) - real drift between this
  upstream `.cc` and devkitA64's actual installed headers. Deleted the 4
  dead instantiation lines locally; nothing in this codebase uses the
  deprecated pointer-taking overload.
- `fs_dir.cc`/`fs_path.cc` needed `bits/largefile-config.h` (stubbed empty -
  aarch64/newlib already uses 64-bit `off_t` natively, no large-file macros
  needed) and `src/filesystem/dir-common.h` (fetched). `fs_path.cc` also
  needed `-std=gnu++17` specifically: at `-std=gnu++20` `char8_t` becomes a
  distinct type, breaking a `?:` between `std::u8string` and `std::string`
  in `filesystem_error::_Impl::make_what` that only works when
  `u8string`/`string` are the same type (pre-C++20 behavior).
- `eh_alloc.cc` needed `libsupc++/unwind-cxx.h` fetched alongside it.
- Two "same source, different name" mysteries resolved by inspecting real
  symbols via `nm` on the actual `libstdc++.a` member and matching against
  upstream source: `lt1-codecvt.o` (libtool's auto-disambiguation prefix
  for a basename collision) is `src/c++11/codecvt.cc` (the `<codecvt>`
  `codecvt_utf8`/`codecvt_utf16` facets), distinct from plain `codecvt.o` =
  `src/c++98/codecvt.cc` (the `std::codecvt<char>` locale facet).
  `monetary_members_cow.o`/`numeric_members.o` are both
  `config/locale/generic/{monetary,numeric}_members.cc`, compiled twice
  under the dual-ABI build with `-D_GLIBCXX_USE_CXX11_ABI=0` and `=1`
  respectively (confirmed via `__cxx11` namespace presence/absence in the
  real archive members' mangled names). `c++locale.o` is
  `config/locale/generic/c_locale.cc` despite the object's misleading name.
  `basic_file.o` is `config/io/basic_file_stdio.cc` (the only IO backend
  variant that exists in this GCC tree).

Extracted all 15 previous overrides from `libswitchext.a` (`ar d`) and
relinked everything - the 15 originals plus all 30 newly-compiled objects -
as standalone command-line objects before `--start-group`, never inside any
archive. Clean link. Result: **1739 &rarr; 214** rodata-landing relocations
(87.7% eliminated). Re-measured ownership of the 214 remainder and found 6
more `libstdc++.a` objects that weren't in the original attribution (their
relocations were apparently among the initial "129 unresolved" bucket, not
mis-scoped): `ext11-inst.cc`, `fstream-inst.cc`, `ios_errcat.cc`, `c++98`'s
plain `ios_failure.cc` (distinct from `cxx11-ios_failure.cc`, already done),
`functional.cc` (for `bad_function_call`), and newlib's own
`config/locale/newlib/ctype_members.cc` override. Fetched, compiled clean
(all 6 first try), relinked as **v4**.

**Result: 1739 &rarr; 72 (95.9% eliminated). Zero remaining relocations from
any `libstdc++.a` object** - every single one of the 44 (now really 50,
counting the 6 found in this round) is fully replaced. What's left is
100% from a different source tree: newlib's `libc.a` (`libc_a-timelocal.o`
63 hits, `libc_a-locale.o` 7 hits) and devkitPro's `libsysbase.a`
(`libsysbase_libsysbase_a-iosupport.o`, 2 hits). Next: find devkitPro's
newlib-cygwin fork and libsysbase source repos and apply the same
recompile-with-`-fPIE` treatment to these last 3 objects to reach true
zero. Not yet tested on device - `mkw_dev_v4.elf` has not been packaged to
NRO or uploaded.

### Round 5: none of Round 4 was actually necessary - devkitA64 ships pre-built PIC libraries

While chasing the last 3 objects (`libc_a-timelocal.o`, `libc_a-locale.o`,
`libsysbase_..._iosupport.o`), wrote a minimal `-fPIE` replacement for
`libc_a-timelocal.o` by hand: extracted the real object from `libc.a` and
disassembled it directly rather than trusting any particular newlib source
snapshot, since two different pasted reference versions of `timelocal.c`
had two different `__time_load_locale` signatures (2-arg vs 4-arg with a
`struct __locale_t*`) - upstream has clearly refactored this file
repeatedly. The *actual linked object* settles it: `__time_load_locale` is
two instructions, `mov w0, #0` / `ret` - an unconditional `return 0`, no
locale-file loading logic at all, argument-count-agnostic since nothing is
read from any register. Wrote `_C_time_locale` as a 63-pointer-field
struct (exactly matching the 63 relocations attributed to this object -
one per field, confirming the layout) plus that trivial stub, verified
`nm`/`size` match the real object exactly (1025 bytes total, same as the
original). For `libc_a-locale.o` (real logic, not a stub - `__loadlocale`/
`_setlocale_r`/etc. actually do string comparisons and category dispatch),
fetched the real `newlib/libc/locale/locale.c` + its private headers
(`setlocale.h`, `ctype/ctype_.h`, `stdlib/local.h`) from
`devkitPro/newlib` and compiled clean with `-fPIE` - all 9 symbols matched
the real object's symbol set exactly.

Then, hunting for `libsysbase.a`'s source (no public devkitPro repo found
for it at all - not under any name in the devkitPro GitHub org), noticed
by chance that `/opt/devkitpro/devkitA64/aarch64-none-elf/lib/` has a
**`pic/` subdirectory** containing pre-built PIC variants of the *entire*
standard library set: `libstdc++.a`, `libc.a`, `libm.a`, `libsysbase.a`,
`libsupc++.a`, `libg.a`, `libpthread.a` - plus a matching
`.../lib/gcc/aarch64-none-elf/16.1.0/pic/libgcc.a`. **devkitA64 ships its
own PIC-safe build of every library involved, specifically for exactly
this scenario** - it was never necessary to recompile anything by hand.

Verified directly: relinked using only `-L .../pic` in place of the
default (non-PIC) library paths, dropping every hand-compiled override
object and `-Wl,-z,notext` entirely. **The link succeeded with no textrel
error at all** (the linker doesn't even need convincing - there's nothing
to warn about), and decoding the resulting `.relr.dyn` confirms **zero**
relocations land in `.rodata` (0 of 501 total RELR entries, vs. 1739 in
the original non-PIC build). Went one step further and also dropped
`switch_dynamic_patched.c` entirely, linking against completely stock
libnx (`dynamic.o` unmodified) - still zero rodata relocations, still no
`-z notext` needed. **The entire libnx-patch strategy (Rounds 1-3) and the
entire libstdc++-recompile strategy (Round 4, ~50 objects) turned out to
be unnecessary detours around a two-flag fix**: just point `-L` at
devkitA64's own `pic/` library directories instead of the default ones.

Final link command, in full:
```
aarch64-none-elf-g++ -specs=$LIBNX/switch.specs -fPIE -pie \
  -march=armv8-a+crc+crypto -o mkw_dev.elf \
  -L$LIBNX/lib \
  -L$DEVKITA64/lib/gcc/aarch64-none-elf/16.1.0/pic \
  -L$DEVKITA64/aarch64-none-elf/lib/pic \
  -Wl,--start-group libtranslated.a libruntime.a libswitchext.a \
    -lstdc++ -lm -lnx -lc -lgcc \
  -Wl,--end-group
```
No `-Wl,-z,notext`, no `switch_dynamic_patched.c`, no override objects.
Packaged as `mkw_dev_final.nro` with the real icon (`mario_kart_wii.png`,
center-cropped to 256x256, no letterbox bars) and nacp (title "Mario Kart
Wii", author "nx-mod"), uploaded to `sdmc:/switch/mkw_dev.nro`. Awaiting
device test.

The ~50 hand-recompiled objects from Round 4 and the 15 libsupc++
overrides from earlier rounds are dead code now, but the investigative
work wasn't wasted: the reloc-attribution methodology (decode `.relr.dyn`,
bisect against the link map's per-object address ranges) is what caught
this - checking the pic-library link's RELR output against zero is what
actually proved the fix, rather than just "it linked so it's probably
fine."

### Round 6: new crash, unrelated to relocations - toml11 EOF-without-newline bug

Tested the pic-library build (`mkw_dev_final.nro`) on device. New crash -
Data Abort, Fault Address `0` (plain NULL deref), well clear of any
relocation/dynamic-linking code this time (PC/LR both inside
`toml::detail::syntax::ws()` / `skip_whitespace` / `parse_comment_line`,
called from `RuntimeConfigFile::LoadConfigFile()` &rarr; `Get()` &rarr;
a static initializer in `settings_overlay.cpp`). Register contents
(`X6`/`X7`/`X12`-`X15`) held leftover ASCII fragments spelling out
`sdmc:/WiiCompiled/Config.toml` and a comment string, confirming exactly
which parse was in flight.

Pulled the actual on-device `sdmc:/WiiCompiled/Config.toml` via FTP: it's
a real, intentional bootstrap config (network/discord disabled, written by
an earlier session directly rather than generated by `EnsureConfigFile()`)
- but its last line (`enabled = false`) has **no trailing newline**
(confirmed with `xxd`). toml11's line-oriented scanner walks past the end
of the input buffer when the final line isn't newline-terminated, which is
a raw crash, not a `toml::syntax_error` - so the existing
`try { toml::parse(...) } catch (...)` in `ParseConfig()` never gets a
chance to catch it.

Fixed in `runtime_config.h`'s `LoadConfigFile()`: instead of streaming the
ifstream directly into `toml::parse`, read the whole file into a
`std::string` first and append a `'\n'` if it doesn't already end with
one, then parse that from an `istringstream`. This isn't Switch-specific -
any hand-edited or truncated `Config.toml` on any platform would hit the
same crash - so the fix applies unconditionally, not behind `__SWITCH__`.
Rebuilt the 12 objects that include the header (`main.cpp`,
`settings_overlay.cpp`, `hle/net/network_core.cpp`, etc.), relinked as
**v5** with the same pic-library recipe from Round 5, repackaged
`mkw_dev_final.nro`, re-uploaded. Awaiting device test.

### Round 7: same PC/LR offsets on v5 - the newline fix wasn't the bug

Tested v5. Identical crash, byte-for-byte same offsets (`PC = +0x1e400`,
`LR = +0x125d8`, inside `toml::detail::syntax::ws()`) - and the register
dump this time shows the config string ending `...= false\n` (`0a` byte
present), confirming the Round 6 newline fix *did* take effect but wasn't
the actual problem.

Disassembled the exact faulting instruction at `+0x1e400`:
```
mrs   x19, tpidr_el0
add   x0, x19, #0x1, lsl #12   ; x0 = x19 + 0x1000
add   x0, x0, #0xa40           ; x0 = x19 + 0x1a40
ldr   x1, [x0]                 ; <-- faults here
```
`X[00]` in the crash report is `0x1a40` - exactly `tpidr_el0 + 0x1a40`
*if* `tpidr_el0 == 0`. **The main thread's TLS base register isn't set up
yet.** This code is one of `toml::detail::syntax::ws()`'s several
`static thread_local` caches (`repeat_at_least`/`character_either`
lazy-initialized singletons - a `thread_local` per parser rule, by
design, for reentrancy). Accessing any `thread_local` this early - before
libnx has finished main-thread TLS setup - reads through a null base and
crashes, regardless of what's actually in Config.toml.

The trigger: `settings_overlay.cpp` had ~19 namespace-scope globals
(`g_rumbleEnabled`, `g_resolutionScale`, `g_frameInterpolationMode`,
`g_displayMode`, `g_wiiRemotesEnabled`, etc.) eagerly initialized straight
from `RuntimeConfigFile::*` calls - i.e. as C++ global-constructor
initializers, run by `_GLOBAL__sub_I__ZN16settings_overlay25...` as part
of `.init_array`, before `main()` and thus before libnx's main-thread TLS
setup completes. The first one in declaration order triggers
`RuntimeConfigFile::Get()`'s Meyer's-singleton first-time parse, hits
toml11's `thread_local` machinery, and crashes.

Fixed by converting all ~19 into plain compile-time-constant defaults
(matching each call's existing fallback argument) plus a new
`LoadPersistedSettingsFromConfig()` function that sets the real values
from `RuntimeConfigFile::*`, called as the very first line of
`InitializeRuntimeSettings()` - which only ever runs from `main.cpp:1444`,
safely after libnx startup. Moved `g_wiiRemotesEnabled`/
`g_wiiContinuousScan`'s declarations up next to the other defaults (they
were originally ~200 lines further down, same eager-init pattern) so
`LoadPersistedSettingsFromConfig()` can set them too. Checked every other
`.cpp` that includes `runtime_config.h` for the same pattern - none found;
this was isolated to `settings_overlay.cpp`.

Rebuilt `settings_overlay.o`, re-archived `libruntime.a`, relinked as
**v6** (same pic-library recipe), repackaged, re-uploaded. Awaiting device
test.

### Round 8: `thread_local` genuinely doesn't work under libnx at all - root cause confirmed

Tested v6. Identical crash, same offsets, still inside `toml::detail::syntax::ws()`'s
`static thread_local` cache. But the register dump now showed the
`...= false\n` config content ending WITH the trailing newline (Round 6's
fix took effect) - so it wasn't a parsing bug at all. Disassembled the
exact faulting instruction:
```
mrs   x19, tpidr_el0
add   x0, x19, #0x1, lsl #12   ; x0 = x19 + 0x1000
add   x0, x0, #0xa40           ; x0 = x19 + 0x1a40
ldr   x1, [x0]                 ; <-- faults here, Fault Address: 0
```
`X[00]` in the report is exactly `0x1a40` - only possible if `tpidr_el0`
itself is `0`. Confirmed this isn't a timing/ordering issue (not "runs
before libnx finishes setup"): after fixing Round 6's static-init crash,
the *next* crash traced back to `RuntimeMain()` itself - real `main()`
execution, well past startup - with the exact same fault. **libnx never
initializes `tpidr_el0` for compiler-emitted `thread_local` storage,
ever, on this platform.**

Verified this exhaustively rather than assume it:
- `armGetTls()` (libnx's own TLS accessor) reads `tpidrRO_el0` (read-only,
  kernel-managed 0x200-byte Thread Local Region used for libnx's internal
  `ThreadVars`/`_reent`) - a completely different register from
  `tpidr_el0` (read-write, the one AArch64 ELF `local-exec` TLS uses for
  compiler `thread_local`).
- Searched libnx's entire source tree (`switchbrew/libnx`, full repo tree
  listing) for every write to `tpidr_el0`: zero hits, anywhere, ever.
  `nx/switch.ld` does reserve a `.main.tls` section with `__tls_start`/
  `__tls_end` symbols sized for `.tdata`/`.tbss`, but nothing in crt0,
  `init.c`, or `thread.c` ever copies the template there or points
  `tpidr_el0` at it.
- The switchbrew wiki's Thread Local Region page states `tpidr_el0` is
  assigned to a `ThreadPointer` field "in threads created by sdk" - i.e.
  Nintendo's own proprietary SDK does this itself for official titles;
  libnx (homebrew) never reimplemented that part.
- Cross-checked against `yashin-sh/WiiCompiled-Switch` (an independent,
  actively-developed - 27 commits/24h - fork of the same upstream
  `patchzyy/Wiicompiled`, hardware-validated well past guest thread/context
  switching and into sustained EGG-subsystem execution): zero commits or
  code mentioning `thread_local`/`tpidr` at all. Consistent with never
  having relied on it in the first place.

**Fix: removed `thread_local` everywhere it was reachable at runtime on
this platform**, converting to plain (non-thread-local) storage, since
every real usage in this codebase turned out to be confined to a single
OS thread anyway:
- `runtime/third_party/toml11/toml.hpp`: all 49 `static thread_local`
  parser-rule caches -> `static` (immutable lazy singletons, single
  parse call, no concurrency to protect).
- `runtime/include/isa/ppc_isa_context.h`: `g_currentCpuContext` dropped
  `thread_local` entirely. Guest CPU execution only ever runs on the main
  host thread via cooperative fiber scheduling (`HostContext`/`libco`-
  equivalent custom `mkw_co_switch` in `platform/switch/co_switch.S`) -
  confirmed no other real OS thread (`std::thread` usage audited across
  the whole runtime: only audio mixing, stdout/stderr capture, and
  desktop-only media-session monitors, none of which touch guest CPU
  state) ever touches this. `host_context.cpp`'s own Switch/Apple path
  already avoided `thread_local` for `g_current` for the exact same
  reason (pre-existing code, with a comment explaining why) - this was
  the one remaining place using it unguarded.

Rebuilding this exposed a chain of stale-object link errors from files
that transitively include `runtime_config.h`/`toml.hpp`
(`nand_api/async/isfs/fs.cpp` via `nand_internal.h`, `hle/vi.cpp`, and -
once `ppc_isa_context.h` changed - all 89 translated shards, since every
shard references `g_currentCpuContext` through `CpuContextScope`). Ended
up doing a full clean rebuild of both `libtranslated.a` and
`libruntime.a` rather than chase individual stale objects one at a time.

**Build note:** this whole session runs inside Termux/proot on a phone.
An earlier `-j8` parallel shard rebuild overloaded the device and had to
be killed; restarted at `-j3`, which held up fine. The kill left 8
zero-byte truncated `.o` files sitting in `obj2/shard/` (mid-write when
killed) that a naive resume silently treated as "already built" and
skipped - causing a confusing batch of undefined-reference link errors
for symbols that should've been defined in those shards. Diagnosed by
checking file sizes/mtimes against the kill time, deleted every 0-byte
`.o` under `obj2/` (`find obj2 -name '*.o' -size 0 -delete`), rebuilt
just those, and relinked clean. Worth remembering for any future
interrupted build on this box: **always sanity-check for zero-byte
objects after killing a build mid-compile**, don't just resume blindly.

Relinked as **v7** (pic-library recipe, unchanged), repackaged
`mkw_dev_final.nro`, re-uploaded to `sdmc:/switch/mkw_dev.nro`. Awaiting
device test.

### Round 9: v7 got further - same TPIDR_EL0 bug, different variable

Tested v7. Real forward progress: no more relocation/config-parsing
crashes, and the crash moved to `SystemBridge::Initialize()`, called from
`RuntimeMain()` - past startup, into actual runtime init. But the
disassembly showed the exact same signature as Round 8's bug:
```
mrs   x0, tpidr_el0
add   x1, x0, #0x4, lsl #12
add   x1, x1, #0xdb8            ; tpidr_el0 + 0x4db8
add   x0, x0, #0x4, lsl #12
add   x0, x0, #0xdb0            ; tpidr_el0 + 0x4db0
str   d31, [x0]                 ; <-- faults, storing 0x1p-126 as a double
```
`0x1p-126` (`0x3810000000000000`) is `ppc_isa_fpenv.h`'s
`kMkwNiFlushThreshold` constant, written into `g_mkwNiFlushThreshold` -
another `thread_local` I hadn't caught in Round 8's pass, since I only
checked the files a first grep happened to turn up rather than doing a
complete sweep.

Did the complete sweep this time: `grep -rn thread_local` across all of
`runtime/src` and `runtime/include` turned up **~30 declarations across
19 files**, not the handful fixed in Round 8. Went through every one
individually rather than blanket-converting:

- Confirmed via call-graph tracing that two of them are genuinely read/
  written from more than one real host thread: `ax_internal.h`'s
  `t_onMixWorker` (an explicit "which thread am I" flag, read by the
  audio mix worker thread in `ax_mix.cpp` and by the main/guest thread)
  and its `ReadAramByte`'s function-local `AramWindow window` cache
  (explicitly called out in a comment in `ax_mix.cpp` as something "the
  worker... caches"). Plain (non-thread-local) storage for either would
  be a real, live data race between two actual OS threads, not just an
  unnecessary precaution - unlike everything else in this runtime.
- Checked one borderline case in depth before converting it:
  `os_sleep.cpp` has a *different* function (`ProcessSleepTimers`) in
  the same file with an explicit comment and atomic-CAS guard for
  running "on more than one host thread." Traced its only two call
  sites (`os_scheduler.cpp`) - both reachable only through guest
  dispatch, so still main-thread-only on this port. Treated as
  prophylactic caution rather than evidence of an actual second caller.
- Everything else (guest CPU/GX/OS emulation state, SEH recovery state,
  dispatch memoization caches, report/log dedup caches, VI/audio poll
  timers) is confined to the single main/guest-execution host thread -
  confirmed by auditing every real `std::thread` creation in the
  codebase (`main.cpp`: stdout/stderr capture; `music_attenuation.cpp`:
  Windows/Linux-only media session monitors; `ax_mix.cpp`: the one real
  audio worker) and finding none of them touch guest state.

**Fix approach:** added `runtime/include/mkw_thread_local.h`, a single
`MKW_THREAD_LOCAL` macro - real `thread_local` on every platform except
Switch, where it's nothing (ordinary storage duration), since libnx
never initializes `TPIDR_EL0` there (Round 8) and every one of these
~28 safe cases is confined to one thread anyway. Swapped `thread_local`
for `MKW_THREAD_LOCAL` at each safe site
(`fiber_manager.{cpp,h}`, `system_bridge.{cpp,h}`, `hle/vi.cpp`,
`hle/audio/audio.cpp`, `hle/gx/gx_dl.cpp` (4), `hle/os/os_alarm.cpp`,
`hle/gx/gx_vertex.cpp` (2), `hle/os/os_report.cpp` (5),
`hle/os/os_sleep.cpp` (6), `abi_bridge.h` (2), `recomp_mod_loader.h`,
`hle/storage/nand_async.cpp`, `isa/ppc_isa_fpenv.h` (2, the actual Round
9 crash) - one macro, one rationale, instead of scattering `#ifdef
__SWITCH__` explanations at 28 different call sites.

For the two real cross-thread cases, added `SwitchThreadLocalBool` and a
templated `SwitchThreadLocal<T>` to `ax_internal.h` (Switch-only,
`#if defined(__SWITCH__)`), both backed by `pthread_key_t` - libnx's
*real* pthreads support (added in libnx 2.1.0, confirmed working since
it isn't built on the same broken `TPIDR_EL0` mechanism as compiler
`thread_local`). Both wrapper types implement the same read/write
surface the plain variables had (`operator bool()`/`operator=` for the
bool flag, a `.Get()` accessor for the struct cache) so call sites in
`ax_mix.cpp`/`ax_memory.cpp` needed zero changes.

Rebuilding this touched `abi_bridge.h` and `recomp_mod_loader.h`, both
included by every one of the 89 translated shards (dispatch memoization
caches, `g_currentTranslatedExecutionAddress`) - full clean rebuild of
both `libtranslated.a` and `libruntime.a` again.

**Build note continued:** this build ran at `-j3` throughout (see Round
8's build note on why - Termux/proot on a phone, `-j8` overloaded the
device and had to be killed). No corrupted zero-byte objects this time
since it ran to completion uninterrupted.

### Round 10: v8/v9 exited cleanly (not a crash) - two missing runtime dependencies, then a real archive-linking bug

Tested v8. No Atmosphere crash report at all this time - the process
exited on its own after ~6 seconds. Found the actual cause via the
runtime's own crash-artifact system (`sdmc:/WiiCompiled/Logs/<run>/`,
written by `WriteFatalLogImpl` in main.cpp, separate from Atmosphere's
system-level reports): `crash_exception.txt` + `crash_exitcode.txt`,
i.e. a caught `std::exception` followed by the generic non-zero-exit
fallback in `AtExitHandler`. Both showed the same all-zero guest CPU
state template (`GetPersistentCpuContext()`'s fallback, used whenever
`TryGetCpuContext()` has no active scope) - meaning this fires from
host-side init, before any guest PPC code ever executes.

The actual exception message wasn't captured anywhere: `ex.what()` only
went to `std::cerr`, and the process-transcript pipe that's supposed to
capture stdout/stderr to `console.log` isn't actually working on Switch
(console.log only ever has the four startup-banner lines written
directly before the pipe redirect begins - a separate, not yet
investigated bug). Fixed the immediate diagnostic gap: changed
`WriteFatalLogImpl("exception")` to `WriteFatalLogImpl("exception",
ex.what())` in `main.cpp`'s `catch (const std::exception&)` block - one
line, and the crash log format already supported a `details` field that
just wasn't being populated for this specific catch site (the sibling
`access_violation` and `terminate` handlers already passed it).

Rebuilt/relinked as v9 with just that change and re-tested. First real
answer: *"Missing bundled Wii DSP coefficient ROM (dsp_coef.bin)"* -
`hle/audio/ax_mix.cpp`'s `FindDspCoefficientRom()` throwing because the
file was never deployed to the device. It lives in the source tree at
`runtime/assets/dsp/dsp_coef.bin` (4096 bytes) but nothing copies it to
`sdmc:/WiiCompiled/` (the `ApplicationDataDirectory()`-relative fallback
path it checks after the desktop-only "adjacent to executable" and
"source tree" ones). Uploaded it directly via FTP - no code change
needed, just a missing deployment step.

Separately (not blocking, but worth fixing before it became the next
surprise): confirmed there was no extracted Mario Kart Wii DATA
directory anywhere on the SD card, and `Config.toml` had no `[paths]`
section, so `dvd_root` was completely unset. `hle/storage/dvd.cpp`'s
`GetDvdRoot()` fails this case cleanly (`FailDvdRoot()`, a direct
`std::exit()` with its own `crash_dvd_root.txt` artifact, not the
generic exception path) - so this would have been immediately
diagnosable on its own once reached, but was resolved proactively.
Found a real, valid extracted DATA directory already present locally at
`/home/proot-dev/switch/Assets/DATA` (2.6 GB, 2043 files, confirmed
`RMCP01` - PAL - via the disc header at `sys/boot.bin`, matching what
`kDefaultEntryAddress` in `system_bridge.h` assumes). Uploaded the whole
tree to `sdmc:/WiiCompiled/DATA` via a resumable FTP script
(`/tmp/opencode/upload_dvd_data.sh` - tracks per-file completion in a
state file so a dropped connection mid-transfer resumes instead of
restarting; needed twice, since the console's FTP server dropped
mid-session for unrelated reasons). All 2043 files uploaded clean, zero
failures. Added `[paths]` `dvd_root = "DATA"` to `Config.toml`.

Rebuilt v9 (just the `ex.what()` logging change) and re-tested with both
fixes in place. New, more interesting failure: *"No translated function
registered at address 0x800060a4"* - from `ResolveEntry()` in main.cpp,
looking up the real PAL entry point via
`TranslatedFunctionRegistry::FindByAddressPtr()`. This address
definitely exists in the generated data - `func_800060A4`'s real body is
in `base_common/shard_067cb4790f9deaab9d250f40.cpp`, and it's registered
in `base_registration/registration_08_7470f2f18eeed4a6.cpp` - so this
wasn't a translation gap, something was silently dropping registration
data at *link* time.

Root cause, confirmed empirically via a link map: `registration_08_...`
(like all 16 `base_registration` files) defines its `kRecords` array and
the `BulkTranslatedFunctionRegistrar kRegistrar` instance that walks it
and calls `TranslatedFunctionRegistry::Register()` for each entry
**inside an anonymous namespace** - internal linkage, so the object file
has zero externally-visible symbol *definitions* (only `extern`
declarations of the real `func_XXXXXXXX` bodies, which live in
`base_common`). Our link puts `libtranslated.a` inside
`--start-group`/`--end-group` as an ordinary static archive; GNU ld only
ever extracts an archive member when something else needs an undefined
symbol *from that specific member*. Since nothing anywhere references
any symbol `registration_08...o` defines, the linker never had a reason
to pull it out of the archive at all - confirmed by grepping a fresh
link map for `registration_08_7470f2f18eeed4a6.o`: zero hits. Its
`.init_array` entry (the actual `kRegistrar` constructor, which is what
would have called `Register()` for every one of its ~1750 records) never
even had a chance to run, because the whole object was invisible to the
linker's archive-resolution pass. This has nothing to do with
`--gc-sections`/`KEEP()` (stock `libnx/switch.ld`'s `.init_array`
handling is fine, standard `KEEP()` usage) - the object was never
*extracted* from the archive in the first place, so section-level GC was
never even in play.

The original CMake build never hits this: a comment in
`runtime/cmake/PublicProducts.cmake` (near where `mkw_base_shared` gets
linked) says outright that "the dispatch-table and registration shards
compile inside the product target itself" - i.e. upstream compiles
`base_registration`/`base_dispatch` directly into the final executable
target as ordinary object files, never inside the `mkw_base_shared`
*static* archive that the real function bodies (`base_common`) live in.
Object files passed directly to a link are never subject to archive
member extraction, so this whole class of bug can't occur there. Our
Makefile2 (written for this port, from scratch) instead archives
*everything* - `base_common`, `base_dispatch`, and `base_registration`
alike - into one `libtranslated.a`, which is what exposed this.

Fixed by linking `libtranslated.a` with `-Wl,--whole-archive ... -Wl,
--no-whole-archive`, forcing every member in unconditionally regardless
of whether anything else references it - which isn't just a workaround
here, it's the *correct* link strategy for a statically-recompiled
game's translated-function archive specifically: guest code can reach
any translated function via a computed/indirect branch resolved only at
runtime, so no static reachability analysis the host linker could do
would ever be safe to rely on for deciding what to keep. Verified via
the link map: all 16 `base_registration` files now present (previously
only whichever ones happened to satisfy some other symbol reference
incidentally). Final binary grew from ~106 MB to ~109 MB - modest, not
the blow-up a naive "just include everything" fear might suggest.

Relinked as **v10**, not yet packaged/uploaded as of this note.

### Round 11: v10 crashed for real (guest execution reached at last) - the flat-memory-view assumption breaks on Switch

Tested v10. First genuine Atmosphère-level hardware crash of the whole
port (Data Abort, real fault) - and a good one: PC/LR resolved to
`func_800211E4` called via `InvokeIndirectCpu` from
`SystemBridge::Initialize()`. **This is real translated Mario Kart Wii
PPC code executing on Switch hardware for the first time** - every prior
round was host-side C++ init failing before guest code ever ran.

Disassembled the fault site directly rather than guess from the register
dump (same method as the TPIDR_EL0 rounds): the instruction is
`str w6, [x0, w3, uxtw]`, a raw pointer store with no bounds check at
all. Traced it to the generated shard source
(`base_common/shard_f818dc72cb0c0a8f7565b37f.cpp`,
`func_800211E4`) - it's `MemoryInline::FlatWriteRam32((r1 + -16), r1)`, a
classic PPC `stwu`-style stack-frame prologue (`r1` is the guest stack
pointer, seeded to `0x81700000` in `main.cpp`'s `SeedCpuContext`). The
computed host address (`0xC5D9A5000 + 0x816FFFF0 = 0xCDF0A4FF0`) matched
the crash report's `Address` field exactly, confirming the "Fault
Address: 0" field in Atmosphère's format is something else (looks like
the IPA, which comes back 0 for a plain translation fault) - `Address`
is the real faulting VA. `0x816FFFF0` is a completely ordinary MEM1
address (`0x80000000`-`0x81800000`, standard Wii layout, correctly
configured in `Memory::Config::WiiDefaults()`) - so this isn't a
config/seeding bug, it's the backing memory itself.

Root cause: `guest_flat_memory.cpp` already has a comment stating "the
flat guest view is intentionally NOT backed on Switch" - no user-space
SVC can alias one physical backing store at a second VA on this firmware
(`svcMapMemory` rejects destinations above 2 GiB, `svcMapProcessMemory`
refuses the pseudo-handle) - so `MapGuestView()` is a no-op on Switch:
the 4 GiB `g_base` VA reservation from `virtmemFindAslr` is never
actually mapped to anything. Every *ordinary* memory op already knows
this and correctly checks `GuestFlat::RequiresCheckedAccess()` (forced
`true` on Switch, confirmed correctly wired in `Initialize()`) before
falling back to the real, working `Memory::Read/Write` page-table path.
But `MemoryInline::FlatWriteRam8/16/32/RamFloat32/RamFloat64` in
`memory_access.h` are a *separate*, deliberately unchecked fast path -
"emitted ONLY for addresses the translator proved at translate time are
ordinary guest RAM (r1-relative stack slots, ~45% of flat stores)" -
and they call `FlatStore<T>` (raw `MKW_FLAT_GUEST_BASE + address`
pointer arithmetic) unconditionally, never consulting
`RequiresCheckedAccess()` at all. The stack-prologue write that crashed
is exactly this fast path. No read-side equivalent exists (`FlatRead32`
already checks the flag correctly; only writes have the unchecked "Ram"
family) - confirmed by grepping the whole runtime for "ReadRam".

Cross-checked against `yashin-sh/WiiCompiled-Switch` (same upstream
tool+game, independently further along - already past this exact class
of problem) for how they solved guest memory backing on Switch, at the
user's suggestion, borrowing the *technique* only (not the code): they
never back the `g_base` token at all either - it's an inert VA
reservation, never dereferenced. All real backing is ordinary
`calloc()`, one buffer per distinct physical region, with MEM1's
physical/cached/uncached (and MEM2's) views intentionally *aliased onto
the same buffer* by masking the guest address (`base & 0x1FFFFFFFu`)
inside a `host_pointer()` lookup - no OS-level aliasing SVC needed at
all, since the "same physical pages, multiple VAs" property only matters
if two views are read through raw pointers simultaneously; routing every
access through one lookup function sidesteps the need for it entirely.
This is architecturally identical to what this codebase's own
`Memory::Read/Write` (`section.hostView`-based) path already does - the
`aligned_alloc`-backed `Section` objects `guest_flat_memory.cpp` already
creates for Switch are exactly this same real backing, just currently
only reachable through the checked path, not the flat one.

Fix: rather than reimplement flat-view backing (a much larger change,
and the checked path already works correctly), made the unchecked
`FlatWriteRam*` family respect `RequiresCheckedAccess()` exactly like
every other Flat* function already does - one `if` per function, five
functions, in `memory_access.h`. Since `RequiresCheckedAccess()` is
already unconditionally `true` on Switch, every one of these now
transparently routes through the real, working `Memory::Write*` path
there, while every other platform (where the flat view genuinely is
backed) keeps taking the original zero-overhead unchecked branch -
`RequiresCheckedAccess()` on those platforms is `constexpr false`, so
the compiler still folds the check away entirely and the "check-free"
promise the comment describes is uncompromised anywhere but Switch.

`memory_access.h` is included by essentially the whole codebase (every
translated shard touches guest memory), so this needs a full clean
rebuild of both `libtranslated.a` and `libruntime.a` again. Rebuilding
at `-j3` as of this note; not yet relinked, packaged, or tested.

Relinked as **v11**, packaged, uploaded. Tested: no crash report at all
(neither Atmosphère nor the runtime's own artifacts) - meaning the
memory fix worked and execution continued. Instead, a new, different
runtime-detected failure: `crash_missing_target.txt` -

> The game stopped because it tried to execute guest address
> 0x801a25d0, but that function was not translated or registered.

`0x801A25D0` is `OS__Report_801a25d0` - Wii's `OSReport` debug-print
function, registered via `PPC_NATIVE_OVERRIDE_VOID` in
`hle/os/os_report.cpp`. **Same exact bug as Round 10, different
archive.** `REGISTER_NATIVE_FUNCTION` (the macro
`PPC_NATIVE_OVERRIDE_VOID`/`PPC_NATIVE_OVERRIDE` expand to) is a
namespace-scope `static AbiTrampoline<...>` object - internal linkage,
side-effecting constructor, exactly the same pattern as the bulk
`BulkTranslatedFunctionRegistrar` from Round 10, just for individual
native (non-translated) HLE bridge functions instead of bulk-registered
translated ones. `os_report.cpp` lives in `libruntime.a`, which Round
10's fix never touched (only `libtranslated.a` got `--whole-archive`) -
so `os_report.o` was never pulled from the archive, since nothing calls
`func_801A25D0` by name (only the guest, indirectly, through the
registry).

Audited comprehensively before just patching this one symptom: grepped
for every registration-style macro in the codebase
(`REGISTER_TRANSLATED_FUNCTION`, `REGISTER_NATIVE_FUNCTION`,
`REGISTER_NATIVE_FUNCTION_AS`, `PPC_NATIVE_OVERRIDE`,
`PPC_NATIVE_OVERRIDE_VOID`) and confirmed all of them expand to the same
internal-linkage static-object pattern, then found every file using any
of them: `REGISTER_TRANSLATED_FUNCTION` only in `hle/os/os_init.cpp`,
`PPC_NATIVE_OVERRIDE`/`REGISTER_NATIVE_FUNCTION` across 44 files, every
one of them under `runtime/src/hle/**` - i.e. entirely within
`libruntime.a`. Also checked `libswitchext.a` (CryptoPP/third-party):
zero uses of any registration macro, confirming it never needed
`--whole-archive` in the first place.

Fixed by extending `--whole-archive` to `libruntime.a` too (still
excluding `libswitchext.a` - tried including it as well first, and it
immediately broke: `multiple definition of 'ECCRYPTO_FNAME'` between
`eccrypto.o` and `cp_eccrypto_inst.o`, two legitimately-alternate
CryptoPP template-instantiation objects that rely on ordinary
archive-style "only pull what's needed" semantics to avoid colliding -
confirming it's correct for that archive specifically to stay as a
normal selective-link archive). Relinked clean as **v12**
(`--whole-archive libtranslated.a libruntime.a --no-whole-archive
libswitchext.a` inside the same `--start-group`), verified via the map
file that `os_report.o` is now present (33 references, was 0 before).
Binary grew from ~109 MB to ~113.5 MB.

Also audited for other instances of Round 11's bug class (unchecked
flat-memory access bypassing `RequiresCheckedAccess()`): found one more
call site touching `MKW_FLAT_GUEST_BASE` directly
(`isa/ppc_isa_quantized.h`'s paired-single quantized load/store fast
path) and confirmed it was already correctly guarded (returns `nullptr`
under checked access, caller falls back properly) - the `FlatWriteRam*`
family fixed in Round 11 was the only gap.

Built and packaged v12; not yet uploaded/tested as of this note - away
from the console. Both fixes in this round address whole classes of
bug (every native/bulk registration macro; every unchecked flat-access
site), not just the two specific symptoms that happened to surface, so
the expectation is this clears out this entire failure mode rather than
trading one more missing-function report for another.

## Wiring real Aurora/GX in (replacing switch_stubs_gx.cpp's no-ops)

Dawn-for-Switch is now confirmed alive end-to-end on real NVK/Tegra X1
hardware (see `dawn/docs/switch-port-notes.md` for that whole chain -
NVK env-var gate, `--whole-archive` link fix, `vkEnumerateInstanceLayerProperties`
optional-proc fix, a real 180-frame color-cycling present demo). This
section is about wiring the *real* Aurora GX backend into WiiCompiled in
place of `switch_stubs_gx.cpp`. Doing this work on a dedicated branch
(`aurora-gx-wireup`, both this repo and `dawn`) given how experimental it
is and how much is already uncommitted on `main`.

### Real dependency scope (aurora_core CMake target)

`aurora-main/cmake/aurora_core.cmake` + `extern/CMakeLists.txt` need,
beyond Dawn itself: **fmt, sqlite3, Tracy (TRACY_ENABLE=OFF), imgui**, and
(if `AURORA_ENABLE_GX`, which is needed) potentially **freetype/libpng/zstd**.
All are `FetchContent`-based (fetched from GitHub, not requiring
preexisting portlibs) except abseil, which Dawn's own build already
vendors and builds (`third_party/abseil` in the Dawn build tree) - point
aurora at that instead of refetching. `imgui`/`xxhash` we've already built
once for `switch_stubs.cpp`/`switch_stubs_gx.cpp`'s object set.

`aurora_core` also requires `lib/window.cpp` + `lib/input.cpp`, which are
written against **real SDL3** - no Switch SDL3 build exists (confirmed:
no portlib, and this is why `switch_stubs.cpp`'s ~28 `SDL_*` stubs exist
at all). These two files need a Switch-native replacement using the same
`nwindowGetDefault()` approach the Dawn smoke test's present-loop demo
already proved works, rather than trying to get real SDL3 building for
Switch.

### sqlite3: use nx-mod/sqlite-nx's real VFS, not aurora's vanilla FetchContent

`aurora_core.cmake` fetches vanilla `sqlite3.c` from sqlite.org and
compiles it as-is against a generic POSIX VFS - risky, since newlib's
POSIX/stdio layer on the sdmc: devoptab has already caused real bugs
tonight (`std::filesystem::copy_file` returning ENOSYS - see the
NAND-bootstrap section above). `nx-mod/sqlite-nx` (the user's own repo,
default branch, commit `5e1ed7d7` "Merge Switch port from TriPlayer's
vendored SQLite (nx-vfs + build)") has a real answer: `source/nx-vfs.c`,
a custom `sqlite3_vfs` implementation built directly on libnx's
`FsFileSystem`/`fsFile*` calls, bypassing newlib's POSIX layer entirely.
Pulled `nx-vfs.c` + that repo's `Makefile` down to
`/tmp/opencode/sqlite-nx/` for reference. Plan: use this VFS (registered
via `sqlite3_vfs_register`) instead of whatever aurora's vanilla fetch
would default to, when sqlite3 actually gets wired into the aurora_core
build.

### Plan (not yet executed as of this note)

1. Configure `aurora-main`'s own CMake against the Switch toolchain
   (`build-switch-toolchain.cmake`, reused from the `dawn` repo), pointing
   `AURORA_DAWN_PROVIDER` at the already-built `dawn/build-switch` tree
   rather than letting it `FetchContent` and rebuild Dawn from scratch.
2. Let `FetchContent` pull fmt/sqlite3/Tracy/imgui and attempt a build;
   fix cross-compile issues as real errors surface (same methodology as
   the whole Dawn/NVK chain tonight - do not pre-guess fixes).
3. Write Switch-native `window.cpp`/`input.cpp` replacements once the
   rest compiles, using `nwindowGetDefault()` + libnx `hid` instead of
   SDL3.
4. Only then: remove `switch_stubs_gx.cpp`'s no-ops and
   `switch_stubs.cpp`'s `aurora_*`/`Aurora*` stubs, replacing them with
   the real `aurora-main` static library in WiiCompiled's own link line
   (with the same `--whole-archive` treatment for `libnvk.a` proven
   necessary tonight - this bug will silently reappear here too if
   forgotten).

### Follow-up idea: audit our own file I/O for the same class of bug

`nx-vfs.c`'s whole reason to exist is that newlib's generic POSIX/stdio
layer over the sdmc: devoptab has real gaps (no file locking, no
truncation, etc.) - the *exact* same class of bug as tonight's
`std::filesystem::copy_file` → `ENOSYS` fix in `nand_path.h`. Worth an
audit pass over WiiCompiled's own remaining `std::filesystem`/stdio call
sites once the Aurora wiring settles, specifically for other APIs that
might silently no-op or fail on this devoptab rather than throwing
something we'd notice immediately (same shape as the copy_file bug: it
returned a real error code, but only because libstdc++ happened to
surface it as one - a silent no-op would be much harder to catch).

## Real CMake build for Switch (replaces the /tmp Makefile2 + switch_stubs approach)

**Why this section exists:** everything in "Build layout" above (Makefile2,
`switch_stubs.cpp`, `switch_stubs_gx.cpp`, `obj2/`) lived in
`/tmp/opencode/mkwwitch/` and was **lost when `/tmp` was wiped** between
sessions. Nothing there was ever committed. Do not rebuild that approach.
`runtime/CMakeLists.txt` already has first-class `MKW_PLATFORM_SWITCH`
support and a real `MKW_BUILD_PRODUCTS=ON` path that links the translated
shards against the *real* `aurora::gx/pad/si/vi/mtx` targets, so no GX stubs
are needed at all.

**Where things live now (nothing important in /tmp):**
- Build tree: `/home/proot-dev/switch/dawn/build-switch` (Dawn's CMake project
  add_subdirectory()s both `aurora-main` and `wiicompiled/runtime` so they all
  reuse the one patched Dawn instead of FetchContent'ing a vanilla copy).
  Target is `WiiCompiled`; output is an ELF, still needs `elf2nro`.
- Logs + helper: `wiicompiled/build-mkwwitch/` (gitignored via `/build-*/`):
  `build.log`, `cmake.log`, `safe_build.sh`.

**Termux crashes during the link - root cause and fix.** The device has
~7 GB RAM and Android kills Termux when it runs low. `libmkw_base_shared.a`
is 2.1 GB and `libwebgpu_dawn.a` 1.7 GB, almost all `-g` DWARF. Linking that
as-is is what crashed Termux, not the compile `-j` level. Fix (link-only, no
recompile): `CMAKE_EXE_LINKER_FLAGS_RELEASE="-Wl,--strip-debug
-Wl,--no-keep-memory"`. Always build through `build-mkwwitch/safe_build.sh`,
which runs `ninja -j1` under `nice` and kills the compiler/linker if
`MemAvailable` drops below 600 MB, so an OOM is a failed step, not a dead
Termux. First successful link peaked around 3.5 GB RSS.

**Compile fixes needed for GCC (devkitA64), all in tracked source:**
- `-fno-slp-vectorize` is Clang-only: guarded in `PublicProducts.cmake`.
- `MKW_PPC_ALWAYS_INLINE_BODY` (`isa/ppc_isa_config.h`) lacked `inline`; GCC
  refuses `always_inline` on an externally-linked function under `-fPIC`
  ("function body can be overwritten at link time"). Added `inline`.
- That fix caused a regression: an `inline` function gets no out-of-line copy,
  but 23 of the 72 shards define `func_XXXX_statefree_vN` helpers that OTHER
  shards call by symbol -> ~49 undefined references at link. Fix: only those
  23 shards are compiled with `-fkeep-inline-functions` (found at configure
  time with `grep -l` in `PublicProducts.cmake`), which emits a weak copy.
  Verified with `nm` (`W func_806212FC_statefree_v0`). Proper long-term fix is
  probably `visibility("hidden")` on the macro instead, but that changes a
  header included by every shard (full rebuild), so it was deferred.

**Aurora on Switch (aurora-main, all tracked):** `window_switch.cpp` /
`input_switch.cpp` / `imgui_switch.cpp` / `imgui_config_switch.cpp` replace the
SDL3-coupled files; `AuroraSDL3Provider.cmake` is skipped; SDL3 is used as
headers only (`dawn/build-switch/_deps/sdl-src/include`); Tracy is an
INTERFACE stub (TRACY_ENABLE off; its TracySystem.cpp has no Horizon branch);
sqlite3 builds with `SQLITE_OMIT_WAL SQLITE_OMIT_LOAD_EXTENSION`; core Dear
ImGui is built from `audit_deps/imgui-1.91.9b-docking` without backends.

**SDL shim:** `runtime/platform_switch/switch_sdl_shim.cpp` defines the ~50 SDL3
entry points still referenced (real stdio-backed `SDL_IOStream`; "no device"
for joystick/gamepad/keyboard/mouse - real controller input is libnx
`PadState` in `wii_remote_input.cpp`).

**NVK link:** `dawn/CMakeLists.txt` attaches `switch_smoke_test/nvk_switch_stubs.cpp`
and `--whole-archive -lnvk` + `-lnvk_support -lz` to `WiiCompiled`. The stubs
file now also has `pipe`/`fchown` and a constructor that sets
`NVK_I_WANT_A_BROKEN_VULKAN_DRIVER=1` before `main`.

**Status:** all compile units build; first link reached ld and failed only on
undefined references (SDL shim, statefree helpers, NVK) which the above address.
Not yet run on hardware.

### Update: first full link succeeded (real Aurora + Dawn + NVK)

The statefree helper regression had three return types (`MkwStateFreeResult2`,
`uint64_t`, `void`), not one; the configure-time `grep -lE` in
`PublicProducts.cmake` now matches any return type -> 33 of 72 shards get
`-fkeep-inline-functions`. Final link: **0 undefined references**,
`WiiCompiled.elf` 140 MB (`text` 126 MB), contains the real `aurora_initialize`,
Dawn `wgpuCreateInstance`, `vk_icdGetInstanceProcAddr` and the SDL shim.

Packaged: `wiicompiled/build-mkwwitch/mkw_dev.nro` (133 MB) with
`nacptool --create "Mario Kart Wii" "nx-mod" "0.2-aurora"` and libnx's
`default_icon.jpg`. **The Mario Kart Wii icon was in /tmp and was lost** - it
needs to be re-supplied (the installed forwarder NSP still has it embedded).

Rebuild recipe: `build-mkwwitch/safe_build.sh` (ninja -j1 + memory watchdog),
then `elf2nro dawn/build-switch/WiiCompiled.elf mkw_dev.nro --icon=... --nacp=...`.

**Not yet run on hardware.** Upload attempt failed: console FTP
(10.109.156.168:5000) timed out. Expect new failures once it boots - this is
the first time real Aurora/GX + NVK are in the game binary.

### Boot ordering: guest OS init runs BEFORE aurora_initialize

Worth knowing before chasing any "blank screen" further. In `RuntimeMain`,
`SystemBridge::Initialize()` (main.cpp ~1396) runs well before
`aurora_initialize` (~1487), and it *executes guest PPC code* - the first
on-hardware crash report of this branch resolved to `func_800211E4` via
`InvokeIndirectCpu` from `SystemBridge::Initialize()`. So the `[OSReport]`
lines in console.log (NW4R/Revolution OS banners) are MKW's own OS init
running while Aurora does not yet exist.

Consequence: a run whose log ends after those OSReport lines has **not
reached the renderer at all**, and the blank screen is a guest-side hang, not
a presentation bug. It also explains the earlier `settings_overlay::Draw()`
crash - the VI retrace path (`VIWaitForRetrace` -> `VI_HLE_PollRetrace` ->
`AdvanceRetrace` -> `settings_overlay::Draw`) calls
`aurora_wait_for_frame_worker()` and ImGui from guest OS init, i.e. before
`aurora_initialize`. There is no aurora-ready guard anywhere in vi.cpp or
settings_overlay.cpp; if that turns out to matter, that guard is the fix.

Also note ImGui is invisible on Switch by design right now: `imgui_switch.cpp`
builds draw lists and drops them (no backend), so the strap/startup screen and
FPS overlay will not appear even when everything else works. Do not read
"no UI" as a failure.

Diagnostics added for this (all in the current build):
- `[boot] SystemBridge::Initialize enter|done`
- `[boot] aurora_initialize enter|done, fb=WxH`
- `[heartbeat] t=Ns guest=0xADDR retraces=N presents=N gxcopies=N` (1 s, then 5 s)

Reading one run: `enter` without `done` on SystemBridge = hung in guest OS
init, and the heartbeat's guest address says where. Stuck at
`aurora_initialize` = Dawn/NVK init. Past it with `gxcopies=0` = guest never
draws. `gxcopies` rising = frames drawn but not reaching the panel.

## 2026-09-19: strap screen reached; pink flicker and 12.5s freeze

- Game boots to MKW's Wii Remote strap screen (disc index fix + fiber stacks).
- **Pink hue is not a colour-channel bug.** NVK's Switch WSI maps
  VK_FORMAT_R8G8B8A8_UNORM to NvColorFormat_A8B8G8R8 / PIXEL_FORMAT_RGBA_8888,
  which matches. Switch screenshots of the pink moment come out correct bright
  white (or black/dimmed), so the display alternates correct frames with other
  frames. Suspect: VI `black=1` at every sampled retrace through the strap
  screen, so AdvanceRetrace's `shouldPresentBlack` path presents black frames
  between GXCopyDisp's presents. VISetBlack/VIFlush traces added to confirm.
- Frame interpolation is off by default (`FrameInterpolationFps` 0), not it.
- **Freeze at ~12.5s** in 4/4 runs, right after the title-scene disc reads; the
  VI retrace log stops after retrace=240. Added `[retrace] guard STUCK ... cb`
  (a retrace callback that never returned) and a 2s `[idle] pulse` from the
  scheduler idle loop: pulses continuing = guest threads deadlocked; silence =
  a guest thread spinning outside the scheduler.
- netlog.txt interleaves several runs; split them on `[boot] transcript initialised`.
- Boot loading text is plain "LOADING". The ~4.7s disc scan (a stat per file
  over 2037 SD files) now runs in main.cpp via DVD_HLE_PrescanDisc() before
  aurora_initialize, so it happens under the loading text; the guest's DVDInit
  skips the scan when it is prescanned.

## 2026-09-19 (later): boot livelock fixed, save-creation still failing

### VBlank livelock (fixed - this was the black screen after ~12s)
Retraces were only polled from the scheduler's idle loop, and that loop exits
early whenever the reschedule-pending mask is non-zero. Once two guest threads
kept each other runnable, the poll was skipped on every pass, VBlank stopped
entirely, and the thread waiting on the retrace queue could never wake. The
counters showed it exactly: sel/idle/fib climbing, `retrace` frozen.

Fix: `VI_HLE_PollRetrace` is now the FIRST statement of the idle loop body, ahead
of the early exits (os_scheduler.cpp).

Rejected first attempt: polling from `SelectThread` entry. Every retrace wakes
the retrace-queue thread, so the scheduler then picked it forever and every
other thread starved - boot did not even reach the first disc read. A 50ms
"rescue" throttle did not help either. `VI_HLE_PollRetraceIfDue` survives in
vi.cpp but is unused; keep it only if a use case appears.

### Switch-specific filesystem traps (all hit in one session)
- `std::filesystem::copy_file` is ENOSYS on Horizon (no copy_file_range/sendfile).
  It silently disabled the NAND write shadows AND failed `NANDSafeOpen`'s scratch
  copy, which is what made MKW report unreadable system memory. `NandCopyFileBytes`
  is the fallback; all three copy sites use it. nand_path.h already had its own
  stream-copy workaround - a hint that was missed.
- FAT cannot `rename` onto an existing file. Save commits failed until
  `AtomicReplaceHostFile` learned to remove the target first (non-atomically, and
  it logs when it does).
- console.log is buffered and the app is killed before it flushes, so NAND and
  OSReport messages never reached the card. Both now mirror to the durable UDP
  log; that is the only reason the above was findable.

### First-run NAND files (new: runtime/include/nand_first_run.h)
The Wii Menu writes these per-console files and no payload can ship them;
Dolphin generates them too (its Data/Sys/Wii has only the WC24 tree, same as
runtime/assets/wii). Generated into a new managed NAND:
- `/shared2/menu/FaceLib/RFL_DB.dat` - empty Mii database, RNOD + RNHD magics and
  a CRC-16/CCITT over the first 0x1F1DE bytes. Its absence failed RFL's read.
- `/shared2/sys/SYSCONF` - defaults matching the SC HLE overrides (16:9, PAL60,
  English), plus IPL.SADR (the game logs "Can't get SimpleAddressData" without it).

### Boot work moved under the loading banner
`NAND_HLE_PrepareHostRoot()` and `DVD_HLE_PrescanDisc()` run before
aurora_initialize, so the NAND seeding and the ~4.7s disc scan happen while the
banner is on screen instead of against a black one.

### Still open
- **Save creation.** The game creates rksys.dat, writes 0x2BC000 zero bytes (its
  own format pass - the buffer it hands us really is zeroed, while the banner
  write next to it carries real "WIBN" data, so our pointer translation is fine),
  closes, and then errors instead of writing the real save. No NAND call fails.
  Next: find the guest caller of the save path in generated/ and read its error
  branch; the game prints nothing before the error screen.
- **Shader caches never open** ("unable to open database file"), so every shader
  is recompiled each launch. Cause: the caches use SQLite's default VFS, which
  wants POSIX locking Horizon lacks. aurora has an SDL-backed VFS
  (pipeline_cache.cpp, `SdlVfsName`) that works there, but its write/truncate are
  stubbed SQLITE_READONLY. Extending it and using it for both caches is the fix.

### Save creation failed because networking was disabled (fixed)
MKW reported unreadable system memory right after "Saving". The save itself was
always fine: the game writes 0x2BC000 zero bytes as its own format pass (the
buffer it hands us really is zeroed; the banner write beside it carries real
"WIBN" data, which is what proved our pointer translation correct), and no NAND
call ever failed.

The game's own code named the culprit. `RKSYS::Mgr::ReplaceBinary` ->
`NandMgr::DeleteRKSYS` -> `NandMgr::CreateRKSYS`, and after the create succeeds
DeleteRKSYS calls 0x80672CC8, which suspends the WiiConnect24 scheduler through
`/dev/net/kd/request`. `Network_HLE_OpenDevice` refused every `/dev/net` node
when `[network] enabled = false`, including the two KD nodes that are local
(scheduler and clock bias), so that call failed and the game turned it into a
save error. Only the networked nodes are gated now.

Reading the translated game code in generated/functions with the symbol table is
what found this - the game prints nothing before its error screen, and every
HLE-side log was clean.

## Ideas / wishlist (not started)
- **Pre-boot settings menu**: set Wii system settings (language, aspect, 50/60Hz,
  sound) and port options before the game boots, while the banner is up. The
  generated SYSCONF makes these real settings rather than hardcoded HLE answers.
  (Note: the game still logs "Can't get SimpleAddressData", so the generated
  SYSCONF layout needs another look - probably the 0x3FAE lookup table.)
- **On-screen ImGui overlay**: the settings overlay, FPS and shader-compilation
  status already exist and are built every frame, but Switch has no ImGui
  renderer - imgui_switch.cpp drops the draw lists. Aurora's own WebGPU ImGui
  backend is wired to SDL; porting it would give the PC port's in-game menu.
- **Enhancements**: higher internal resolution, widescreen handling, texture
  filtering, frame interpolation (already in aurora, off by default), and the
  usual quality options. All want the overlay above to be usable first.
- **Switch Mii bridge**: read the console's Miis and convert them into the
  generated RFL_DB.dat so the player's own Mii becomes their license.
- **Shader cache persistence** (see above): the single biggest startup win.

## Performance plan (to full speed at stock clocks)

Baseline: races run ~4x too slow at 1020 MHz. Raising the GPU clock changes
nothing; the work per frame is ~55 ms of CPU against a 16.6 ms budget (the
idle-loop share is the game waiting for the next VBlank after a late frame).
Not the culprits (checked): TLS (plain globals on Switch), direct-call
dispatch (statically resolved), `ApplyRuntimeCallOptions` (constant-folds),
paired singles (NEON + FMA), the FPCR writes (only on FP mode changes).

### 0. Attribution build (in progress)
Every translated call now records its guest address and every native (HLE)
call its target, in plain globals (`RecompMod::ScopedGuestExecution` /
`ScopedNativeExecution` in recomp_mod_loader.h). The 1 kHz watchdog sampler
reports, per 10 s: the game-code vs native-runtime split, and the top 15 on
each side (`[prof] split:`, `[prof] game #n`, `[prof] native #n`; developer
log only). This decides between 1 and 2.

### 1. Dual-core GX (if native GX dominates)
Today GX calls only append to aurora's FIFO buffer (gx/fifo.cpp), but
`fifo::drain()` -> `process()` decodes the whole stream - state, vertex
conversion, draw recording - synchronously on the game thread at every sync
point (GXDrawDone, display lists, end_frame, copies). The frame worker only
encodes/submits/presents. Plan: drain() hands the buffer to a decode thread
and returns; block only where the CPU consumes GPU results (GXDrawDone
semantics, EFB peeks, texture copies read back by the CPU). Guest memory read
asynchronously - the same trade Dolphin's dual-core mode makes.

### 2. Translator code quality (if game code dominates) - mostly done upstream
Checked: the translator already fuses a compare into its branch when nothing
else reads the CR field (FlagElision; ~45k direct compare-branches in
generated/ vs ~75k remaining SetCRResident, the cases fusion cannot prove
safe), and the CLI enables leaf ABI spill elision. Remaining headroom is in
the harder cases: fusing across more block shapes, and register liveness for
non-leaf calls (9,880 functions still carry gpr_read=0xFFFFFFFF). Real work in
upstream code, benefiting every platform; do it only if the profile says game
code dominates and 1/3 are exhausted.

### 3. PGO + LTO (any case, on the next full rebuild)
devkitA64 GCC 16.1 ships libgcov and the LTO plugin. Instrumented build with
-fprofile-generate; at runtime set GCOV_PREFIX to sdmc:/WiiCompiled/pgo and
call __gcov_dump() on a timer (HOME kills the process, so atexit never runs);
play a race; copy the .gcda back; rebuild with -fprofile-use.

### 4. Direct guest RAM access
Switch forces g_requiresCheckedAccess (no second VA alias of the same memory
is possible). Map MEM1/MEM2 at fixed offsets in one reserved range and use
the flat path for RAM, checked path for everything else. Modest: the checked
read is an inlined bias-table lookup, a few instructions, not a call.

### 5. Native replacements for hot game routines
Driven by the `[prof] game` list: matrix math, decompression (decodeSZS),
THP decode are the usual candidates.

## Shader caches persist (sqlite-nx)

Aurora's SQLite builds on Switch with sqlite-nx's `nx-vfs.c` (`SQLITE_OS_OTHER`), replacing the stock unix
VFS that could not work on Horizon; `lib/switch_sqlite_vfs.cpp` is gone. `dawn_cache.db` and
`pipeline_cache.db` had always been 0 bytes. Second launch on hardware: `Dawn blob cache: 2859/2860 hits,
0 stores, 26.1 MiB loaded`. Shader stutter is gone from repeat runs; the general slowness is CPU time in game
code and is unaffected. Note: the Switch FTP server lists files the game holds open as 0 bytes.

## 2026-09-21: where the frame actually goes (VI cleared, scheduler suspect)

Three separate things, from one hardware run and its `boot.log`.

### Packaging: a plain `elf2nro` is not launchable here

Yesterday's NRO was packed with `elf2nro WiiCompiled.elf mkw_dev.nro` and nothing
else. It crashed instantly: the Atmosphère report showed **hbl as the only
module**, a user break at `hbl + 0x4044`, and the game's own logs untouched from
the previous session - it died in the loader. Repacking with the documented
recipe (`--icon=$DEVKITPRO/libnx/default_icon.jpg --nacp=build-mkwwitch/mkw.nacp`)
booted normally. The tell is the asset section: 22,725 trailing bytes with
`ASET`, versus `trailing=0` without.

Note for reading crash reports: that same `hbl + 0x4044` break appears when a
*good* session exits, so the break address alone proves nothing. What separates a
load failure from an exit is whether the game wrote any log at all.

### VI was never the problem, and the "fix" for it was a regression

With `interval=`/`deadline=`/`forced=`/`late=` on the `[vi]` line:

    interval=16666us  forced=0

The TV mode is read correctly and `VI_HLE_ForceRetrace` never fires - the
"something is forcing retraces" theory is dead. The 135-179 Hz that started this
was **catch-up**: the old run averaged 13,740 retraces over 255 s = 53.8 Hz,
slightly *under* the 60 Hz target, with bursts while draining a backlog.

Worse, the fix written for it (claiming `g_vi.lastRetrace = target` under the
lock before calling `AdvanceRetrace`) *threw retraces away*. `AdvanceRetrace` has
a re-entry guard that returns early when a guest callback is already inside one;
claiming the interval up front meant a guard-skipped retrace was never retried:

    deadline=2630   retrace=1140   [retrace] SKIPPED, guard held (#1600)

2,630 intervals claimed, 1,140 delivered, and the difference is the skip count.
Guest retrace rate fell from 33.3 Hz to 29.9 Hz in the same window. Reverted; the
counters stay. With it reverted the early part of a run reads 57-63 Hz.

(After the revert, `deadline=` counts *attempts*, since a skipped interval is
re-tried and re-counted, and `late=` accumulates across retries. `retrace=` is
the honest one.)

### The profile, named

`example-wii-nx/scripts/profile-report` turns the `[prof]` addresses into names
from `MAP.txt`, marks what the engine already replaces, and totals the rest:

    10000 samples: game code 55.8%, native runtime 44.2%
      0x801AA9B8  37.9%  native  OS::SleepThread   <- already native
      0x80064FD0   9.9%  game    (unnamed)
      0x80063870   9.5%  game    (unnamed)
      0x80074770   2.4%  game    nw4r::g3d::ScnMdl::G3dProc
      0x800679A0   1.6%  game    nw4r::g3d::CalcWorld
      guest code in the top 16 with no native replacement: 30.7%

The two unnamed ones were identified by reading their own translated source and
naming their callees:

- **0x80063870 (9.5%)** calls `GX::LoadPosMtxImm`, `LoadNrmMtxImm`,
  `LoadPosMtxIndx`, `LoadNrmMtxIndx3x3` - nw4r matrix loading, per object.
- **0x80064FD0 (9.9%)** calls `GXGetChanCtrl`, `GXGetTevKColor`,
  `GX::SetTevKColor`, `G3DState::Invalidate`, `pow` - material/TEV state.

Both are "write GX state" routines, which is why a track flyover runs several
times faster than a race: few materials, few matrices.

### Host phases say the time is not in game code

Same 10,000 samples, by host phase:

    GX__DrawDone done   33.4%    sched select body    28.3%
    present done        25.2%    sched idle Audio_HLE_Poll  9.5%

Caveat: a phase marker means "time since this marker was set", so the `done`
entries include guest code that ran afterwards. `sched select body` and the audio
poll do not - those are the scheduler itself.

The idle loop (`os_scheduler.cpp`) spins `PollRetrace -> ProcessSleepTimers ->
Audio_HLE_Poll -> ProcessTimerEvents -> ProcessAlarmQueue ->
WaitForNextRetracePoll` while nothing is runnable, and `Audio_HLE_Poll` took
`g_ai.mutex` on every pass to service a model that advances in 3 ms blocks.
Now rate-limited to a 200 us floor; skipped time is not lost, because
`ConsumeAudioPollDeltaMicros` leaves the interval unconsumed for the next poll.

### Per-frame counters, to settle scheduler vs renderer

New `[sched]` line beside `[vi]`, per **GX copy** rather than per retrace:

    [sched] per frame over N: sleeps=.. idle=..x ..us drawDone=..x ..us

Written at the three places that wait (`os_sleep.cpp`, the idle loop,
`GX__DrawDone`), reported from `vi.cpp`. Large `idle` means we spin while the
guest has nothing to do (scheduler); large `drawDone` means the guest waits on
the renderer (Aurora/present); high `sleeps` with little idle means the guest
parks itself too often (glue and native replacements matter more).

### Two more, unaddressed

- `Pipeline prewarm finished: 1241 pipelines in 113.4 s (Dawn blob cache:
  528/2866 hits, 2337 stores)`. Two minutes of startup, 80% cache miss - the
  cache key moves when the binary does.
- `Presentation job took 611.4 ms (acquire 595.1)`, twice in a run. Swapchain
  acquire, not encode or submit.

### Why the call glue is uneven

The translator already has a state-free call ABI that passes host registers
instead of spilling `CpuContext`, but `StateFreeFitsNativeRegisterBudget` gates
it at **inputs <= 4 and outputs <= 2**. Anything wider falls back to the full
boundary - which is exactly what both hot `nw4r::g3d` functions do, spilling and
reloading the register file around every `InvokeDirectCpu`.

### GL backends: the error was being swallowed

`dawn-nx-demo` reports 10 passed, 2 failed - both GL adapter requests, "No
supported adapters". Dawn's `BackendGL.cpp` wraps EGL discovery in
`SwallowDiscoveryError`, so the real cause never reaches the log. The demo now
brings EGL up itself and reports each step (core proc through
`eglGetProcAddress`, `eglGetDisplay`, `eglInitialize`, vendor/version/extensions,
`eglBindAPI`) and hands Dawn the display it got.

## 2026-09-21 (later): audio works, and why it never had

The first sound out of this port. The game's audio had been real all along -
`peak=24706` of 32767 at the backend - and four separate faults kept it off
the speaker, while also costing a third of every frame.

### The cost: 41 ms of a 113 ms frame

The `[idle]` split found it, not the scheduler: one idle entry per frame, and
inside it `audio=41646us`, with `wait=0` and `loops=1`. Not a spin - a single
`Audio_HLE_Poll` call chewing through 48.7 DMA blocks per frame, permanently
behind, partly because the audout path reclaimed buffers through
`audoutWaitPlayFinish` with a 10 ms timeout. After moving to audren:

    audio per frame   41,646 us  ->  1,886 us
    blocks per frame      48.7   ->      3.7

About 40 ms back per frame; measured on hardware as 15-20% faster (race second
from 4+ s to about 3 s).

### audout -> audren

- audout is fixed at 48 kHz; the Wii's AI DMA is 32 kHz and nothing resampled.
  An audren voice carries its own rate (`audrvVoiceInit(..., 32000)`) and the
  renderer resamples.
- The audout path cleared `header.data_size` right after submitting, which is
  the field its own in-flight check read, so a buffer still playing looked free.
  audren owns buffer state in `AudioDriverWaveBuf::state`, written only by the
  driver.
- `end_sample_offset` counts frames, not samples (4 bytes a stereo frame).

Shape taken from libnx's `audren-simple` example and the dusklight SDL3 Switch
backend Melee-NX uses; both check every return value, which ours now does.

### The four faults, in the order they were found

1. **A full ring killed audio for the whole run.** `AppendSamplesLocked`
   returned `false` on backpressure; the caller treats `false` as "unreadable
   DMA buffer" and sets `g_ai.enabled = false`. One block, then silence.
2. **Every audren failure was invisible.** `RT_LOG` is `std::cerr`, which
   nothing collects on the device. Each step now also writes the boot log, plus
   `[audio] first wavebuf submitted to audren` once.
3. **The counter lied.** `AudioStatsTick` ran before anything was staged and
   `dropped` was never incremented; a capacity drop returned early uncounted.
   Added `peak=` (loudest sample since last report) and counted the drop.
4. **Deadlock on stale wavebuf states.** libnx updates a wavebuf's state only in
   `audrvUpdate()`. The capacity check counted states without refreshing, and
   dropped the push before reaching the only code that refreshed them - so once
   the ring filled it stayed "full" forever: `pushes=19786 dropped=19735`.
   `QueuedBytesLocked` now calls `audrvUpdate()` first.

### What audio does now, and why it still lags

Audio is mixed by the game, at the game's speed: MKW mixes the next 3 ms each
time a DMA block completes. So audio lags wherever the game lags (animated-button
menus, races), and is a mirror of frame rate rather than a separate problem.

After a slow screen, the accumulated DMA time was replayed up to 4 blocks per
tick, so the game mixed faster than real time - heard as audio racing past
normal speed during the flyover. The accumulator is now clamped to two blocks
(`kMaxAudioBacklogBlocks`): slow stretches sound choppy while they last and
normal the moment they end.

### Memory: not a leak

`used=3185MB` is identical in all 7,961 samples across three runs. It is the
heap libnx maps once at startup in full-memory mode (no `__nx_heap_size`
override anywhere), so `malloc` has it all available and audren's work buffer
allocates fine.

## Native THP: written, not yet wired

`runtime/src/hle/thp_decode.cpp.pending` replaces `THPVideoDecode`
(`0x801B3BAC`) - the decoder behind every animated menu button, and 100%
translated PowerPC today. It follows the decompiled SDK `THPDec.c` where THP
departs from JPEG:

- no byte stuffing: the SDK reads the entropy stream as whole big-endian words;
- restarts realign to the next byte and reset DC, with no RST marker bytes;
- output is GX I8 tiles, not rows: `(y/4)*(W*4) + (x/8)*32 + (y%4)*8 + (x%8)`,
  from the SDK's `slwi xPos,2` / `slwi wid,2` store addressing.

The IDCT is libjpeg's float AAN (`jidctflt.c`), which is the SDK's own
algorithm and constants (AAN-scaled tables, `(x + 1024) / 8` output).

It cannot simply be linked in: the translator bakes native replacements into
generated code at translation time (`KnownNativeCpuCall<Target>`), and the
game's `bl THPVideoDecode` compiles to a direct call to the translated
`func_801B3BAC` - linking both gives `multiple definition of func_801B3BAC`.
Wiring it means re-running the translator with the native present, the same
way every existing native was wired.

## ogws: Wii Sports, and the libraries every Wii game shares

[ogws](https://github.com/doldecomp/ogws) (CC0) decompiles Wii Sports, forked as
[nx-mod/ogws-nx](https://github.com/nx-mod/ogws-nx) (`switch` branch, our README,
upstream's in `docs/OGWS_README.md`). It is 35% matched - 1.2 of 3.5 MB of code,
5,967 of 14,122 functions - so it cannot build the game; Wii Sports is recompiled
like every other title (`wiigames-nx/wiisports-nx`, `RSPE01`, staged). What it
has decompiled is the valuable part:

    nw4r  egg  revolution  RVLFaceLib  homebuttonMiniLib  MSL  runtime  Pack

and full symbol maps for both retail revisions. `wiisports-nx/scripts/fetch-symbols`
turns the map into MAP.txt: 14,122 functions, 7,590 named. It names our hottest
function in a second game: `LoadResShpPrimitive__Q34nw4r3g3d8G3DState...` at
`0x80066880` in Wii Sports, `0x80063870` in Mario Kart Wii.

The bundle disc that used the name moved to `wiisportspack-nx` (`SP2E01`).

### Where the frame goes after the audio fix

With ~40 ms of audio waste gone, game code is 87.8% of samples (was 55.8%), and
it is nearly all nw4r:

    12.6%  0x80063870  nw4r::g3d::G3DState::LoadResShpPrimitive (ogws g3d_state.cpp)
    12.2%  0x80064FD0  nw4r::g3d material/TEV load              (ogws g3d_resmat.cpp)
     2.7%  nw4r::g3d::ScnMdl::G3dProc
     2.1%  nw4r::lyt::Pane::CalculateMtx                        (menu layout)
     1.8%  nw4r::ef::DrawBillboardStrategy::DrawNormalBillboard (particles)
     1.5%  nw4r::g3d::CalcWorld
     1.4%  nw4r::ef::ParticleManager::Calc
     1.3%  KCLController::IsCollidingImpl                       (MKW only)
     1.2%  RaceScene::OnCalc                                    (MKW only)

Two functions are a quarter of the frame. nw4r ships in most first-party Wii
games, so a native nw4r speeds every title in wiigames-nx, not just Mario Kart.

### Why the decompilation is a specification, not a drop-in

Compiling ogws's nw4r and linking it in does not work, for two reasons:

- **Memory layout.** Decompiled code expects its structures in its own address
  space: 32-bit pointers, big-endian fields. Here those structures live in guest
  memory and are reached through `Memory::`, with byte swaps, from a 64-bit
  little-endian host. Every field access has to be rewritten.
- **Version drift.** nw4r changed between Wii Sports (2006) and Mario Kart Wii
  (2008). A function correct for Wii Sports' structure offsets can read the
  wrong fields in Mario Kart's.

So a native replacement is written against the decompiled function as its
specification - exactly how `thp_decode.cpp` was written against `THPDec.c` -
and checked against the target game's own layout. Leaf routines over plain
buffers (decoders, math over arrays) come across nearly verbatim; routines that
walk nw4r's object graph need their field accesses translated.

### Order

1. THP on hardware (built, waiting on a test).
2. The two g3d loaders: 24.8% of the frame.
3. nw4r::lyt and nw4r::ef: menus and particles, shared by every game.
4. The call glue in the translator: every translated call, all at once.

## Threaded GX decode (`[video] threaded_gx`)

Measured in a race: ~13 ms of every frame was graphics-command handling on the
game core, most of it Aurora decoding display lists and building draws (~10 µs
a draw, ~480 draws). `threaded_gx = true` moves that decode to a worker core.

How it works (Aurora `lib/gx/fifo.cpp`):

- The game thread only appends big-endian command bytes to the FIFO buffer.
  `GXCallDisplayList` appends the list itself (same stream format), which also
  copies it, so reused scratch lists are safe the moment the call returns.
- Full 32 KB buffers go to a bounded queue (8 deep); the worker runs
  `process()` on each, in order, after the same frame-worker SEALED wait the
  inline drain does.
- `drain()` becomes "hand off and wait until decoded", so everything that used
  to drain (frame end, copies, readbacks, DrawDone) sees current state.

Rules for code that runs on the game thread:

- Code that changes `g_gxState` or render passes goes through
  `fifo::defer(step)`: a `GX_LOAD_AURORA_DEFERRED` packet carrying an index
  into the batch's steps, run by the worker when the stream reaches it (and at
  once when decoding inline or recording a display list). The copies, the copy
  source/destination/clamp/gamma setters, z-scale and scissor offset, the
  source vertex descriptor and the bounding-box clear do. Register writes they
  also make stay on the game thread, in the same order.
- Code that must read decoder state back (`GXPeekZ`, `GXReadBoundingBox`, the
  copy filter, viewport policy and safe area) calls
  `fifo::sync_for_state_access()`. A sync waits, like the inline drain did,
  for the frame worker to have prepared the frame first - except between
  `aurora_end_frame` and the next begin, when the worker is idle anyway and
  waiting for the frame worker would wait for a begin only this thread makes.
- With syncs on setters, the first working threaded build still synced ~450
  times a frame (7.6 ms waiting) and raced the frame worker preparing the next
  frame: static, models breaking up, screens resizing, then an Aurora assert
  ("Final render pass must not have resolve target").
- Never call `drain()`/`sync()` while holding the renderer GPU mutex: the
  worker's `process()` takes it. `aurora_end_frame` syncs before locking.
- Direct decoder entry points bypass ordering. `GXApplyBPReg` (called by the
  runtime's FIFO parser) queues a BP packet instead when threaded.
- Redundancy checks that compare against decoder state (`GXSetVtxDesc`,
  `GXSetVtxAttrFmt`) can't trust it while threaded; they always resend.
- Nothing on the worker may run guest code. Aurora's frame-worker wait calls
  the runtime's guest-timing pump (alarms, audio callbacks) while it waits,
  and the worker waits there before each batch: the first threaded build hung
  at the intro movie with the game thread parked in `sync()`. The pump now
  runs only when the waiter is not the decode worker.
- Host memory a queued command points at must outlive the decode: the
  runtime packs wide-stride vertex arrays into a pool recycled only after
  `aurora_end_frame` (a sync before each reuse cost ~380 syncs a frame).
- The runtime's copy natives call `GXDrawDone()` before each copy for ordering;
  threaded decode skips it (`aurora_gx_threaded()`), since a deferred copy is
  in stream order already.

`[gxthread] per frame: syncs= wait=` shows how long the game thread waited on
the worker; `[gxdl] aurora=` drops towards zero when the decode has moved.

## Watch out for

Traps hit for real, with the check that catches each. Add to this list the same
session a new one bites.

### Building and shipping

- **A failed link can look like a successful build.** A background task's own
  exit code is not the build's: read `build.log`'s last line (`exit: 0`) and
  confirm `WiiCompiled.elf` exists with a fresh timestamp. A failed link also
  *deletes* the ELF, so a later `elf2nro` fails with "Failed to open input!".
- **NRO size is not proof of a new build.** Segments are page-aligned, so a small
  change often yields the same NRO size. Check the ELF instead: its size, a new
  string (`strings -n 20 WiiCompiled.elf | grep ...`), or a new symbol
  (`aarch64-none-elf-nm`).
- **`elf2nro` must get `--nacp` and `--icon`.** Without the asset section the
  NRO dies inside hbl before any code runs.
- **`hbl + 0x4044` means nothing on its own.** The same user break appears when a
  good session exits. What separates a load failure from an exit is whether the
  game wrote any log.
- **FTP uploads can break mid-transfer**, leaving a truncated NRO. Compare the
  size on the card with the local file after every upload.
- **The FTP server lists files the game holds open as 0 bytes.** Close the game
  before reading logs or caches.
- **Never run the translator and the build together.** Either can use gigabytes;
  both at once is how proot died. Each runs under a MemAvailable watchdog.

- **A launch that hangs before its first log line: rule out the console first.**
  `boot.log` byte-identical to the previous run's, with only a new
  `logs/base_*` folder, means it died before logging anything. That happened
  with Nextendo running, which has hung other apps the same way, and after a
  force-closed session that may still hold system services (audren). Reboot,
  and try without Nextendo, before blaming the build.

### Logging on the device

- **`RT_LOG` is `std::cerr`, and nothing on the device collects stderr.** Anything
  that must be seen on hardware goes through `SwitchBootLogExternal`.
- **A counter must be taken where the event really happens.** The old
  `[audio] pushes=` counted before anything was staged and never counted drops,
  so `dropped=0` meant nothing.

### Profiling

- **`[prof] game #N` is inclusive, not self time.** The sampler records the last
  *indirect-dispatch* target, which direct calls never update. So a function's
  share includes every direct callee below it and all host work it triggers:
  GX FIFO decode, Aurora draws. The two g3d loaders showed 30% of a race frame,
  most of it Aurora's display-list work (`[gxfifo] split` / `[gxdl]`). Check
  with `logs/pcsamples.bin` (real host PC, resolved against the ELF with
  `nm -C`) before porting a function because of its `[prof]` number.
- **Hot function names:** doldecomp/mkw's `config/RMCP01/symbols.txt` names
  ~16% of the g3d range (e.g. 0x80074770 `ScnMdl::G3dProc`); the rest are
  `fn_` - match those against ogws by structure.
- **Sized-by-default statics land in `.data`.** A static table whose element has
  a non-zero default member (`dstAlpha = UINT32_MAX`) is stored in the ELF and
  RAM in full: Aurora's pipeline memo was 2.8 MB of it.

### Native code and the translator

- **A new native for an already-translated function needs a re-translation.**
  The translator bakes natives into the generated code (`KnownNativeCpuCall`,
  direct calls), so linking a `PPC_NATIVE_OVERRIDE` alone gives
  `multiple definition of func_XXXXXXXX`.
- **Natives in a game's own folder must be visible to every translator step.**
  `emit-build-shards` built its native index from `--native-source-dir` alone and
  re-emitted Mario Kart's strap screen (`func_800077C8`). Fixed: it now also reads
  the project's `game_native_root` and bindings, or `--game-native-dir`.
- **Back up `generated/` before re-translating.** `--prune-stale` deletes what it
  does not rewrite.
- **A native that writes guest memory the renderer reads must say so.**
  Textures, TLUTs and vertex data are cached by the renderer and re-read only on
  `GxNotifyGuestRamDmaWrite(addr, size)` - which the cache-flush natives and
  `LCStoreData` make on the game's behalf. Native THP wrote its planes through a
  host pointer and skipped it: menu buttons showed two videos flickering over
  each other, stale planes mixed with fresh ones. Any native that writes
  something GX later samples calls it when done.
- **Global declarations stay out of anonymous namespaces.** A function declared
  inside one gets internal linkage and fails to link (`SwitchBootLogExternal`,
  twice).

### Audio

- **libnx wavebuf states only change in `audrvUpdate()`.** Refresh before
  counting in-flight buffers, or a full ring stays "full" forever.
- **`false` from the backend push means "unreadable DMA buffer"** to its caller,
  which then disables audio for the run. Backpressure must return `true`.
- **The audio tick runs about once per frame, not per millisecond.** Any per-tick
  budget has to cover a whole frame (50-125 ms), or it starves every screen.

## Double-check when working from a decompilation

Applies to any decompiled code used as a reference, here or elsewhere.

- **Which build is it?** A decomp matches one game, one revision. Confirm the
  target's ID and revision (ogws: `RSPE01` rev 0 or rev 1) before trusting an
  address, offset or size.
- **Which library version?** Shared libraries drift: nw4r in Wii Sports (2006)
  and Mario Kart Wii (2008) differ. Check structure offsets in the target game,
  not the decomp's.
- **Matched or just decompiled?** Only *matched* functions are known to be
  identical to the binary. Treat the rest as a good guess.
- **Where does the data live?** Decompiled code assumes its own 32-bit,
  big-endian address space. Every pointer and field access needs translating for
  a host that reaches guest memory through an accessor.
- **Format quirks hide in the asm.** THP looked like JPEG but has no byte
  stuffing, no RST markers, and writes GX I8 tiles; each detail was only in the
  hand-written assembly, not in anything named.
- **Output layout, not just values.** Read the store addressing (THP's
  `slwi xPos, 2`) before assuming rows of pixels or plain arrays.
- **Keep reverse-engineering output out of public repos.** Disassembly, Ghidra
  databases and decompiled game code stay private; symbol maps and addresses of
  SDK functions are the most a public repo carries.

## Idea (not started): trace-assisted decompilation

Every guest call already passes through generated code, so the runtime can see
every function's arguments, return values and memory accesses, in every game we
translate. Recorded selectively, that is a decompilation aid for all of them:

- **Signatures**: argument count and kinds (pointer, int, float), return value,
  observed rather than guessed.
- **Struct layouts**: per pointer argument, the offsets read and written and at
  what width; aggregated across calls, the structure's shape.
- **Class hierarchies**: the vtable targets each indirect call site actually uses.
- **Ground-truth test vectors**: recorded input -> output pairs per function. A
  decomp can prove a function compiles to the same bytes; these prove it behaves
  the same. They are also exactly what native-check needs.
- **Coverage and heat**: which of a game's functions run at all, and how often.

The translator already infers every function's input and output registers
statically (its liveness pass); dynamic observation on top of that is stronger
than either alone.

It does not write source - it produces facts, and readable C still takes a
decompiler or a person. It only sees code that runs. And "everything" is
millions of calls a second, so it must be selective: named functions, sampled,
aggregated on the device rather than logged raw.

**Traces are reverse-engineering output: they stay private, never in wii-nx.**

First step when this starts: record I/O for functions about to go native (the
two nw4r::g3d loaders), so the recordings double as native-check test vectors.

### Related: native-check

A check build keeps a function's translated body alongside its native, runs both
on the same input, compares, and logs any divergence with the function and the
first differing byte. Cheap for pure functions (THP: compare the output planes);
for the g3d loaders the output is the GX command stream they write. The
translator's `MKW_STATE_FREE_DIFFERENTIAL_BEGIN` block already does this for its
own ABI and is the pattern to extend.
