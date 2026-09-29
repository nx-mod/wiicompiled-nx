#include "../../fs_helper.hpp"
#include "../../input.hpp"
#include "../../internal.hpp"
#include <dolphin/pad.h>
#include <dolphin/si.h>
#include <aurora/gamepad.h>

#include <cstdio>
#include <cstring>

#include <array>
#include <atomic>
#include <sys/stat.h>
#include <ranges>

namespace {
// Mapping files are little-endian, as SDL's IO helpers wrote them.
bool read_u32le(std::FILE* f, uint32_t* v) {
  uint8_t b[4];
  if (std::fread(b, 1, 4, f) != 4) return false;
  *v = b[0] | b[1] << 8 | b[2] << 16 | uint32_t(b[3]) << 24;
  return true;
}
bool write_u32le(std::FILE* f, uint32_t v) {
  const uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
  return std::fwrite(b, 1, 4, f) == 4;
}
bool write_s32le(std::FILE* f, int32_t v) { return write_u32le(f, static_cast<uint32_t>(v)); }
bool write_u8(std::FILE* f, uint8_t v) { return std::fwrite(&v, 1, 1, f) == 1; }
constexpr int32_t k_mappingsFileVersion = 3;

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsStandard{{
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsXBox360{{
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsXBoxOne{{
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsPS3{{
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsPS4{{
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsPS5{{
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsGamecube{{
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {AURORA_GAMEPAD_BUTTON_MISC3, PAD_TRIGGER_L},
    {AURORA_GAMEPAD_BUTTON_MISC4, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsNSOGamecube{{
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_BACK, PAD_TRIGGER_Z},
    {AURORA_GAMEPAD_BUTTON_LEFT_SHOULDER, PAD_TRIGGER_L},
    {AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsProCon{{
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsJoyConRight{{
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsJoyConLeft{{
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsJoyPair{{
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

// Wii U Pro Controllers through SDL's HIDAPI Wii driver. No AuroraGamepadType
// singles them out, so they are picked by the name the driver gives them (see
// __PADSetDefaultMapping). Wii Remotes, with or without a Nunchuk or Classic
// Controller, are read by the game through KPAD instead and never get a
// GameCube mapping (the runtime hides those ports from PADRead).

// Nintendo's labelled layout: A on the right accelerates, B at the bottom
// brakes. SDL's Wii driver reports ZL/ZR as the LEFT_TRIGGER/RIGHT_TRIGGER
// axes, never as shoulder buttons, so they are left unbound here and picked up
// by aurora's default axis mapping (g_defaultAxes) the same way every
// analog-trigger pad's L/R is.
std::array<PADButtonMapping, PAD_BUTTON_COUNT> g_defaultButtonsWiiUPro{{
    {AURORA_GAMEPAD_BUTTON_EAST, PAD_BUTTON_A},
    {AURORA_GAMEPAD_BUTTON_SOUTH, PAD_BUTTON_B},
    {AURORA_GAMEPAD_BUTTON_NORTH, PAD_BUTTON_X},
    {AURORA_GAMEPAD_BUTTON_WEST, PAD_BUTTON_Y},
    {AURORA_GAMEPAD_BUTTON_START, PAD_BUTTON_START},
    {AURORA_GAMEPAD_BUTTON_BACK, PAD_TRIGGER_Z},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L},
    {PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R},
    {AURORA_GAMEPAD_BUTTON_DPAD_UP, PAD_BUTTON_UP},
    {AURORA_GAMEPAD_BUTTON_DPAD_DOWN, PAD_BUTTON_DOWN},
    {AURORA_GAMEPAD_BUTTON_DPAD_LEFT, PAD_BUTTON_LEFT},
    {AURORA_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_BUTTON_RIGHT},
}};

std::array<PADKeyButtonBinding, PAD_BUTTON_COUNT> g_defaultKeys{{
    {PAD_KEY_INVALID, PAD_BUTTON_A},
    {PAD_KEY_INVALID, PAD_BUTTON_B},
    {PAD_KEY_INVALID, PAD_BUTTON_X},
    {PAD_KEY_INVALID, PAD_BUTTON_Y},
    {PAD_KEY_INVALID, PAD_BUTTON_START},
    {PAD_KEY_INVALID, PAD_TRIGGER_Z},
    {PAD_KEY_INVALID, PAD_TRIGGER_L},
    {PAD_KEY_INVALID, PAD_TRIGGER_R},
    {PAD_KEY_INVALID, PAD_BUTTON_UP},
    {PAD_KEY_INVALID, PAD_BUTTON_DOWN},
    {PAD_KEY_INVALID, PAD_BUTTON_LEFT},
    {PAD_KEY_INVALID, PAD_BUTTON_RIGHT},
}};

std::array<PADKeyAxisBinding, PAD_AXIS_COUNT> g_defaultKeyAxis{{
    {PAD_KEY_INVALID, PAD_AXIS_LEFT_X_POS, 0},
    {PAD_KEY_INVALID, PAD_AXIS_LEFT_X_NEG, 0},
    {PAD_KEY_INVALID, PAD_AXIS_LEFT_Y_POS, 0},
    {PAD_KEY_INVALID, PAD_AXIS_LEFT_Y_NEG, 0},
    {PAD_KEY_INVALID, PAD_AXIS_RIGHT_X_POS, 0},
    {PAD_KEY_INVALID, PAD_AXIS_RIGHT_X_NEG, 0},
    {PAD_KEY_INVALID, PAD_AXIS_RIGHT_Y_POS, 0},
    {PAD_KEY_INVALID, PAD_AXIS_RIGHT_Y_NEG, 0},
    {PAD_KEY_INVALID, PAD_AXIS_TRIGGER_L, 0},
    {PAD_KEY_INVALID, PAD_AXIS_TRIGGER_R, 0},
}};

std::array<PADAxisMapping, PAD_AXIS_COUNT> g_defaultAxes{{
    {{AURORA_GAMEPAD_AXIS_LEFTX, AXIS_SIGN_POSITIVE}, AURORA_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_X_POS},
    {{AURORA_GAMEPAD_AXIS_LEFTX, AXIS_SIGN_NEGATIVE}, AURORA_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_X_NEG},
    // SDL's gamepad y-axis is inverted from GC's
    {{AURORA_GAMEPAD_AXIS_LEFTY, AXIS_SIGN_NEGATIVE}, AURORA_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_Y_POS},
    {{AURORA_GAMEPAD_AXIS_LEFTY, AXIS_SIGN_POSITIVE}, AURORA_GAMEPAD_BUTTON_INVALID, PAD_AXIS_LEFT_Y_NEG},
    {{AURORA_GAMEPAD_AXIS_RIGHTX, AXIS_SIGN_POSITIVE}, AURORA_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_X_POS},
    {{AURORA_GAMEPAD_AXIS_RIGHTX, AXIS_SIGN_NEGATIVE}, AURORA_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_X_NEG},
    // see above
    {{AURORA_GAMEPAD_AXIS_RIGHTY, AXIS_SIGN_NEGATIVE}, AURORA_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_Y_POS},
    {{AURORA_GAMEPAD_AXIS_RIGHTY, AXIS_SIGN_POSITIVE}, AURORA_GAMEPAD_BUTTON_INVALID, PAD_AXIS_RIGHT_Y_NEG},
    {{AURORA_GAMEPAD_AXIS_LEFT_TRIGGER, AXIS_SIGN_POSITIVE}, AURORA_GAMEPAD_BUTTON_INVALID, PAD_AXIS_TRIGGER_L},
    {{AURORA_GAMEPAD_AXIS_RIGHT_TRIGGER, AXIS_SIGN_POSITIVE}, AURORA_GAMEPAD_BUTTON_INVALID, PAD_AXIS_TRIGGER_R},
}};

template <typename T, size_t N>
constexpr const std::array<T, N>& toStdArray(const T (&array)[N]) {
  static_assert(sizeof(array) == sizeof(std::array<T, N>));
  return reinterpret_cast<const std::array<T, N>&>(array);
}

struct PADKeyboardState {
  std::array<PADKeyButtonBinding, PAD_BUTTON_COUNT> m_buttonMapping{};
  std::array<PADKeyAxisBinding, PAD_AXIS_COUNT> m_axisMapping{};
  bool m_mappingsSet = false;
};

std::array<PADKeyboardState, PAD_MAX_CONTROLLERS> g_keyboardBindings;

struct PADCLampRegion {
  uint8_t minTrigger;
  uint8_t maxTrigger;
  int8_t minStick;
  int8_t maxStick;
  int8_t xyStick;
  int8_t minSubstick;
  int8_t maxSubstick;
  int8_t xySubstick;
  int8_t radStick;
  int8_t radSubstick;
};

constexpr PADCLampRegion ClampRegion{
    // Triggers
    30,
    180,

    // Left stick
    15,
    72,
    40,

    // Right stick
    15,
    59,
    31,

    // Stick radii
    56,
    44,
};

bool g_initialized;
bool g_keyboardBindingsLoaded = false;
std::atomic_bool g_blockPAD{false};
bool g_suppressHeldOnRead = false;
std::array<PADButton, PAD_CHANMAX> g_suppressedButtons{};
std::array<bool, PAD_CHANMAX> g_suppressLeftTrigger{};
std::array<bool, PAD_CHANMAX> g_suppressRightTrigger{};

bool is_mouse_scancode(const s32 scancode) { return scancode < PAD_KEY_INVALID; }
bool is_native_binding_pressed(AuroraGamepad* gamepad, u32 binding) {
  if (PADIsAxisButton(binding)) {
    const u32 axis = PADAxisButtonAxis(binding);
    const u32 threshold = PADAxisButtonThreshold(binding);
    if (axis >= AURORA_GAMEPAD_AXIS_COUNT || threshold < 1 || threshold > 100) return false;
    int value = aurora_gamepad_axis(gamepad, static_cast<AuroraGamepadAxis>(axis));
    if (PADAxisButtonNegative(binding)) value = -value;
    return value > 0 && value * 100 >= static_cast<int>(threshold) * 32767;
  }
  return binding < AURORA_GAMEPAD_BUTTON_COUNT &&
         aurora_gamepad_button(gamepad, static_cast<AuroraGamepadButton>(binding));
}
bool is_mouse_button_pressed(const s32 scancode) {
  const int32_t buttonNum = -(scancode + 1);
  if (buttonNum < 1 || buttonNum > 5) {
    return false;
  }
  float x, y;
  const auto buttons = aurora_mouse_buttons(&x, &y);
  return (buttons & 1u << (buttonNum - 1)) != 0u;
}
} // namespace

void PADSetSpec(u32 spec [[maybe_unused]]) {}

static void load_keyboard_bindings();
static void save_keyboard_bindings();

// ReSharper disable once CppDFAConstantFunctionResult
BOOL PADInit() {
  if (g_initialized) {
    return true;
  }
  g_initialized = true;

  std::ranges::for_each(g_keyboardBindings, [](auto& state) {
    state.m_buttonMapping = g_defaultKeys;
    state.m_axisMapping = g_defaultKeyAxis;
  });

  return true;
}

BOOL PADRecalibrate(u32 mask [[maybe_unused]]) { return true; }

BOOL PADReset(u32 mask [[maybe_unused]]) { return true; }

void PADSetAnalogMode(u32 mode [[maybe_unused]]) {}

aurora::input::GameController* __PADGetControllerForIndex(const u32 idx) /*  NOLINT(*-reserved-identifier) */
{
  if (idx >= aurora::input::g_GameControllers.size()) {
    return nullptr;
  }

  uint32_t tmp = 0;
  auto iter = aurora::input::g_GameControllers.begin();
  while (tmp < idx) {
    ++iter;
    ++tmp;
  }
  if (iter == aurora::input::g_GameControllers.end()) {
    return nullptr;
  }

  return &iter->second;
}

u32 PADCount() { return aurora::input::g_GameControllers.size(); }

const char* PADGetNameForControllerIndex(const u32 idx) {
  const auto* ctrl = __PADGetControllerForIndex(idx);
  if (ctrl == nullptr) {
    return nullptr;
  }

  return aurora_gamepad_name(ctrl->m_controller);
}

void PADSetPortForIndex(const u32 idx, const u32 port) {
  const auto* ctrl = __PADGetControllerForIndex(idx);
  if (ctrl == nullptr) {
    return;
  }

  const int32_t oldPort = aurora_gamepad_player_index(ctrl->m_controller);
  if (const auto* dest = aurora::input::get_controller_for_player(port); dest != nullptr && dest != ctrl) {
    aurora_gamepad_set_player_index(dest->m_controller, -1);
  }
  if (oldPort >= 0 && oldPort != port) {
    aurora::input::persist_controller_for_player(oldPort, nullptr);
  }
  aurora_gamepad_set_player_index(ctrl->m_controller, static_cast<int32_t>(port));
  aurora::input::persist_controller_for_player(port, ctrl);
}

int32_t PADGetIndexForPort(const u32 port) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return -1;
  }
  int32_t index = 0;
  for (auto iter = aurora::input::g_GameControllers.begin(); iter != aurora::input::g_GameControllers.end();
       ++iter, ++index) {
    if (&iter->second == ctrl) {
      break;
    }
  }

  return index;
}

void PADClearPort(const u32 port) {
  aurora::input::persist_controller_for_player(port, nullptr);
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return;
  }
  aurora_gamepad_set_player_index(ctrl->m_controller, -1);
}

// Secondary bindings live only in memory; the runtime re-applies them from its
// config after every (re)load, so any mapping reset also clears them.
static void reset_alt_button_mapping(aurora::input::GameController* controller) {
  for (size_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
    controller->m_altButtonMapping[i] = {PAD_NATIVE_BUTTON_INVALID, controller->m_buttonMapping[i].padButton};
  }
}

// SDL's hidapi Wii driver names the pad "Nintendo Wii U Pro Controller"; the
// other names it produces are Wii Remotes, which the game reads through KPAD
// and which therefore never take a GameCube mapping.
static bool wii_default_mapping(const aurora::input::GameController* controller,
                                std::array<PADButtonMapping, PAD_BUTTON_COUNT>& out) {
  const char* name = aurora_gamepad_name(controller->m_controller);
  if (name == nullptr || std::strstr(name, "Wii U Pro Controller") == nullptr) {
    return false;
  }
  out = g_defaultButtonsWiiUPro;
  return true;
}

// Picks the default button table for a controller by name (Wii pads) or SDL gamepad type.
void __PADSetDefaultMapping(aurora::input::GameController* controller) /*  NOLINT(*-reserved-identifier) */
{
  if (wii_default_mapping(controller, controller->m_buttonMapping)) {
    reset_alt_button_mapping(controller);
    return;
  }
  switch (aurora_gamepad_type(controller->m_controller)) {
  case AURORA_GAMEPAD_TYPE_XBOX360:
    controller->m_buttonMapping = g_defaultButtonsXBox360;
    break;
  case AURORA_GAMEPAD_TYPE_XBOXONE:
    controller->m_buttonMapping = g_defaultButtonsXBoxOne;
    break;
  case AURORA_GAMEPAD_TYPE_STANDARD:
    controller->m_buttonMapping = g_defaultButtonsStandard;
    break;
  case AURORA_GAMEPAD_TYPE_PS3:
    controller->m_buttonMapping = g_defaultButtonsPS3;
    break;
  case AURORA_GAMEPAD_TYPE_PS4:
    controller->m_buttonMapping = g_defaultButtonsPS4;
    break;
  case AURORA_GAMEPAD_TYPE_PS5:
    controller->m_buttonMapping = g_defaultButtonsPS5;
    break;
  case AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    if (controller->m_pid == 0x2073) {
      controller->m_buttonMapping = g_defaultButtonsNSOGamecube;
    } else {
      controller->m_buttonMapping = g_defaultButtonsProCon;
    }
    break;
  case AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    controller->m_buttonMapping = g_defaultButtonsJoyConRight;
    break;
  case AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    controller->m_buttonMapping = g_defaultButtonsJoyConLeft;
    break;
  case AURORA_GAMEPAD_TYPE_GAMECUBE:
    controller->m_buttonMapping = g_defaultButtonsGamecube;
    break;
  default:
    controller->m_buttonMapping = g_defaultButtonsStandard;
    break;
  }
  reset_alt_button_mapping(controller);
}

static bool is_valid_native_axis(const PADSignedNativeAxis axis) {
  return axis.nativeAxis == -1 ||
         (axis.nativeAxis >= 0 && axis.nativeAxis < AURORA_GAMEPAD_AXIS_COUNT &&
          (axis.sign == AXIS_SIGN_POSITIVE || axis.sign == AXIS_SIGN_NEGATIVE));
}

static PADDeadZones default_dead_zones(const aurora::input::GameController& controller) {
  return {
      .emulateTriggers = !(controller.m_isGameCube ||
                           (aurora_gamepad_type(controller.m_controller) == AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO &&
                            controller.m_pid == 0x2073)),
      .useDeadzones = true,
      .stickDeadZone = 8000,
      .substickDeadZone = 8000,
      .leftTriggerActivationZone = 31150,
      .rightTriggerActivationZone = 31150,
  };
}

static void sanitize_dead_zones(aurora::input::GameController* controller, const int32_t playerIndex) {
  PADDeadZones& deadZones = controller->m_deadZones;
  const bool invalid =
      deadZones.stickDeadZone > AURORA_JOYSTICK_AXIS_MAX || deadZones.substickDeadZone > AURORA_JOYSTICK_AXIS_MAX ||
      deadZones.leftTriggerActivationZone > AURORA_JOYSTICK_AXIS_MAX ||
      deadZones.rightTriggerActivationZone > AURORA_JOYSTICK_AXIS_MAX;
  if (!invalid) {
    return;
  }

  aurora::input::Log.warn("__PADLoadMapping port={}: invalid deadzone data in file, resetting to defaults",
                          playerIndex);
  deadZones = default_dead_zones(*controller);
}

void __PADLoadMapping(aurora::input::GameController* controller) /*  NOLINT(*-reserved-identifier) */ {
  int32_t playerIndex = aurora_gamepad_player_index(controller->m_controller);
  if (playerIndex == -1) {
    return;
  }

  const std::filesystem::path basePath = fs_path_from_string(aurora::g_config.userPath);
  if (!controller->m_mappingLoaded) {
    __PADSetDefaultMapping(controller);
    controller->m_axisMapping = g_defaultAxes;
  }

  controller->m_mappingLoaded = true;

  const auto path = fs_path_to_string(
      basePath / fmt::format("{}_{:04X}_{:04X}.controller", PADGetName(playerIndex), controller->m_vid,
                             controller->m_pid));
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return;
  }

  uint32_t magic = 0;
  read_u32le(file, &magic);
  if (magic != SBIG('CTRL')) {
    aurora::input::Log.warn("Invalid controller mapping magic!");
    std::fclose(file);
    return;
  }

  uint32_t version = 0;
  read_u32le(file, &version);
  if (version != k_mappingsFileVersion) {
    aurora::input::Log.warn("Invalid controller mapping version! (Expected {0}, found {1})", k_mappingsFileVersion,
                            version);
    std::fclose(file);
    return;
  }

  bool isGameCube = false;
  std::fread(&isGameCube, 1, sizeof(bool), file);
  std::fseek(file, std::ftell(file) + 31 & ~31, SEEK_SET);
  const auto dataStart = std::ftell(file);
  if (dataStart == -1) {
    aurora::input::Log.warn("Unable to seek in controller bindings! Path: \"{}\"", path);
    std::fclose(file);
    return;
  }
  if (isGameCube) {
    constexpr uint32_t dzSecLen = sizeof(PADDeadZones);
    constexpr uint32_t btnSecLen = sizeof(PADButtonMapping) * PAD_BUTTON_COUNT;
    constexpr uint32_t axisSecLen = sizeof(PADAxisMapping) * PAD_AXIS_COUNT;
    std::fseek(file, dataStart + (dzSecLen + btnSecLen + axisSecLen) * playerIndex, SEEK_SET);
  }

  std::fread(&controller->m_deadZones, 1, sizeof(PADDeadZones), file);
  std::fread(&controller->m_buttonMapping, 1, sizeof(PADButtonMapping) * PAD_BUTTON_COUNT, file);
  std::fread(&controller->m_axisMapping, 1, sizeof(PADAxisMapping) * PAD_AXIS_COUNT, file);
  if (!isGameCube) {
    std::fread(&controller->m_rumbleIntensityLow, 1, sizeof(u16), file);
    std::fread(&controller->m_rumbleIntensityHigh, 1, sizeof(u16), file);
  }
  std::fclose(file);
  sanitize_dead_zones(controller, playerIndex);

  bool axisCorrupt = false;
  for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
    if (controller->m_axisMapping[i].padAxis != static_cast<PADAxis>(i) ||
        !is_valid_native_axis(controller->m_axisMapping[i].nativeAxis)) {
      axisCorrupt = true;
      break;
    }
  }
  if (axisCorrupt) {
    aurora::input::Log.warn("__PADLoadMapping port={}: corrupt axis data in file, resetting axes to defaults",
                            playerIndex);
    controller->m_axisMapping = g_defaultAxes;
  }

  bool buttonCorrupt = false;
  for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
    if (controller->m_buttonMapping[i].padButton == 0) {
      buttonCorrupt = true;
      break;
    }
  }
  if (buttonCorrupt) {
    aurora::input::Log.warn("__PADLoadMapping port={}: corrupt button data in file, resetting buttons to defaults",
                            playerIndex);
    __PADSetDefaultMapping(controller);
  }
  reset_alt_button_mapping(controller);
}

static void EnsureMappingLoaded(aurora::input::GameController* controller) {
  if (!controller->m_mappingLoaded) {
    __PADLoadMapping(controller);
  }
}

static int16_t _get_axis_value(const aurora::input::GameController* controller, //  NOLINT(*-reserved-identifier)
                              PADAxis axis) {
  const auto iter =
      std::ranges::find_if(controller->m_axisMapping, [axis](const auto& pair) { return pair.padAxis == axis; });
  if (iter == controller->m_axisMapping.end()) {
    return 0;
  }

  if (iter->nativeAxis.nativeAxis != -1) {
    const auto [nativeAxis, sign] = iter->nativeAxis;
    const auto value = aurora_gamepad_axis(controller->m_controller, static_cast<AuroraGamepadAxis>(nativeAxis));
    if (sign == AXIS_SIGN_POSITIVE) {
      return value > 0 ? value : 0;
    }
    if (value >= 0) {
      return 0;
    }
    // Clamp before negating so SDL's -32768 minimum fits in int16_t.
    return static_cast<int16_t>(value == AURORA_JOYSTICK_AXIS_MIN ? AURORA_JOYSTICK_AXIS_MAX : -value);
  }

  assert(iter->nativeButton != -1);
  if (aurora_gamepad_button(controller->m_controller, static_cast<AuroraGamepadButton>(iter->nativeButton))) {
    return AURORA_JOYSTICK_AXIS_MAX;
  }
  return 0;
}

static void neutralize_status(PADStatus& status) {
  status.button = 0;
  status.stickX = 0;
  status.stickY = 0;
  status.substickX = 0;
  status.substickY = 0;
  status.triggerLeft = 0;
  status.triggerRight = 0;
  status.analogA = 0;
  status.analogB = 0;
}

static void apply_unblock_suppression(PADStatus& status, const u32 port, const bool captureHeldInput) {
  if (captureHeldInput) {
    g_suppressedButtons[port] |= status.button;
    g_suppressLeftTrigger[port] = g_suppressLeftTrigger[port] || status.triggerLeft > ClampRegion.minTrigger;
    g_suppressRightTrigger[port] = g_suppressRightTrigger[port] || status.triggerRight > ClampRegion.minTrigger;
  }

  g_suppressedButtons[port] &= status.button;
  status.button &= ~g_suppressedButtons[port];

  if (g_suppressLeftTrigger[port]) {
    if (status.triggerLeft <= ClampRegion.minTrigger) {
      g_suppressLeftTrigger[port] = false;
    } else {
      status.triggerLeft = 0;
    }
  }

  if (g_suppressRightTrigger[port]) {
    if (status.triggerRight <= ClampRegion.minTrigger) {
      g_suppressRightTrigger[port] = false;
    } else {
      status.triggerRight = 0;
    }
  }
}

u32 PADRead(PADStatus* status) {
  aurora::input::poll();
  if (!g_keyboardBindingsLoaded) {
    g_keyboardBindingsLoaded = true;
    load_keyboard_bindings();
  }

  int numKeys = 0;
  const bool* kbState = aurora_keyboard_state(&numKeys);
  const bool inputBlocked = g_blockPAD.load(std::memory_order_acquire);
  const bool captureHeldInput = g_suppressHeldOnRead && !inputBlocked;
  g_suppressHeldOnRead = false;

  uint32_t rumbleSupport = 0;
  for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
    memset(&status[i], 0, sizeof(PADStatus));
    auto controller = aurora::input::get_controller_for_player(i);
    if (controller == nullptr && !g_keyboardBindings[i].m_mappingsSet) {
      status[i].err = PAD_ERR_NO_CONTROLLER;
      g_suppressedButtons[i] = 0;
      g_suppressLeftTrigger[i] = false;
      g_suppressRightTrigger[i] = false;
      continue;
    }

    status[i].err = PAD_ERR_NONE;
    if (g_keyboardBindings[i].m_mappingsSet && aurora_keyboard_focused()) {
      std::ranges::for_each(
          g_keyboardBindings[i].m_buttonMapping, [&kbState, &numKeys, &i, &status](const PADKeyButtonBinding& mapping) {
            if (mapping.scancode > PAD_KEY_INVALID && mapping.scancode < numKeys && kbState[mapping.scancode]) {
              status[i].button |= mapping.padButton;
            } else if (is_mouse_scancode(mapping.scancode) && is_mouse_button_pressed(mapping.scancode)) {
              status[i].button |= mapping.padButton;
            }
          });

      int lx = 0, ly = 0, rx = 0, ry = 0, tl = 0, tr = 0;
      for (const auto& binding : g_keyboardBindings[i].m_axisMapping) {
        bool pressed = false;
        if (binding.scancode > PAD_KEY_INVALID) {
          pressed = binding.scancode < numKeys && kbState[binding.scancode];
        } else if (is_mouse_scancode(binding.scancode)) {
          pressed = is_mouse_button_pressed(binding.scancode);
        }
        if (!pressed) {
          continue;
        }
        switch (binding.padAxis) {
        case PAD_AXIS_LEFT_X_POS:
          lx += 127;
          break;
        case PAD_AXIS_LEFT_X_NEG:
          lx -= 127;
          break;
        case PAD_AXIS_LEFT_Y_POS:
          ly += 127;
          break;
        case PAD_AXIS_LEFT_Y_NEG:
          ly -= 127;
          break;
        case PAD_AXIS_RIGHT_X_POS:
          rx += 127;
          break;
        case PAD_AXIS_RIGHT_X_NEG:
          rx -= 127;
          break;
        case PAD_AXIS_RIGHT_Y_POS:
          ry += 127;
          break;
        case PAD_AXIS_RIGHT_Y_NEG:
          ry -= 127;
          break;
        case PAD_AXIS_TRIGGER_L:
          tl += 255;
          break;
        case PAD_AXIS_TRIGGER_R:
          tr += 255;
          break;
        default:
          break;
        }
      }
      status[i].stickX = static_cast<s8>(std::clamp(static_cast<int>(status[i].stickX) + lx, -127, 127));
      status[i].stickY = static_cast<s8>(std::clamp(static_cast<int>(status[i].stickY) + ly, -127, 127));
      status[i].substickX = static_cast<s8>(std::clamp(static_cast<int>(status[i].substickX) + rx, -127, 127));
      status[i].substickY = static_cast<s8>(std::clamp(static_cast<int>(status[i].substickY) + ry, -127, 127));
      status[i].triggerLeft = static_cast<u8>(std::min(static_cast<int>(status[i].triggerLeft) + tl, 255));
      status[i].triggerRight = static_cast<u8>(std::min(static_cast<int>(status[i].triggerRight) + tr, 255));
    }

    if (controller && !g_keyboardBindings[i].m_mappingsSet) {
      EnsureMappingLoaded(controller);

      // Wii U Pro Controller raw D-pad fallback. SDL's HIDAPI Wii driver posts
      // the D-pad as joystick buttons 11-14 (the AURORA_GAMEPAD_BUTTON_DPAD_*
      // values) and never as a hat, but the mapping SDL generates for HIDAPI
      // pads binds the D-pad to hat 0, so aurora_gamepad_button(DPAD_*) stays
      // false. Keep this restricted to the Wii driver's pad so raw button
      // indices don't interfere with other controller types.
      const char* name = aurora_gamepad_name(controller->m_controller);
      const bool isWiiUPro = name != nullptr && std::strstr(name, "Wii U Pro Controller") != nullptr;

      if (isWiiUPro) {
        uint32_t raw = 0;
        const int buttonCount = aurora_gamepad_raw_button_count(controller->m_controller);

        for (int b = 0; b < buttonCount && b < 32; ++b) {
          if (aurora_gamepad_raw_button(controller->m_controller, b)) {
            raw |= (1u << b);
          }
        }

        // Up    = button 11
        if (raw & (1u << 11)) {
          status[i].button |= PAD_BUTTON_UP;
        }
        // Down  = button 12
        if (raw & (1u << 12)) {
          status[i].button |= PAD_BUTTON_DOWN;
        }
        // Left  = button 13
        if (raw & (1u << 13)) {
          status[i].button |= PAD_BUTTON_LEFT;
        }
        // Right = button 14
        if (raw & (1u << 14)) {
          status[i].button |= PAD_BUTTON_RIGHT;
        }
      }

      bool leftTriggerSet = false;
      bool rightTriggerSet = false;
      std::ranges::for_each(controller->m_buttonMapping, [&controller, &i, &status, &leftTriggerSet,
                                                          &rightTriggerSet](const auto& mapping) {
        if (is_native_binding_pressed(controller->m_controller, mapping.nativeButton)) {
          status[i].button |= mapping.padButton;
        }

        if (mapping.padButton == PAD_TRIGGER_L && mapping.nativeButton != PAD_NATIVE_BUTTON_INVALID) {
          leftTriggerSet = true;
        }
        if (mapping.padButton == PAD_TRIGGER_R && mapping.nativeButton != PAD_NATIVE_BUTTON_INVALID) {
          rightTriggerSet = true;
        }
      });

      std::ranges::for_each(controller->m_altButtonMapping, [&controller, &i, &status, &leftTriggerSet,
                                                             &rightTriggerSet](const auto& mapping) {
        if (mapping.nativeButton == PAD_NATIVE_BUTTON_INVALID) {
          return;
        }
        if (is_native_binding_pressed(controller->m_controller, mapping.nativeButton)) {
          status[i].button |= mapping.padButton;
        }

        if (mapping.padButton == PAD_TRIGGER_L) {
          leftTriggerSet = true;
        }
        if (mapping.padButton == PAD_TRIGGER_R) {
          rightTriggerSet = true;
        }
      });


      // TODO: Add serializable mappings for these (probably not necessary)?
      static constexpr std::array<std::pair<AuroraGamepadButton, PADExtButton>, PAD_EXT_BUTTON_COUNT> kExtButtonMappings{{
          {AURORA_GAMEPAD_BUTTON_BACK, PAD_BUTTON_BACK},
          {AURORA_GAMEPAD_BUTTON_GUIDE, PAD_BUTTON_GUIDE},
          {AURORA_GAMEPAD_BUTTON_MISC1, PAD_BUTTON_MISC1},
          {AURORA_GAMEPAD_BUTTON_MISC2, PAD_BUTTON_MISC2},
          {AURORA_GAMEPAD_BUTTON_MISC3, PAD_BUTTON_MISC3},
          {AURORA_GAMEPAD_BUTTON_MISC4, PAD_BUTTON_MISC4},
          {AURORA_GAMEPAD_BUTTON_MISC5, PAD_BUTTON_MISC5},
          {AURORA_GAMEPAD_BUTTON_MISC6, PAD_BUTTON_MISC6},
          {AURORA_GAMEPAD_BUTTON_RIGHT_PADDLE1, PAD_BUTTON_RIGHT_PADDLE1},
          {AURORA_GAMEPAD_BUTTON_LEFT_PADDLE1, PAD_BUTTON_LEFT_PADDLE1},
          {AURORA_GAMEPAD_BUTTON_RIGHT_PADDLE2, PAD_BUTTON_RIGHT_PADDLE2},
          {AURORA_GAMEPAD_BUTTON_LEFT_PADDLE2, PAD_BUTTON_LEFT_PADDLE2},
          {AURORA_GAMEPAD_BUTTON_RIGHT_STICK, PAD_BUTTON_RIGHT_STICK},
          {AURORA_GAMEPAD_BUTTON_LEFT_STICK, PAD_BUTTON_LEFT_STICK},
          {AURORA_GAMEPAD_BUTTON_TOUCHPAD, PAD_BUTTON_TOUCHPAD},
      }};

      for (const auto& [native, button] : kExtButtonMappings) {
        if (aurora_gamepad_button(controller->m_controller, native)) {
          status[i].extButton |= button;
        }
      }

      const auto xlPos = _get_axis_value(controller, PAD_AXIS_LEFT_X_POS);
      const auto xlNeg = _get_axis_value(controller, PAD_AXIS_LEFT_X_NEG);
      const auto ylPos = _get_axis_value(controller, PAD_AXIS_LEFT_Y_POS);
      const auto ylNeg = _get_axis_value(controller, PAD_AXIS_LEFT_Y_NEG);

      auto xl = static_cast<int16_t>(xlPos - xlNeg);
      auto yl = static_cast<int16_t>(ylPos - ylNeg);
      if (controller->m_deadZones.useDeadzones) {
        if (std::abs(xl) > controller->m_deadZones.stickDeadZone) {
          xl /= 256;
        } else {
          xl = 0;
        }
        if (std::abs(yl) > controller->m_deadZones.stickDeadZone) {
          yl /= 256;
        } else {
          yl = 0;
        }
      } else {
        xl /= 256;
        yl /= 256;
      }

      status[i].stickX = static_cast<int8_t>(xl);
      status[i].stickY = static_cast<int8_t>(yl);

      const auto xrPos = _get_axis_value(controller, PAD_AXIS_RIGHT_X_POS);
      const auto xrNeg = _get_axis_value(controller, PAD_AXIS_RIGHT_X_NEG);
      const auto yrPos = _get_axis_value(controller, PAD_AXIS_RIGHT_Y_POS);
      const auto yrNeg = _get_axis_value(controller, PAD_AXIS_RIGHT_Y_NEG);

      auto xr = static_cast<int16_t>(xrPos - xrNeg);
      auto yr = static_cast<int16_t>(yrPos - yrNeg);
      if (controller->m_deadZones.useDeadzones) {
        if (std::abs(xr) > controller->m_deadZones.substickDeadZone) {
          xr /= 256;
        } else {
          xr = 0;
        }

        if (std::abs(yr) > controller->m_deadZones.substickDeadZone) {
          yr /= 256;
        } else {
          yr = 0;
        }
      } else {
        xr /= 256;
        yr /= 256;
      }

      status[i].substickX = static_cast<int8_t>(xr);
      status[i].substickY = static_cast<int8_t>(yr);

      int16_t tl = std::max(static_cast<int16_t>(0), _get_axis_value(controller, PAD_AXIS_TRIGGER_L));
      int16_t tr = std::max(static_cast<int16_t>(0), _get_axis_value(controller, PAD_AXIS_TRIGGER_R));

      // Games can read either the digital L/R bits or their analog pressure.
      // An explicit button binding must drive both, otherwise the original
      // L2/R2 axis still activates L/R even when it was rebound to L1/R1.
      // Real GC pads retain independent analog travel and end-stop clicks.
      if (!(controller->m_isGameCube ||
            (aurora_gamepad_type(controller->m_controller) == AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO &&
             controller->m_pid == 0x2073))) {
        if (leftTriggerSet) tl = (status[i].button & PAD_TRIGGER_L) != 0 ? 32767 : 0;
        if (rightTriggerSet) tr = (status[i].button & PAD_TRIGGER_R) != 0 ? 32767 : 0;
      }

      if (controller->m_deadZones.emulateTriggers) {
        if (!leftTriggerSet && tl > controller->m_deadZones.leftTriggerActivationZone) {
          status[i].button |= PAD_TRIGGER_L;
        }
        if (!rightTriggerSet && tr > controller->m_deadZones.rightTriggerActivationZone) {
          status[i].button |= PAD_TRIGGER_R;
        }
      }
      tl /= 128;
      tr /= 128;

      status[i].triggerLeft = static_cast<int8_t>(tl);
      status[i].triggerRight = static_cast<int8_t>(tr);

      if (controller->m_hasRumble) {
        rumbleSupport |= PAD_CHAN0_BIT >> i;
      }

      // Update the LED colors when they exist and the controller is read (which should happen once per frame in most
      // games)
      if (controller->m_hasRgbLed && controller->m_isColorDirty) {
        aurora_gamepad_set_led(controller->m_controller, controller->m_ledRed, controller->m_ledGreen,
                          controller->m_ledBlue);
        controller->m_isColorDirty = false;
      }
    }

    if (inputBlocked) {
      neutralize_status(status[i]);
    } else {
      apply_unblock_suppression(status[i], i, captureHeldInput);
    }
  }
  return rumbleSupport;
}

void PADControlMotor(const u32 chan, const u32 cmd) {
  const auto controller = aurora::input::get_controller_for_player(chan);
  const auto instance = aurora::input::get_instance_for_player(chan);
  if (controller == nullptr) {
    return;
  }

  if (controller->m_isGameCube) {
    if (cmd == PAD_MOTOR_STOP || cmd == PAD_MOTOR_STOP_HARD) {
      // Use an unambiguous motor-off request. The (0, 1) coast encoding
      // requires SDL's GameCube brake mode; other backends or an overridden
      // hint interpret it as rumble and can leave the controller vibrating.
      aurora::input::controller_rumble(instance, 0, 0, 0);
    } else if (cmd == PAD_MOTOR_RUMBLE) {
      aurora::input::controller_rumble(instance, 1, 1, 0);
    }
  } else {
    if (cmd == PAD_MOTOR_STOP) {
      aurora::input::controller_rumble(instance, 0, 0, 1);
    } else if (cmd == PAD_MOTOR_RUMBLE) {
      aurora::input::controller_rumble(instance, controller->m_rumbleIntensityLow, controller->m_rumbleIntensityHigh,
                                       0);
    } else if (cmd == PAD_MOTOR_STOP_HARD) {
      aurora::input::controller_rumble(instance, 0, 0, 0);
    }
  }
}

void PADControlAllMotors(const u32* cmdArr) {
  for (u32 i = 0; i < PAD_CHANMAX; ++i) {
    PADControlMotor(i, cmdArr[i]);
  }
}

void ClampTrigger(u8* trigger, const u8 min, const u8 max) {
  if (*trigger <= min) {
    *trigger = 0;
  } else {
    if (*trigger > max) {
      *trigger = max;
    }
    *trigger -= min;
  }
}

void ClampCircle(s8* px, s8* py, const s8 radius, const s8 min) {
  int x = *px; // NOLINT(*-str34-c)
  int y = *py; // NOLINT(*-str34-c)

  if (-min < x && x < min) {
    x = 0;
  } else if (0 < x) {
    x -= min;
  } else {
    x += min;
  }

  if (-min < y && y < min) {
    y = 0;
  } else if (0 < y) {
    y -= min;
  } else {
    y += min;
  }

  if (const int squared = x * x + y * y; radius * radius < squared) {
    const auto length = static_cast<int32_t>(std::sqrt(squared));
    x = x * radius / length;
    y = y * radius / length;
  }

  *px = static_cast<int8_t>(x);
  *py = static_cast<int8_t>(y);
}

void ClampStick(s8* px, s8* py, const s8 max, const s8 xy, const s8 min) {
  int32_t x = *px; // NOLINT(*-str34-c)
  int32_t y = *py; // NOLINT(*-str34-c)

  int32_t signX = 0;
  if (0 <= x) {
    signX = 1;
  } else {
    signX = -1;
    x = -x;
  }

  int8_t signY = 0;
  if (0 <= y) {
    signY = 1;
  } else {
    signY = -1;
    y = -y;
  }

  if (x <= min) {
    x = 0;
  } else {
    x -= min;
  }
  if (y <= min) {
    y = 0;
  } else {
    y -= min;
  }

  if (x == 0 && y == 0) {
    *px = *py = 0;
    return;
  }

  x = x * max / (INT8_MAX - min);
  y = y * max / (INT8_MAX - min);

  if (xy * y <= xy * x) {
    if (const int32_t d = xy * x + (max - xy) * y; xy * max < d) {
      x = xy * max * x / d;
      y = xy * max * y / d;
    }
  } else {
    if (const int32_t d = xy * y + (max - xy) * x; xy * max < d) {
      x = xy * max * x / d;
      y = xy * max * y / d;
    }
  }

  *px = static_cast<s8>(signX * x);
  *py = static_cast<s8>(signY * y);
}

void PADClamp(PADStatus* status) {
  for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
    if (status[i].err != PAD_ERR_NONE) {
      continue;
    }

    ClampStick(&status[i].stickX, &status[i].stickY, ClampRegion.maxStick, ClampRegion.xyStick, ClampRegion.minStick);
    ClampStick(&status[i].substickX, &status[i].substickY, ClampRegion.maxSubstick, ClampRegion.xySubstick,
               ClampRegion.minSubstick);
    ClampTrigger(&status[i].triggerLeft, ClampRegion.minTrigger, ClampRegion.maxTrigger);
    ClampTrigger(&status[i].triggerRight, ClampRegion.minTrigger, ClampRegion.maxTrigger);
  }
}

void PADClampCircle(PADStatus* status) {
  for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
    if (status[i].err != PAD_ERR_NONE) {
      continue;
    }

    ClampCircle(&status[i].stickX, &status[i].stickY, ClampRegion.radStick, ClampRegion.minStick);
    ClampCircle(&status[i].substickX, &status[i].substickY, ClampRegion.radSubstick, ClampRegion.minSubstick);
    ClampTrigger(&status[i].triggerLeft, ClampRegion.minTrigger, ClampRegion.maxTrigger);
    ClampTrigger(&status[i].triggerRight, ClampRegion.minTrigger, ClampRegion.maxTrigger);
  }
}

void PADGetVidPid(const u32 port, u32* vid, u32* pid) {
  *vid = 0;
  *pid = 0;
  const auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }

  *vid = controller->m_vid;
  *pid = controller->m_pid;
}

const char* PADGetName(const u32 port) {
  const auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return nullptr;
  }

  return aurora_gamepad_name(controller->m_controller);
}

void PADSetButtonMapping(const u32 port, const PADButtonMapping mapping) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }

  const auto iter = std::ranges::find_if(controller->m_buttonMapping,
                                         [mapping](const auto& pair) { return mapping.padButton == pair.padButton; });
  if (iter == controller->m_buttonMapping.end()) {
    return;
  }

  *iter = mapping;
}

void PADSetAllButtonMappings(const u32 port, const PADButtonMapping buttons[PAD_BUTTON_COUNT]) {
  for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
    PADSetButtonMapping(port, buttons[i]);
  }
}

PADButtonMapping* PADGetButtonMappings(const u32 port, u32* buttonCount) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    *buttonCount = 0;
    return nullptr;
  }

  EnsureMappingLoaded(controller);

  *buttonCount = PAD_BUTTON_COUNT;
  return controller->m_buttonMapping.data();
}

void PADSetAltButtonMapping(const u32 port, const PADButtonMapping mapping) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }

  const auto iter = std::ranges::find_if(controller->m_altButtonMapping,
                                         [mapping](const auto& pair) { return mapping.padButton == pair.padButton; });
  if (iter == controller->m_altButtonMapping.end()) {
    return;
  }

  *iter = mapping;
}

PADButtonMapping* PADGetAltButtonMappings(const u32 port, u32* buttonCount) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    *buttonCount = 0;
    return nullptr;
  }

  EnsureMappingLoaded(controller);
  *buttonCount = PAD_BUTTON_COUNT;
  return controller->m_altButtonMapping.data();
}

void PADSetAxisMapping(const u32 port, const PADAxisMapping mapping) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }

  const auto iter = std::ranges::find_if(controller->m_axisMapping,
                                         [mapping](const auto& pair) { return mapping.padAxis == pair.padAxis; });
  if (iter == controller->m_axisMapping.end()) {
    return;
  }

  *iter = mapping;
}

void PADSetAllAxisMappings(const u32 port, const PADAxisMapping axes[PAD_AXIS_COUNT]) {
  for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
    PADSetAxisMapping(port, axes[i]);
  }
}

PADAxisMapping* PADGetAxisMappings(const u32 port, u32* axisCount) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    *axisCount = 0;
    return nullptr;
  }

  EnsureMappingLoaded(controller);
  *axisCount = PAD_AXIS_COUNT;
  return controller->m_axisMapping.data();
}

