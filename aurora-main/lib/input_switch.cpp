#include "input.hpp"
#include "internal.hpp"

// Minimal stub: real libnx hid-backed controller support is a follow-up task.
// This reports "no controllers" while satisfying every symbol aurora_core and
// its callers (aurora.cpp, window_switch.cpp) link against.

namespace aurora::input {
Module Log("aurora::input");
absl::flat_hash_map<Uint32, GameController> g_GameControllers;

GameController* get_controller_for_player(uint32_t /*player*/) noexcept { return nullptr; }

Sint32 get_instance_for_player(uint32_t /*player*/) noexcept { return -1; }

SDL_JoystickID add_controller(SDL_JoystickID which) noexcept { return which; }

bool refresh_controller(SDL_JoystickID /*instance*/) noexcept { return false; }

void remove_controller(Uint32 /*instance*/) noexcept {}

Sint32 player_index(Uint32 /*instance*/) noexcept { return -1; }

void set_player_index(Uint32 /*instance*/, Sint32 /*index*/) noexcept {}

std::string controller_name(Uint32 /*instance*/) noexcept { return {}; }

bool is_gamecube(Uint32 /*instance*/) noexcept { return false; }

bool controller_has_rumble(Uint32 /*instance*/) noexcept { return false; }

void controller_rumble(uint32_t /*instance*/, uint16_t /*low_freq_intensity*/, uint16_t /*high_freq_intensity*/,
                       uint16_t /*duration_ms*/) noexcept {}

uint32_t controller_count() noexcept { return 0; }

void initialize() noexcept {}

void persist_controller_for_player(uint32_t /*player*/, const GameController* /*controller*/) noexcept {}

void set_mouse_scroll(float /*scrollX*/, float /*scrollY*/) noexcept {}

void get_mouse_scroll(float* scrollX, float* scrollY) noexcept {
  if (scrollX != nullptr) {
    *scrollX = 0.f;
  }
  if (scrollY != nullptr) {
    *scrollY = 0.f;
  }
}

void shutdown() noexcept {}
} // namespace aurora::input
