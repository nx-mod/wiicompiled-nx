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
