// Aurora's controllers, independent of the host input layer.
//
// Desktop implements these on SDL3 (lib/gamepad_sdl.cpp), Switch on libnx HID
// (lib/gamepad_switch.cpp), so nothing here needs SDL. The enum values are
// SDL3's, which is what saved controller mappings store.
#ifndef AURORA_GAMEPAD_H
#define AURORA_GAMEPAD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  AURORA_GAMEPAD_BUTTON_INVALID = -1,
  AURORA_GAMEPAD_BUTTON_SOUTH,
  AURORA_GAMEPAD_BUTTON_EAST,
  AURORA_GAMEPAD_BUTTON_WEST,
  AURORA_GAMEPAD_BUTTON_NORTH,
  AURORA_GAMEPAD_BUTTON_BACK,
  AURORA_GAMEPAD_BUTTON_GUIDE,
  AURORA_GAMEPAD_BUTTON_START,
  AURORA_GAMEPAD_BUTTON_LEFT_STICK,
  AURORA_GAMEPAD_BUTTON_RIGHT_STICK,
  AURORA_GAMEPAD_BUTTON_LEFT_SHOULDER,
  AURORA_GAMEPAD_BUTTON_RIGHT_SHOULDER,
  AURORA_GAMEPAD_BUTTON_DPAD_UP,
  AURORA_GAMEPAD_BUTTON_DPAD_DOWN,
  AURORA_GAMEPAD_BUTTON_DPAD_LEFT,
  AURORA_GAMEPAD_BUTTON_DPAD_RIGHT,
  AURORA_GAMEPAD_BUTTON_MISC1,
  AURORA_GAMEPAD_BUTTON_RIGHT_PADDLE1,
  AURORA_GAMEPAD_BUTTON_LEFT_PADDLE1,
  AURORA_GAMEPAD_BUTTON_RIGHT_PADDLE2,
  AURORA_GAMEPAD_BUTTON_LEFT_PADDLE2,
  AURORA_GAMEPAD_BUTTON_TOUCHPAD,
  AURORA_GAMEPAD_BUTTON_MISC2,
  AURORA_GAMEPAD_BUTTON_MISC3,
  AURORA_GAMEPAD_BUTTON_MISC4,
  AURORA_GAMEPAD_BUTTON_MISC5,
  AURORA_GAMEPAD_BUTTON_MISC6,
  AURORA_GAMEPAD_BUTTON_COUNT
} AuroraGamepadButton;

typedef enum {
  AURORA_GAMEPAD_AXIS_INVALID = -1,
  AURORA_GAMEPAD_AXIS_LEFTX,
  AURORA_GAMEPAD_AXIS_LEFTY,
  AURORA_GAMEPAD_AXIS_RIGHTX,
  AURORA_GAMEPAD_AXIS_RIGHTY,
  AURORA_GAMEPAD_AXIS_LEFT_TRIGGER,
  AURORA_GAMEPAD_AXIS_RIGHT_TRIGGER,
  AURORA_GAMEPAD_AXIS_COUNT
} AuroraGamepadAxis;

typedef enum {
  AURORA_GAMEPAD_TYPE_UNKNOWN = 0,
  AURORA_GAMEPAD_TYPE_STANDARD,
  AURORA_GAMEPAD_TYPE_XBOX360,
  AURORA_GAMEPAD_TYPE_XBOXONE,
  AURORA_GAMEPAD_TYPE_PS3,
  AURORA_GAMEPAD_TYPE_PS4,
  AURORA_GAMEPAD_TYPE_PS5,
  AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO,
  AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT,
  AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT,
  AURORA_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR,
  AURORA_GAMEPAD_TYPE_GAMECUBE,
  AURORA_GAMEPAD_TYPE_COUNT
} AuroraGamepadType;

typedef enum {
  AURORA_SENSOR_INVALID = -1,
  AURORA_SENSOR_UNKNOWN,
  AURORA_SENSOR_ACCEL,
  AURORA_SENSOR_GYRO,
  AURORA_SENSOR_ACCEL_L,
  AURORA_SENSOR_GYRO_L,
  AURORA_SENSOR_ACCEL_R,
  AURORA_SENSOR_GYRO_R,
} AuroraSensorType;

