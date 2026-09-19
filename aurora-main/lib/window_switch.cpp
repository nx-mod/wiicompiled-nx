#include "window.hpp"
#include "internal.hpp"

#include <aurora/aurora.h>
#include <aurora/event.h>

#include <switch.h>

// Real (not stubbed) Switch-native replacement for window.cpp: there is
// exactly one display, one always-open window (the applet's default
// NWindow), and no window manager, so most of window.hpp's SDL-window-
// manipulation surface collapses to fixed values or no-ops. create_window()
// hands back nwindowGetDefault() disguised as an SDL_Window*, exactly like
// window.cpp hands back an opaque per-platform handle on every other
// platform (e.g. Android's ANativeWindow* via SDL_Window properties) -
// BackendBinding.cpp's Horizon branch reinterpret_casts it straight back to
// NWindow* for wgpu::SurfaceSourceViNN.

namespace aurora::window {
namespace {
Module Log("aurora::window");

SDL_Window* g_window = nullptr;
AuroraWindowSize g_windowSize{};
AuroraEvent g_events[2];
std::atomic<AuroraDisplayMode> g_displayMode{AURORA_DISPLAY_MODE_EXCLUSIVE};
bool g_aspectLocked = false;
int g_presentAspectWidth = 0;
int g_presentAspectHeight = 0;
} // namespace

SurfaceLock::SurfaceLock() noexcept = default;
SurfaceLock::~SurfaceLock() = default;

bool initialize() { return true; }

bool initialize_event_watch() { return true; }

bool push_custom_event(CustomEvent /*eventType*/) { return true; }

void shutdown() {}

bool create_window(AuroraBackend /*backend*/) {
  NWindow* nwindow = nwindowGetDefault();
  if (nwindow == nullptr) {
    Log.error("nwindowGetDefault() returned null");
    return false;
  }
  g_window = reinterpret_cast<SDL_Window*>(nwindow);

  // The OS compositor always presents at 1280x720 regardless of docked vs.
  // handheld; the console itself scales the final output.
  constexpr uint32_t kWidth = 1280;
  constexpr uint32_t kHeight = 720;
  g_windowSize = AuroraWindowSize{
      .width = kWidth,
      .height = kHeight,
      .fb_width = kWidth,
      .fb_height = kHeight,
      .native_fb_width = kWidth,
      .native_fb_height = kHeight,
      .scale = 1.f,
  };
  return true;
}

bool create_renderer() { return true; }

void destroy_window() {
  // nwindowGetDefault() hands back a libnx-owned static window we never
  // created, so there is nothing to close here.
  g_window = nullptr;
}

void show_window() {}

AuroraWindowSize get_window_size() { return g_windowSize; }

const AuroraEvent* poll_events() {
  size_t count = 0;
  if (!appletMainLoop()) {
    g_events[count++] = AuroraEvent{.type = AURORA_EXIT};
  }
  g_events[count] = AuroraEvent{.type = AURORA_NONE};
  return g_events;
}

SDL_Window* get_sdl_window() { return g_window; }

SDL_Renderer* get_sdl_renderer() { return nullptr; }

bool is_paused() noexcept { return false; }

bool is_presentable() noexcept { return g_window != nullptr; }

void pump_events() noexcept {}

bool native_resize_pending() noexcept { return false; }

bool native_window_size_matches(uint32_t width, uint32_t height) noexcept {
  return width == g_windowSize.native_fb_width && height == g_windowSize.native_fb_height;
}

void set_surface_ready(bool /*ready*/) noexcept {}

void set_title(const char* /*title*/) {}

void set_fullscreen(bool /*fullscreen*/) {}

bool get_fullscreen() { return true; }

void set_display_mode(AuroraDisplayMode mode) { g_displayMode.store(mode, std::memory_order_release); }

AuroraDisplayMode get_display_mode() { return g_displayMode.load(std::memory_order_acquire); }

void set_window_size(uint32_t /*width*/, uint32_t /*height*/) {}

void set_window_position(uint32_t /*x*/, uint32_t /*y*/) {}

void center_window() {}

void sync_frame_buffer_size() noexcept {}

void request_frame_buffer_resize() {}

void set_frame_buffer_scale(float /*scale*/) {}

void set_frame_buffer_aspect_fit(bool /*fit*/) {}

void set_present_surface_fill(bool /*fill*/) {}

void lock_present_aspect_ratio(int width, int height) {
  g_presentAspectWidth = width;
  g_presentAspectHeight = height;
  g_aspectLocked = width > 0 && height > 0;
}

void unlock_present_aspect_ratio() { g_aspectLocked = false; }

bool get_present_aspect_ratio(float& aspect) noexcept {
  if (!g_aspectLocked) {
    return false;
  }
  aspect = static_cast<float>(g_presentAspectWidth) / static_cast<float>(g_presentAspectHeight);
  return true;
}

void set_background_input(bool /*value*/) {}
} // namespace aurora::window