BOOL PADSetKeyButtonBinding(const u32 port, const PADKeyButtonBinding binding) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return FALSE;
  }

  for (auto& state = g_keyboardBindings[port]; auto& [scancode, padButton] : state.m_buttonMapping) {
    if (padButton == binding.padButton) {
      scancode = binding.scancode;
      return TRUE;
    }
  }

  return FALSE;
}

BOOL PADSetKeyButtonBindings(const u32 port, PADKeyButtonBinding bindings[PAD_BUTTON_COUNT]) {
  for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
    if (!PADSetKeyButtonBinding(port, bindings[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

PADKeyButtonBinding* PADGetKeyButtonBindings(const u32 port, u32* buttonCount) {
  PADInit();
  if (!g_keyboardBindingsLoaded) {
    g_keyboardBindingsLoaded = true;
    load_keyboard_bindings();
  }
  if (port >= PAD_MAX_CONTROLLERS || !g_keyboardBindings[port].m_mappingsSet) {
    return nullptr;
  }
  auto& state = g_keyboardBindings[port];
  *buttonCount = PAD_BUTTON_COUNT;
  return state.m_buttonMapping.data();
}

BOOL PADSetKeyAxisBinding(const u32 port, const PADKeyAxisBinding binding) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return FALSE;
  }

  for (auto& state = g_keyboardBindings[port]; auto& b : state.m_axisMapping) {
    if (b.padAxis == binding.padAxis) {
      b.scancode = binding.scancode;
      return TRUE;
    }
  }

  return FALSE;
}
BOOL PADSetKeyAxisBindings(const u32 port, PADKeyAxisBinding bindings[PAD_BUTTON_COUNT]) {
  for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
    if (!PADSetKeyAxisBinding(port, bindings[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

PADKeyAxisBinding* PADGetKeyAxisBindings(const u32 port, u32* axisCount) {
  if (port >= PAD_MAX_CONTROLLERS || !g_keyboardBindings[port].m_mappingsSet) {
    return nullptr;
  }
  auto& state = g_keyboardBindings[port];
  *axisCount = PAD_AXIS_COUNT;
  return state.m_axisMapping.data();
}

void PADSetKeyboardActive(const u32 port, const BOOL active) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return;
  }
  g_keyboardBindings[port].m_mappingsSet = active != FALSE;
}

void PADClearKeyBindings(const u32 port) {
  if (port >= PAD_MAX_CONTROLLERS) {
    return;
  }
  g_keyboardBindings[port].m_buttonMapping = g_defaultKeys;
  g_keyboardBindings[port].m_axisMapping = g_defaultKeyAxis;
  g_keyboardBindings[port].m_mappingsSet = false;
}

constexpr uint32_t k_keyboardMagic = SBIG('KBND');
constexpr int32_t k_keyboardVersion = 3;

static void load_keyboard_bindings() {
  const auto filePath = fs_path_from_string(aurora::g_config.userPath) / "keyboard_bindings.dat";
  std::FILE* file = std::fopen(fs_path_to_string(filePath).c_str(), "rb");
  if (file == nullptr) {
    return;
  }

  uint32_t magic = 0;
  read_u32le(file, &magic);
  if (magic != k_keyboardMagic) {
    aurora::input::Log.warn("keyboard_bindings.dat: invalid magic");
    std::fclose(file);
    return;
  }

  uint32_t version = 0;
  read_u32le(file, &version);
  if (version != k_keyboardVersion) {
    aurora::input::Log.warn("keyboard_bindings.dat: version mismatch (expected {}, got {})", k_keyboardVersion,
                            version);
    std::fclose(file);
    return;
  }

  const int64_t dataStart = std::ftell(file) + 31 & ~31;
  std::fseek(file, dataStart, SEEK_SET);

  for (uint32_t port = 0; port < g_keyboardBindings.size(); ++port) {
    auto& [buttonMapping, axisMapping, mappingsSet] = g_keyboardBindings[port];
    std::fread(&mappingsSet, 1, sizeof(bool), file);
    std::fread(buttonMapping.data(), 1, sizeof(PADKeyButtonBinding) * PAD_BUTTON_COUNT, file);
    std::fread(axisMapping.data(), 1, sizeof(PADKeyAxisBinding) * PAD_AXIS_COUNT, file);

    bool kbButtonCorrupt = false;
    for (uint32_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
      if (buttonMapping[i].padButton != g_defaultKeys[i].padButton) {
        kbButtonCorrupt = true;
        break;
      }
    }
    if (kbButtonCorrupt) {
      aurora::input::Log.warn("keyboard_bindings.dat port={}: corrupt button identifiers, resetting to defaults", port);
      buttonMapping = g_defaultKeys;
    }

    bool kbAxisCorrupt = false;
    for (uint32_t i = 0; i < PAD_AXIS_COUNT; ++i) {
      if (axisMapping[i].padAxis != g_defaultKeyAxis[i].padAxis) {
        kbAxisCorrupt = true;
        break;
      }
    }
    if (kbAxisCorrupt) {
      aurora::input::Log.warn("keyboard_bindings.dat port={}: corrupt axis identifiers, resetting to defaults", port);
      axisMapping = g_defaultKeyAxis;
    }

    if (mappingsSet) {
      const bool anyBound =
          std::ranges::any_of(buttonMapping,
                              [](const PADKeyButtonBinding& b) { return b.scancode != PAD_KEY_INVALID; }) ||
          std::ranges::any_of(axisMapping, [](const PADKeyAxisBinding& b) { return b.scancode != PAD_KEY_INVALID; });
      if (!anyBound) {
        mappingsSet = false;
      }
    }
  }
  std::fclose(file);
}

static void save_keyboard_bindings() {
  const auto filePath = fs_path_from_string(aurora::g_config.userPath) / "keyboard_bindings.dat";
  const auto filePathStr = fs_path_to_string(filePath);
  std::FILE* file = std::fopen(filePathStr.c_str(), "wb");
  if (file == nullptr) {
    aurora::input::Log.warn("save_keyboard_bindings: failed to open {} for writing", filePathStr);
    return;
  }

  write_u32le(file, k_keyboardMagic);
  write_s32le(file, k_keyboardVersion);

  const int64_t dataStart = std::ftell(file) + 31 & ~31;
  std::fseek(file, dataStart, SEEK_SET);

  for (const auto& [buttonMapping, axisMapping, mappingsSet] : g_keyboardBindings) {
    write_u8(file, mappingsSet);
    std::fwrite(buttonMapping.data(), 1, sizeof(PADKeyButtonBinding) * PAD_BUTTON_COUNT, file);
    std::fwrite(axisMapping.data(), 1, sizeof(PADKeyAxisBinding) * PAD_AXIS_COUNT, file);
  }
  std::fclose(file);
}

void __PADWriteDeadZones(std::FILE* file, // NOLINT(*-reserved-identifier)
                         const aurora::input::GameController& controller) {
  std::fwrite(&controller.m_deadZones, 1, sizeof(PADDeadZones), file);
}

void PADSerializeMappings() {
  const std::filesystem::path basePath = fs_path_from_string(aurora::g_config.userPath);

  for (auto& controller : aurora::input::g_GameControllers | std::views::values) {
    EnsureMappingLoaded(&controller);
    const auto filePath =
        basePath / fmt::format("{}_{:04X}_{:04X}.controller", aurora::input::controller_name(controller.m_index),
                               controller.m_vid, controller.m_pid);
    std::string filePathStr = fs_path_to_string(filePath);

    // don't truncate the file if it already exists
    const char* openMode = std::filesystem::exists(filePath) ? "r+b" : "wb";
    std::FILE* file = std::fopen(filePathStr.c_str(), openMode);
    if (file == nullptr) {
      return;
    }
    std::fseek(file, 0, SEEK_SET);

    // write header
    constexpr uint32_t magic = SBIG('CTRL');
    write_u32le(file, magic);
    write_u32le(file, k_mappingsFileVersion);
    write_u8(file, controller.m_isGameCube);

    // start writing data at next 32-byte aligned offset
    const int64_t dataStart = std::ftell(file) + 31 & ~31;
    if (dataStart == -1) {
      aurora::input::Log.warn("Unable to seek in controller bindings! Path: \"{}\"", filePathStr);
      return;
    }
    std::fseek(file, dataStart, SEEK_SET);
    if (controller.m_isGameCube) {
      // GameCube adapters expose 4 input devices with the same vid/pid, we store all 4 in the same file
      const auto port = aurora::input::player_index(controller.m_index);
      constexpr int64_t dzSecLen = sizeof(PADDeadZones);
      constexpr int64_t btnSecLen = sizeof(PADButtonMapping) * PAD_BUTTON_COUNT;
      constexpr int64_t axisSecLen = sizeof(PADAxisMapping) * PAD_AXIS_COUNT;
      // skip to offset in file for this particular port
      std::fseek(file, dataStart + (dzSecLen + btnSecLen + axisSecLen) * port, SEEK_SET);
    }
    __PADWriteDeadZones(file, controller);
    std::fwrite(controller.m_buttonMapping.data(), 1, sizeof(PADButtonMapping) * PAD_BUTTON_COUNT, file);
    std::fwrite(controller.m_axisMapping.data(), 1, sizeof(PADAxisMapping) * PAD_AXIS_COUNT, file);

    if (!controller.m_isGameCube) {
      std::fwrite(&controller.m_rumbleIntensityLow, 1, sizeof(u16), file);
      std::fwrite(&controller.m_rumbleIntensityHigh, 1, sizeof(u16), file);
    }
    std::fclose(file);
  }

  save_keyboard_bindings();
}

PADDeadZones* PADGetDeadZones(const u32 port) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return nullptr;
  }
  return &controller->m_deadZones;
}

static constexpr std::array<std::pair<PADButton, std::string_view>, PAD_BUTTON_COUNT> skButtonNames = {{
    {PAD_BUTTON_LEFT, "Left"sv},
    {PAD_BUTTON_RIGHT, "Right"sv},
    {PAD_BUTTON_DOWN, "Down"sv},
    {PAD_BUTTON_UP, "Up"sv},
    {PAD_TRIGGER_Z, "Z"sv},
    {PAD_TRIGGER_R, "R"sv},
    {PAD_TRIGGER_L, "L"sv},
    {PAD_BUTTON_A, "A"sv},
    {PAD_BUTTON_B, "B"sv},
    {PAD_BUTTON_X, "X"sv},
    {PAD_BUTTON_Y, "Y"sv},
    {PAD_BUTTON_START, "Start"sv},
}};

static constexpr std::array<std::pair<PADButton, std::string_view>, PAD_AXIS_COUNT> skAxisNames = {{
    {PAD_AXIS_LEFT_X_POS, "Left X+"sv},
    {PAD_AXIS_LEFT_X_NEG, "Left X-"sv},
    {PAD_AXIS_LEFT_Y_POS, "Left Y+"sv},
    {PAD_AXIS_LEFT_Y_NEG, "Left Y-"sv},
    {PAD_AXIS_RIGHT_X_POS, "Right X+"sv},
    {PAD_AXIS_RIGHT_X_NEG, "Right X-"sv},
    {PAD_AXIS_RIGHT_Y_POS, "Right Y+"sv},
    {PAD_AXIS_RIGHT_Y_NEG, "Right Y-"sv},
    {PAD_AXIS_TRIGGER_L, "Trigger L"sv},
    {PAD_AXIS_TRIGGER_R, "Trigger R"sv},
}};

static constexpr std::array<std::pair<PADButton, std::string_view>, PAD_AXIS_COUNT> skAxisDirLabels = {{
    {PAD_AXIS_LEFT_X_POS, "Right"sv},
    {PAD_AXIS_LEFT_X_NEG, "Left"sv},
    {PAD_AXIS_LEFT_Y_POS, "Up"sv},
    {PAD_AXIS_LEFT_Y_NEG, "Down"sv},
    {PAD_AXIS_RIGHT_X_POS, "Right"sv},
    {PAD_AXIS_RIGHT_X_NEG, "Left"sv},
    {PAD_AXIS_RIGHT_Y_POS, "Up"sv},
    {PAD_AXIS_RIGHT_Y_NEG, "Down"sv},
    {PAD_AXIS_TRIGGER_L, "N/A"sv},
    {PAD_AXIS_TRIGGER_R, "N/A"sv},
}};

const char* PADGetButtonName(const PADButton button) {

  if (const auto iter =
          std::ranges::find_if(skButtonNames, [&button](const auto& pair) { return button == pair.first; });
      iter != skButtonNames.end()) {
    return iter->second.data();
  }

  return nullptr;
}

const char* PADGetNativeButtonName(u32 button) {
  return aurora_gamepad_button_string(static_cast<AuroraGamepadButton>(button));
}

const char* PADGetAxisName(const PADAxis axis) {
  if (const auto it = std::ranges::find_if(skAxisNames, [&axis](const auto& pair) { return axis == pair.first; });
      it != skAxisNames.end()) {
    return it->second.data();
  }

  return nullptr;
}

const char* PADGetAxisDirectionLabel(const PADAxis axis) {
  if (const auto it = std::ranges::find_if(skAxisDirLabels, [&axis](const auto& pair) { return axis == pair.first; });
      it != skAxisDirLabels.end()) {
    return it->second.data();
  }

  return nullptr;
}

const char* PADGetNativeAxisName(PADSignedNativeAxis axis) {
  return aurora_gamepad_axis_string(static_cast<AuroraGamepadAxis>(axis.nativeAxis));
}

int32_t PADGetNativeButtonPressed(const u32 port) {
  const auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return -1;
  }

  for (int32_t i = 0; i < AURORA_GAMEPAD_BUTTON_COUNT; ++i) {
    if (aurora_gamepad_button(controller->m_controller, static_cast<AuroraGamepadButton>(i)) != 0u) {
      return i;
    }
  }
  return -1;
}

PADSignedNativeAxis PADGetNativeAxisPulled(const u32 port) {
  const auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return {-1, AXIS_SIGN_POSITIVE};
  }

  for (int32_t i = 0; i < AURORA_GAMEPAD_AXIS_COUNT; ++i) {
    const auto axisVal = aurora_gamepad_axis(controller->m_controller, static_cast<AuroraGamepadAxis>(i));
    if (axisVal >= 16384) {
      return {i, AXIS_SIGN_POSITIVE};
    }

    if (axisVal <= -16384) {
      // SDL3 triggers rest at -32768, so skip their negative direction.
      if (i == AURORA_GAMEPAD_AXIS_LEFT_TRIGGER || i == AURORA_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        continue;
      }
      return {i, AXIS_SIGN_NEGATIVE};
    }
  }
  return {-1, AXIS_SIGN_POSITIVE};
}