typedef enum {
  AURORA_POWERSTATE_ERROR = -1,
  AURORA_POWERSTATE_UNKNOWN,
  AURORA_POWERSTATE_ON_BATTERY,
  AURORA_POWERSTATE_NO_BATTERY,
  AURORA_POWERSTATE_CHARGING,
  AURORA_POWERSTATE_CHARGED,
} AuroraPowerState;

#define AURORA_JOYSTICK_AXIS_MAX 32767
#define AURORA_JOYSTICK_AXIS_MIN (-32768)

// A connected controller. SDL_Gamepad on desktop, an npad slot on Switch.
typedef struct AuroraGamepad AuroraGamepad;
typedef uint32_t AuroraControllerID;

// Keyboard scancodes (USB HID usage IDs, as SDL3 numbers them) that bindings
// store. Only the ones the runtime names are listed.
enum {
  AURORA_SCANCODE_A = 4,
  AURORA_SCANCODE_D = 7,
  AURORA_SCANCODE_E = 8,
  AURORA_SCANCODE_I = 12,
  AURORA_SCANCODE_J = 13,
  AURORA_SCANCODE_K = 14,
  AURORA_SCANCODE_L = 15,
  AURORA_SCANCODE_Q = 20,
  AURORA_SCANCODE_S = 22,
  AURORA_SCANCODE_W = 26,
  AURORA_SCANCODE_RETURN = 40,
  AURORA_SCANCODE_ESCAPE = 41,
  AURORA_SCANCODE_BACKSPACE = 42,
  AURORA_SCANCODE_SPACE = 44,
  AURORA_SCANCODE_BACKSLASH = 49,
  AURORA_SCANCODE_F10 = 67,
  AURORA_SCANCODE_DELETE = 76,
  AURORA_SCANCODE_RIGHT = 79,
  AURORA_SCANCODE_LEFT = 80,
  AURORA_SCANCODE_DOWN = 81,
  AURORA_SCANCODE_UP = 82,
  AURORA_SCANCODE_LSHIFT = 225,
  AURORA_SCANCODE_COUNT = 512,
};

// The controller assigned to a player (0-3), or null.
AuroraGamepad* aurora_gamepad_for_player(int player);
// The open controller with this id, or null.
AuroraGamepad* aurora_gamepad_from_id(AuroraControllerID id);
AuroraControllerID aurora_gamepad_id(AuroraGamepad* pad);
bool aurora_gamepad_button(AuroraGamepad* pad, AuroraGamepadButton button);
int16_t aurora_gamepad_axis(AuroraGamepad* pad, AuroraGamepadAxis axis);
const char* aurora_gamepad_name(AuroraGamepad* pad);
AuroraGamepadType aurora_gamepad_type(AuroraGamepad* pad);
int aurora_gamepad_player_index(AuroraGamepad* pad);
void aurora_gamepad_set_player_index(AuroraGamepad* pad, int index);
bool aurora_gamepad_set_led(AuroraGamepad* pad, uint8_t r, uint8_t g, uint8_t b);
bool aurora_gamepad_has_sensor(AuroraGamepad* pad, AuroraSensorType sensor);
bool aurora_gamepad_set_sensor_enabled(AuroraGamepad* pad, AuroraSensorType sensor, bool enabled);
bool aurora_gamepad_sensor_enabled(AuroraGamepad* pad, AuroraSensorType sensor);
bool aurora_gamepad_sensor_data(AuroraGamepad* pad, AuroraSensorType sensor, float* data, int count);
AuroraPowerState aurora_gamepad_power(AuroraGamepad* pad, int* percent);
// The controller's raw buttons, for pads whose gamepad mapping loses some.
int aurora_gamepad_raw_button_count(AuroraGamepad* pad);
bool aurora_gamepad_raw_button(AuroraGamepad* pad, int button);
const char* aurora_gamepad_button_string(AuroraGamepadButton button);
const char* aurora_gamepad_axis_string(AuroraGamepadAxis axis);

// Keyboard and mouse, which Switch does not have: no keys, no buttons, and the
// app always has focus.
const bool* aurora_keyboard_state(int* count);
bool aurora_keyboard_focused(void);
uint32_t aurora_mouse_buttons(float* x, float* y);
const char* aurora_scancode_name(int scancode);
void aurora_set_cursor_visible(bool visible);

#ifdef __cplusplus
}
#endif

#endif
