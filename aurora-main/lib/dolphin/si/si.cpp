#include <dolphin/si.h>
#include "../../input.hpp"


uint32_t SIProbe(int32_t chan) {
  auto* const controller = aurora::input::get_controller_for_player(chan);
  if (controller == nullptr) {
    return SI_ERROR_NO_RESPONSE;
  }

  if (controller->m_isGameCube) {
    // A WaveBird reports no battery level; a wired pad does.
    if (aurora_gamepad_power(controller->m_controller, nullptr) == AURORA_POWERSTATE_UNKNOWN) {
      return SI_GC_WAVEBIRD;
    }
  }

  return SI_GC_CONTROLLER;
}
