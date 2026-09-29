// aurora/gamepad.h on SDL3: an AuroraGamepad is an SDL_Gamepad.
#include <aurora/gamepad.h>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

namespace {
SDL_Gamepad* sdl(AuroraGamepad* pad) { return reinterpret_cast<SDL_Gamepad*>(pad); }
} // namespace

extern "C" {

AuroraGamepad* aurora_gamepad_for_player(int player) {
  return reinterpret_cast<AuroraGamepad*>(SDL_GetGamepadFromPlayerIndex(player));
}
AuroraControllerID aurora_gamepad_id(AuroraGamepad* pad) { return SDL_GetGamepadID(sdl(pad)); }
AuroraGamepad* aurora_gamepad_from_id(AuroraControllerID id) {
  return reinterpret_cast<AuroraGamepad*>(SDL_GetGamepadFromID(id));
}
bool aurora_gamepad_sensor_enabled(AuroraGamepad* pad, AuroraSensorType sensor) {
  return SDL_GamepadSensorEnabled(sdl(pad), static_cast<SDL_SensorType>(sensor));
}
bool aurora_gamepad_button(AuroraGamepad* pad, AuroraGamepadButton button) {
  return SDL_GetGamepadButton(sdl(pad), static_cast<SDL_GamepadButton>(button));
}
int16_t aurora_gamepad_axis(AuroraGamepad* pad, AuroraGamepadAxis axis) {
  return SDL_GetGamepadAxis(sdl(pad), static_cast<SDL_GamepadAxis>(axis));
}
const char* aurora_gamepad_name(AuroraGamepad* pad) { return SDL_GetGamepadName(sdl(pad)); }
AuroraGamepadType aurora_gamepad_type(AuroraGamepad* pad) {
  return static_cast<AuroraGamepadType>(SDL_GetGamepadType(sdl(pad)));
}
int aurora_gamepad_player_index(AuroraGamepad* pad) { return SDL_GetGamepadPlayerIndex(sdl(pad)); }
void aurora_gamepad_set_player_index(AuroraGamepad* pad, int index) {
  SDL_SetGamepadPlayerIndex(sdl(pad), index);
}
bool aurora_gamepad_set_led(AuroraGamepad* pad, uint8_t r, uint8_t g, uint8_t b) {
  return SDL_SetGamepadLED(sdl(pad), r, g, b);
}
bool aurora_gamepad_has_sensor(AuroraGamepad* pad, AuroraSensorType sensor) {
  return SDL_GamepadHasSensor(sdl(pad), static_cast<SDL_SensorType>(sensor));
}
bool aurora_gamepad_set_sensor_enabled(AuroraGamepad* pad, AuroraSensorType sensor, bool enabled) {
  return SDL_SetGamepadSensorEnabled(sdl(pad), static_cast<SDL_SensorType>(sensor), enabled);
}
bool aurora_gamepad_sensor_data(AuroraGamepad* pad, AuroraSensorType sensor, float* data, int count) {
  return SDL_GetGamepadSensorData(sdl(pad), static_cast<SDL_SensorType>(sensor), data, count);
}
AuroraPowerState aurora_gamepad_power(AuroraGamepad* pad, int* percent) {
  return static_cast<AuroraPowerState>(SDL_GetGamepadPowerInfo(sdl(pad), percent));
}
int aurora_gamepad_raw_button_count(AuroraGamepad* pad) {
  return SDL_GetNumJoystickButtons(SDL_GetGamepadJoystick(sdl(pad)));
}
bool aurora_gamepad_raw_button(AuroraGamepad* pad, int button) {
  return SDL_GetJoystickButton(SDL_GetGamepadJoystick(sdl(pad)), button);
}
const char* aurora_gamepad_button_string(AuroraGamepadButton button) {
  return SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(button));
}
const char* aurora_gamepad_axis_string(AuroraGamepadAxis axis) {
  return SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(axis));
}
const bool* aurora_keyboard_state(int* count) { return SDL_GetKeyboardState(count); }
bool aurora_keyboard_focused(void) { return SDL_GetKeyboardFocus() != nullptr; }
uint32_t aurora_mouse_buttons(float* x, float* y) { return SDL_GetMouseState(x, y); }
const char* aurora_scancode_name(int scancode) {
  return scancode >= 0 && scancode < SDL_SCANCODE_COUNT ? SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode))
                                                       : "Unknown";
}
void aurora_set_cursor_visible(bool visible) {
  if (visible) {
    SDL_ShowCursor();
  } else {
    SDL_HideCursor();
  }
}

} // extern "C"