void PADRestoreDefaultMapping(const u32 port) {
  auto* controller = aurora::input::get_controller_for_player(port);
  if (controller == nullptr) {
    return;
  }
  __PADSetDefaultMapping(controller);
  controller->m_axisMapping = g_defaultAxes;
}

void PADBlockInput(const bool block) {
  if (g_blockPAD.exchange(block, std::memory_order_acq_rel) && !block) {
    g_suppressHeldOnRead = true;
  }
}


AuroraGamepad* PADGetGamepadForIndex(const u32 index) {
  const auto* ctrl = __PADGetControllerForIndex(index);
  if (ctrl == nullptr) {
    return nullptr;
  }

  return ctrl->m_controller;
}

void PADSetDefaultMapping(const PADDefaultMapping* mapping, const PADControllerType type) {
  if (g_initialized) {
    aurora::input::Log.fatal("PADSetDefaultMapping called after PADInit()!");
  }

  switch (type) {
  case PAD_TYPE_STANDARD:
    g_defaultButtonsStandard = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_XBOX360:
    g_defaultButtonsXBox360 = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_XBOXONE:
    g_defaultButtonsXBoxOne = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_PS3:
    g_defaultButtonsPS3 = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_PS4:
    g_defaultButtonsPS4 = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_PS5:
    g_defaultButtonsPS5 = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_SWITCH_PROCON:
    g_defaultButtonsProCon = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_JOYCON_LEFT:
    g_defaultButtonsJoyConLeft = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_JOYCON_RIGHT:
    g_defaultButtonsJoyConRight = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_JOYCON_PAIR:
    g_defaultButtonsJoyPair = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_GAMECUBE:
    g_defaultButtonsGamecube = toStdArray(mapping->buttons);
    break;
  case PAD_TYPE_NSO_GAMECUBE:
    g_defaultButtonsNSOGamecube = toStdArray(mapping->buttons);
    break;
  default:
    break;
  }
  g_defaultAxes = toStdArray(mapping->axes);
}

