// aurora::input on Switch: each connected npad is a GameController, keyed by
// its slot (1-4). Mappings, dead zones and ports are pad.cpp's, as on desktop.
#include "input.hpp"
#include "internal.hpp"
#include "gamepad_switch.hpp"

namespace aurora::input {
Module Log("aurora::input");
absl::flat_hash_map<uint32_t, GameController> g_GameControllers;

namespace {
bool g_initialized = false;

AuroraControllerID id_for_slot(uint32_t index) { return index + 1; }
} // namespace

void initialize() noexcept {
  switch_pad::initialize();
  g_initialized = true;
  poll();
}

void poll() noexcept {
  if (!g_initialized) {
    return;
  }
  switch_pad::update();
  for (uint32_t i = 0; i < switch_pad::kSlotCount; ++i) {
    auto* slot = switch_pad::slot(i);
    const AuroraControllerID id = id_for_slot(i);
    const bool connected = switch_pad::connected(*slot);
    const bool known = g_GameControllers.contains(id);
    if (connected && !known) {
      GameController controller;
      controller.m_controller = reinterpret_cast<AuroraGamepad*>(slot);
      controller.m_index = static_cast<int32_t>(id);
      controller.m_playerIndex = slot->player;
      g_GameControllers[id] = controller;
      Log.info("controller {} connected: {}", i + 1, aurora_gamepad_name(controller.m_controller));
    } else if (!connected && known) {
      g_GameControllers.erase(id);
      Log.info("controller {} disconnected", i + 1);
    }
  }
}

GameController* get_controller_for_player(uint32_t player) noexcept {
  for (auto& [id, controller] : g_GameControllers) {
    if (aurora_gamepad_player_index(controller.m_controller) == static_cast<int>(player)) {
      return &controller;
    }
  }
  return nullptr;
}

int32_t get_instance_for_player(uint32_t player) noexcept {
  for (const auto& [id, controller] : g_GameControllers) {
    if (aurora_gamepad_player_index(controller.m_controller) == static_cast<int>(player)) {
      return static_cast<int32_t>(id);
    }
  }
  return -1;
}

AuroraControllerID add_controller(AuroraControllerID which) noexcept {
  poll();
  return which;
}

bool refresh_controller(AuroraControllerID instance) noexcept {
  const auto it = g_GameControllers.find(instance);
  if (it == g_GameControllers.end()) {
    return false;
  }
  it->second.m_mappingLoaded = false;
  return true;
}

void remove_controller(uint32_t instance) noexcept { g_GameControllers.erase(instance); }

int32_t player_index(uint32_t instance) noexcept {
  const auto it = g_GameControllers.find(instance);
  return it == g_GameControllers.end() ? -1 : aurora_gamepad_player_index(it->second.m_controller);
}

void set_player_index(uint32_t instance, int32_t index) noexcept {
  const auto it = g_GameControllers.find(instance);
  if (it != g_GameControllers.end()) {
    aurora_gamepad_set_player_index(it->second.m_controller, index);
    it->second.m_playerIndex = index;
  }
}

std::string controller_name(uint32_t instance) noexcept {
  const auto it = g_GameControllers.find(instance);
  return it == g_GameControllers.end() ? std::string{} : aurora_gamepad_name(it->second.m_controller);
}

bool is_gamecube(uint32_t) noexcept { return false; }

// Rumble is not wired to HD Rumble yet.
bool controller_has_rumble(uint32_t) noexcept { return false; }
void controller_rumble(uint32_t, uint16_t, uint16_t, uint16_t) noexcept {}

uint32_t controller_count() noexcept { return static_cast<uint32_t>(g_GameControllers.size()); }

// Ports follow npad slots on Switch; there is no preference file to keep.
void persist_controller_for_player(uint32_t, const GameController*) noexcept {}

void set_mouse_scroll(float, float) noexcept {}
void get_mouse_scroll(float* scrollX, float* scrollY) noexcept {
  if (scrollX != nullptr) {
    *scrollX = 0.f;
  }
  if (scrollY != nullptr) {
    *scrollY = 0.f;
  }
}

void shutdown() noexcept { g_GameControllers.clear(); }
} // namespace aurora::input
