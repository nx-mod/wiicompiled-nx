// Switch controller slots behind aurora/gamepad.h: one per npad, player 1 also
// taking the handheld pair.
#pragma once

#include <aurora/gamepad.h>
#include <switch.h>

namespace aurora::input::switch_pad {
constexpr uint32_t kSlotCount = 4;

struct Slot {
  PadState pad{};
  uint32_t index = 0;
  int player = -1;
  uint64_t buttons = 0;
  HidAnalogStickState left{}, right{};
};

void initialize() noexcept;
// Reads every npad once; call before reading buttons for a frame.
void update() noexcept;
bool connected(const Slot& s) noexcept;
Slot* slot(uint32_t index) noexcept;
} // namespace aurora::input::switch_pad