BOOL PADSetColor(const u32 port, const u8 red, const u8 green, const u8 blue) {
  const auto ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  ctrl->m_ledRed = red;
  ctrl->m_ledGreen = green;
  ctrl->m_ledBlue = blue;
  ctrl->m_isColorDirty = true;
  return true;
}

BOOL PADGetColor(const u32 port, u8* red, u8* green, u8* blue) {
  const auto ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  *red = ctrl->m_ledRed;
  *green = ctrl->m_ledGreen;
  *blue = ctrl->m_ledBlue;
  return TRUE;
}

BOOL PADSetSensorEnabled(const u32 port, const PADSensorType sensor, const BOOL enabled) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);

  if (ctrl == nullptr) {
    return FALSE;
  }

  return aurora_gamepad_set_sensor_enabled(ctrl->m_controller, static_cast<AuroraSensorType>(sensor), enabled ? true : false)
             ? TRUE
             : FALSE;
}

BOOL PADHasSensor(const u32 port, const PADSensorType sensor) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  return aurora_gamepad_has_sensor(ctrl->m_controller, static_cast<AuroraSensorType>(sensor)) ? TRUE : FALSE;
}

BOOL PADGetSensorData(const u32 port, const PADSensorType sensor, f32* data, const int nValues) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  return aurora_gamepad_sensor_data(ctrl->m_controller, static_cast<AuroraSensorType>(sensor), data, nValues);
}

