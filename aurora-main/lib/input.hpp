#pragma once

#include <string>
#include <array>
#include "dolphin/pad.h" // For PADDeaZones and PADButtonMapping
#include <aurora/gamepad.h>
#include "logging.hpp"

#include <absl/container/flat_hash_map.h>

namespace aurora::input {
extern Module Log;

struct GameController {
  AuroraGamepad* m_controller = nullptr;
  bool m_isGameCube = false;
  bool m_gameCubeUseOrdinaryStop = false;
  int32_t m_index = -1;
  int32_t m_playerIndex = -1;
  bool m_hasRumble = false;
  PADDeadZones m_deadZones{
      .emulateTriggers = true,
      .useDeadzones = true,
      .stickDeadZone = 8000,
      .substickDeadZone = 8000,
      .leftTriggerActivationZone = 31150,
      .rightTriggerActivationZone = 31150,
  };
  uint16_t m_vid = 0;
  uint16_t m_pid = 0;
  std::array<PADButtonMapping, PAD_BUTTON_COUNT> m_buttonMapping{};
  // Secondary binding per GC button; not persisted in .controller files, the
  // runtime re-applies it from its own config whenever a controller attaches.
  std::array<PADButtonMapping, PAD_BUTTON_COUNT> m_altButtonMapping{};
  std::array<PADAxisMapping, PAD_AXIS_COUNT> m_axisMapping{};
  uint16_t m_rumbleIntensityLow = 32767;
  uint16_t m_rumbleIntensityHigh = 32767;
  bool m_mappingLoaded = false;
  constexpr bool operator==(const GameController& other) const {
    return m_controller == other.m_controller && m_index == other.m_index;
  }
  uint8_t m_ledRed = 0xFF;
  uint8_t m_ledGreen = 0xFF;
  uint8_t m_ledBlue = 0xFF;
  bool m_isColorDirty = true;
  bool m_hasRgbLed = false;
};

GameController* get_controller_for_player(uint32_t player) noexcept;
int32_t get_instance_for_player(uint32_t player) noexcept;
AuroraControllerID add_controller(AuroraControllerID which) noexcept;
bool refresh_controller(AuroraControllerID instance) noexcept;
void remove_controller(uint32_t instance) noexcept;
int32_t player_index(uint32_t instance) noexcept;
void set_player_index(uint32_t instance, int32_t index) noexcept;
std::string controller_name(uint32_t instance) noexcept;
bool is_gamecube(uint32_t instance) noexcept;
bool controller_has_rumble(uint32_t instance) noexcept;
void controller_rumble(uint32_t instance, uint16_t low_freq_intensity, uint16_t high_freq_intensity,
                       uint16_t duration_ms) noexcept;
uint32_t controller_count() noexcept;
void initialize() noexcept;
// Reads the host's controllers once. Switch needs it each frame; desktop's SDL
// event pump already does this, so there it does nothing.
void poll() noexcept;
void persist_controller_for_player(uint32_t player, const GameController* controller) noexcept;
extern absl::flat_hash_map<uint32_t, GameController> g_GameControllers;

void set_mouse_scroll(float scrollX, float scrollY) noexcept;
void get_mouse_scroll(float* scrollX, float* scrollY) noexcept;

void shutdown() noexcept;
} // namespace aurora::input
