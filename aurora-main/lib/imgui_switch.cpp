#include "imgui.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>

// Switch build of aurora::imgui: a real Dear ImGui context and frame
// lifecycle, but no platform/renderer backend (imgui_impl_sdl3 and
// imgui_impl_wgpu are SDL3/Dawn-window coupled and are not built here).
//
// A context has to exist because WiiCompiled's settings overlay calls
// ImGui:: every retrace, and its Draw() also does non-UI work (Wii Remote
// polling, controller mapping, display-mode persistence) that must keep
// running. Draw lists are built and then dropped; presenting them is a
// follow-up (a Switch-native imgui render path).

namespace aurora::imgui {
namespace {
bool g_frameOpen = false;
bool g_frameDataBuilt = false;
std::chrono::steady_clock::time_point g_lastFrame{};
} // namespace

void create_context() noexcept {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  ImGui::LoadIniSettingsFromMemory("", 0);
  io.WantSaveIniSettings = false;
}

void initialize() noexcept {
  // NewFrame() asserts the font atlas is built; normally the renderer backend
  // triggers that. Build it on the CPU and never upload it.
  unsigned char* pixels = nullptr;
  int width = 0;
  int height = 0;
  ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
}

void shutdown() noexcept {
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }
  g_frameOpen = false;
  g_frameDataBuilt = false;
}

void process_event(const SDL_Event& /*event*/) noexcept {}

bool wants_capture_event(const SDL_Event& /*event*/) noexcept { return false; }

void new_frame(const AuroraWindowSize& size) noexcept {
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  if (g_frameOpen) {
    // The previous frame was never rendered; close it before starting another.
    ImGui::Render();
    g_frameOpen = false;
  }

  const auto now = std::chrono::steady_clock::now();
  float deltaSeconds = 1.f / 60.f;
  if (g_lastFrame.time_since_epoch().count() != 0) {
    deltaSeconds = std::chrono::duration<float>(now - g_lastFrame).count();
  }
  g_lastFrame = now;

  ImGuiIO& io = ImGui::GetIO();
  io.DeltaTime = std::max(deltaSeconds, 1.0f / 1000000.f);
  io.DisplaySize = {static_cast<float>(size.width), static_cast<float>(size.height)};
  io.DisplayFramebufferScale = {
      size.width > 0 ? static_cast<float>(size.native_fb_width) / static_cast<float>(size.width) : 1.0f,
      size.height > 0 ? static_cast<float>(size.native_fb_height) / static_cast<float>(size.height) : 1.0f,
  };
  ImGui::NewFrame();
  g_frameOpen = true;
  g_frameDataBuilt = false;
}

void render_frame_data() noexcept {
  if (g_frameDataBuilt || !g_frameOpen) {
    return;
  }
  ImGui::Render();
  g_frameOpen = false;
  g_frameDataBuilt = true;
}

void render(const wgpu::RenderPassEncoder& /*pass*/) noexcept { render_frame_data(); }
} // namespace aurora::imgui
