#include <aurora/imgui_config.h>

// No real SDL3 (and so no SDL_IOStream) is built on Switch. Dear ImGui calls
// these for its own .ini settings persistence, which nothing here depends on
// yet - report "unavailable" rather than wiring up real file I/O.

ImFileHandle ImFileOpen(const char*, const char*) { return nullptr; }

bool ImFileClose(ImFileHandle file) { return file == nullptr; }

ImU64 ImFileGetSize(ImFileHandle) { return static_cast<ImU64>(-1); }

ImU64 ImFileRead(void*, ImU64, ImU64, ImFileHandle) { return 0; }

ImU64 ImFileWrite(const void*, ImU64, ImU64, ImFileHandle) { return 0; }
