// aurora/gamepad.h on libnx HID: Switch controllers as Aurora sees them.
//
// Buttons follow SDL's positional layout, which saved mappings assume: SOUTH is
// the bottom face button (B on a Switch pad), EAST is A, WEST Y, NORTH X.
// Stick Y is inverted to SDL's down-positive. ZL/ZR are digital, reported as
// fully released or fully pressed triggers.
#include "gamepad_switch.hpp"

#include <switch.h>

namespace aurora::input::switch_pad {
namespace {
Slot g_slots[kSlotCount];
} // namespace

Slot* slot(uint32_t index) noexcept { return index < kSlotCount ? &g_slots[index] : nullptr; }

void initialize() noexcept {
  padConfigureInput(kSlotCount, HidNpadStyleSet_NpadStandard);
  for (uint32_t i = 0; i < kSlotCount; ++i) {
    g_slots[i].index = i;
    g_slots[i].player = static_cast<int>(i);
    if (i == 0) {
      // Player one is also the handheld pair.
      padInitialize(&g_slots[i].pad, HidNpadIdType_No1, HidNpadIdType_Handheld);
    } else {
      padInitialize(&g_slots[i].pad, static_cast<HidNpadIdType>(HidNpadIdType_No1 + i));
    }
  }
}

void update() noexcept {
  for (auto& s : g_slots) {
    padUpdate(&s.pad);
    s.buttons = padGetButtons(&s.pad);
    s.left = padGetStickPos(&s.pad, 0);
    s.right = padGetStickPos(&s.pad, 1);
  }
}

bool connected(const Slot& s) noexcept { return padIsConnected(&s.pad); }
} // namespace aurora::input::switch_pad

using aurora::input::switch_pad::Slot;

namespace {
Slot* to_slot(AuroraGamepad* pad) { return reinterpret_cast<Slot*>(pad); }

uint64_t button_mask(AuroraGamepadButton button) {
  switch (button) {
  case AURORA_GAMEPAD_BUTTON_SOUTH: return HidNpadButton_B;
  case AURORA_GAMEPAD_BUTTON_EAST: return HidNpadButton_A;
  case AURORA_GAMEPAD_BUTTON_WEST: return HidNpadButton_Y;
  case AURORA_GAMEPAD_BUTTON_NORTH: return HidNpadButton_X;
  case AURORA_GAMEPAD_BUTTON_BACK: return HidNpadButton_Minus;
  case AURORA_GAMEPAD_BUTTON_START: return HidNpadButton_Plus;
  case AURORA_GAMEPAD_BUTTON_LEFT_STICK: return HidNpadButton_StickL;
  case AURORA_GAMEPAD_BUTTON_RIGHT_STICK: return HidNpadButton_StickR;
  case AURORA_GAMEPAD_BUTTON_LEFT_SHOULDER: return HidNpadButton_L;
  case AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER: return HidNpadButton_R;
  case AURORA_GAMEPAD_BUTTON_DPAD_UP: return HidNpadButton_Up;
  case AURORA_GAMEPAD_BUTTON_DPAD_DOWN: return HidNpadButton_Down;
  case AURORA_GAMEPAD_BUTTON_DPAD_LEFT: return HidNpadButton_Left;
  case AURORA_GAMEPAD_BUTTON_DPAD_RIGHT: return HidNpadButton_Right;
  default: return 0; // Home and Capture are the system's, not the game's
  }
}

int16_t clamp_axis(int32_t v) {
  return static_cast<int16_t>(v > AURORA_JOYSTICK_AXIS_MAX ? AURORA_JOYSTICK_AXIS_MAX
                              : v < AURORA_JOYSTICK_AXIS_MIN ? AURORA_JOYSTICK_AXIS_MIN : v);
}
} // namespace