BOOL PADSetRumbleIntensity(const u32 port, const u16 low, const u16 high) {
  auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr || ctrl->m_isGameCube || !ctrl->m_hasRumble) {
    return FALSE;
  }
  ctrl->m_rumbleIntensityLow = low;
  ctrl->m_rumbleIntensityHigh = high;

  return TRUE;
}

BOOL PADGetRumbleIntensity(const u32 port, u16* low, u16* high) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr || ctrl->m_isGameCube || !ctrl->m_hasRumble) {
    *low = 0;
    *high = 0;
    return FALSE;
  }

  *low = ctrl->m_rumbleIntensityLow;
  *high = ctrl->m_rumbleIntensityHigh;
  return TRUE;
}

BOOL PADSupportsRumbleIntensity(const u32 port) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }

  return !ctrl->m_isGameCube && ctrl->m_hasRumble;
}

BOOL PADIsGCAdapter(const u32 port) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return FALSE;
  }
  return ctrl->m_isGameCube;
}

PADBatteryState PADGetBatteryState(const u32 port, f32* perc) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return PAD_BATTERYSTATE_ERROR;
  }

  int tmp = 0;
  const auto ret = aurora_gamepad_power(ctrl->m_controller, &tmp);
  if (tmp != -1) {
    *perc = static_cast<float>(tmp) / 100.f;
  } else {
    *perc = static_cast<float>(tmp);
  }
  return static_cast<PADBatteryState>(ret);
}

PADControllerType PADGetControllerType(const u32 port) {
  const auto* ctrl = aurora::input::get_controller_for_player(port);
  if (ctrl == nullptr) {
    return PAD_TYPE_UNKNOWN;
  }

  auto type = aurora_gamepad_type(ctrl->m_controller);
  return static_cast<PADControllerType>(type);
}

PADControllerType PADGetControllerTypeForIndex(const u32 index) {
  const auto* ctrl = __PADGetControllerForIndex(index);
  if (ctrl == nullptr) {
    return PAD_TYPE_UNKNOWN;
  }

  auto type = aurora_gamepad_type(ctrl->m_controller);
  if (type == AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && ctrl->m_pid == 0x2073) {
    return PAD_TYPE_NSO_GAMECUBE;
  }
  return static_cast<PADControllerType>(type);
}
