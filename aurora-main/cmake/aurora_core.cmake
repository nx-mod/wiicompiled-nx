# SDL3 has no Switch platform module at all (confirmed: its own CMakeLists has
# hardcoded thread backends for Windows/Vita/PSP/PS2/3DS but nothing for
# Horizon, so HAVE_SDL_THREADS never gets set and configure aborts outright -
# not a quick CMake fix). window.cpp/input.cpp are written directly against
# real SDL3 types, so they're swapped for Switch-native replacements instead
# of trying to make real SDL3 build here.
if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    set(AURORA_CORE_WINDOW_SRC lib/window_switch.cpp)
    set(AURORA_CORE_INPUT_SRC lib/input_switch.cpp)
    # SQLite cannot resolve "sdmc:/" paths without a working directory; this
    # registers a pass-through VFS the shader caches open with.
    set(AURORA_CORE_SQLITE_SRC lib/switch_sqlite_vfs.cpp)
else ()
    set(AURORA_CORE_WINDOW_SRC lib/window.cpp)
    set(AURORA_CORE_INPUT_SRC lib/input.cpp)
endif ()

add_library(aurora_core STATIC
        lib/aurora.cpp
        ${AURORA_CORE_SQLITE_SRC}
        ${AURORA_CORE_INPUT_SRC}
        ${AURORA_CORE_WINDOW_SRC}
        lib/logging.cpp
        lib/system_info.cpp
        lib/system_info.hpp
)
add_library(aurora::core ALIAS aurora_core)
set_target_properties(aurora_core PROPERTIES FOLDER "aurora")

target_compile_definitions(aurora_core PUBLIC AURORA TARGET_PC)
target_include_directories(aurora_core PUBLIC include)
if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    # event.h/input.hpp declare real SDL3 types by value (SDL_Event,
    # SDL_JoystickID) even though we never build or link SDL3 on Switch.
    # Reuse the SDL3 source tree Dawn's own build already fetched, purely
    # for its headers - no network access needed here.
    target_include_directories(aurora_core PUBLIC "${CMAKE_BINARY_DIR}/_deps/sdl-src/include")
    target_link_libraries(aurora_core PUBLIC fmt::fmt xxhash)
else ()
    target_link_libraries(aurora_core PUBLIC fmt::fmt ${AURORA_SDL3_TARGET} xxhash)
endif ()
target_link_libraries(aurora_core PRIVATE absl::btree absl::flat_hash_map sqlite3 TracyClient)
if (AURORA_ENABLE_GX AND AURORA_CACHE_USE_ZSTD)
    target_compile_definitions(aurora_core PRIVATE AURORA_CACHE_USE_ZSTD)
    target_link_libraries(aurora_core PRIVATE libzstd_static)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL Windows)
    # stuff for fetching system info.
    target_link_libraries(aurora_core PRIVATE ntdll dxgi advapi32 user32)
elseif (APPLE)
    target_sources(aurora_core PRIVATE lib/system_info_mac.mm)
endif ()

if (AURORA_ENABLE_GX)
    if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
        # Dear ImGui's backends are SDL3/SDL_Renderer-coupled (imgui_impl_sdl3,
        # imgui_impl_sdlrenderer3), which we never build on Switch - stub the
        # aurora::imgui interface instead of pulling that dependency in. The
        # core imgui library (built in extern/CMakeLists.txt without those
        # backends) is still linked PUBLIC: WiiCompiled's settings-overlay/
        # controller-mapping-wizard UI calls the raw ImGui:: API directly and
        # needs it, even though nothing presents those draw lists yet.
        target_sources(aurora_core PRIVATE lib/imgui_switch.cpp)
        target_link_libraries(aurora_core PUBLIC imgui)
    else ()
        target_sources(aurora_core PRIVATE lib/imgui.cpp)
        target_link_libraries(aurora_core PUBLIC imgui)
    endif ()
endif ()

if (AURORA_ENABLE_GX)
    target_compile_definitions(aurora_core PUBLIC AURORA_ENABLE_GX WEBGPU_DAWN)
    target_sources(aurora_core PRIVATE lib/webgpu/gpu.cpp lib/webgpu/gpu_cache.cpp lib/dawn/BackendBinding.cpp)
    target_link_libraries(aurora_core PRIVATE dawn::webgpu_dawn)
    if (DAWN_ENABLE_VULKAN)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_VULKAN)
    endif ()
    if (DAWN_ENABLE_METAL)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_METAL)
        target_sources(aurora_core PRIVATE lib/dawn/MetalBinding.mm)
        set_source_files_properties(lib/dawn/MetalBinding.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
        target_link_options(aurora_core PUBLIC "LINKER:-weak_framework,Metal")
    endif ()
    if (DAWN_ENABLE_D3D11)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_D3D11)
    endif ()
    if (DAWN_ENABLE_D3D12)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_D3D12)
    endif ()
    if (DAWN_ENABLE_DESKTOP_GL OR DAWN_ENABLE_OPENGLES)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_OPENGL)
        if (DAWN_ENABLE_DESKTOP_GL)
            target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_DESKTOP_GL)
        endif ()
        if (DAWN_ENABLE_OPENGLES)
            target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_OPENGLES)
        endif ()
    endif ()
    if (DAWN_ENABLE_NULL)
        target_compile_definitions(aurora_core PRIVATE DAWN_ENABLE_BACKEND_NULL)
    endif ()
endif ()