extern "C" {

AuroraGamepad* aurora_gamepad_for_player(int player) {
  for (uint32_t i = 0; i < aurora::input::switch_pad::kSlotCount; ++i) {
    Slot* s = aurora::input::switch_pad::slot(i);
    if (s->player == player && aurora::input::switch_pad::connected(*s)) {
      return reinterpret_cast<AuroraGamepad*>(s);
    }
  }
  return nullptr;
}
AuroraControllerID aurora_gamepad_id(AuroraGamepad* pad) { return pad ? to_slot(pad)->index + 1 : 0; }
AuroraGamepad* aurora_gamepad_from_id(AuroraControllerID id) {
  Slot* s = id > 0 ? aurora::input::switch_pad::slot(id - 1) : nullptr;
  return s != nullptr && aurora::input::switch_pad::connected(*s) ? reinterpret_cast<AuroraGamepad*>(s) : nullptr;
}
bool aurora_gamepad_sensor_enabled(AuroraGamepad*, AuroraSensorType) { return false; }
bool aurora_gamepad_button(AuroraGamepad* pad, AuroraGamepadButton button) {
  const uint64_t mask = button_mask(button);
  return pad != nullptr && mask != 0 && (to_slot(pad)->buttons & mask) != 0;
}

int16_t aurora_gamepad_axis(AuroraGamepad* pad, AuroraGamepadAxis axis) {
  if (pad == nullptr) return 0;
  const Slot& s = *to_slot(pad);
  switch (axis) {
  case AURORA_GAMEPAD_AXIS_LEFTX: return clamp_axis(s.left.x);
  case AURORA_GAMEPAD_AXIS_LEFTY: return clamp_axis(-s.left.y);
  case AURORA_GAMEPAD_AXIS_RIGHTX: return clamp_axis(s.right.x);
  case AURORA_GAMEPAD_AXIS_RIGHTY: return clamp_axis(-s.right.y);
  case AURORA_GAMEPAD_AXIS_LEFT_TRIGGER: return (s.buttons & HidNpadButton_ZL) ? AURORA_JOYSTICK_AXIS_MAX : 0;
  case AURORA_GAMEPAD_AXIS_RIGHT_TRIGGER: return (s.buttons & HidNpadButton_ZR) ? AURORA_JOYSTICK_AXIS_MAX : 0;
  default: return 0;
  }
}

AuroraGamepadType aurora_gamepad_type(AuroraGamepad* pad) {
  if (pad == nullptr) return AURORA_GAMEPAD_TYPE_UNKNOWN;
  const uint32_t style = padGetStyleSet(&to_slot(pad)->pad);
  if (style & HidNpadStyleTag_NpadFullKey) return AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;
  if (style & (HidNpadStyleTag_NpadHandheld | HidNpadStyleTag_NpadJoyDual)) {
    return AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
  }
  if (style & HidNpadStyleTag_NpadJoyLeft) return AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT;
  if (style & HidNpadStyleTag_NpadJoyRight) return AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT;
  return AURORA_GAMEPAD_TYPE_STANDARD;
}

const char* aurora_gamepad_name(AuroraGamepad* pad) {
  switch (aurora_gamepad_type(pad)) {
  case AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: return "Pro Controller";
  case AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return "Joy-Con (L/R)";
  case AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: return "Joy-Con (L)";
  case AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return "Joy-Con (R)";
  default: return "Controller";
  }
}

int aurora_gamepad_player_index(AuroraGamepad* pad) { return pad ? to_slot(pad)->player : -1; }
void aurora_gamepad_set_player_index(AuroraGamepad* pad, int index) {
  if (pad) to_slot(pad)->player = index;
}

// Not wired yet: LEDs, motion and battery. Each reports "not supported".
bool aurora_gamepad_set_led(AuroraGamepad*, uint8_t, uint8_t, uint8_t) { return false; }
bool aurora_gamepad_has_sensor(AuroraGamepad*, AuroraSensorType) { return false; }
bool aurora_gamepad_set_sensor_enabled(AuroraGamepad*, AuroraSensorType, bool) { return false; }
bool aurora_gamepad_sensor_data(AuroraGamepad*, AuroraSensorType, float*, int) { return false; }
AuroraPowerState aurora_gamepad_power(AuroraGamepad*, int* percent) {
  if (percent) *percent = -1;
  return AURORA_POWERSTATE_UNKNOWN;
}

int aurora_gamepad_raw_button_count(AuroraGamepad*) { return 0; }
bool aurora_gamepad_raw_button(AuroraGamepad*, int) { return false; }

const char* aurora_gamepad_button_string(AuroraGamepadButton button) {
  static const char* const kNames[AURORA_GAMEPAD_BUTTON_COUNT] = {
      "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder",
      "rightshoulder", "dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2",
      "paddle3", "paddle4", "touchpad", "misc2", "misc3", "misc4", "misc5", "misc6"};
  return button >= 0 && button < AURORA_GAMEPAD_BUTTON_COUNT ? kNames[button] : nullptr;
}

const char* aurora_gamepad_axis_string(AuroraGamepadAxis axis) {
  static const char* const kNames[AURORA_GAMEPAD_AXIS_COUNT] = {"leftx", "lefty", "rightx", "righty",
                                                                "lefttrigger", "righttrigger"};
  return axis >= 0 && axis < AURORA_GAMEPAD_AXIS_COUNT ? kNames[axis] : nullptr;
}

const bool* aurora_keyboard_state(int* count) {
  if (count) *count = 0;
  return nullptr;
}
bool aurora_keyboard_focused(void) { return true; }
uint32_t aurora_mouse_buttons(float* x, float* y) {
  if (x) *x = 0.f;
  if (y) *y = 0.f;
  return 0;
}
const char* aurora_scancode_name(int) { return "Unknown"; }
void aurora_set_cursor_visible(bool) {}

} // extern "C"
