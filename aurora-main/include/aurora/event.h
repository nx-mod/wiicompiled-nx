#ifndef AURORA_EVENT_H
#define AURORA_EVENT_H

#include "aurora.h"

#include "gamepad.h"

// Switch has no SDL: its events carry no SDL_Event.
#if !defined(__SWITCH__)
#include <SDL3/SDL_events.h>
#endif

#ifdef __cplusplus
#include <cstdint>

extern "C" {
#else
#include "stdint.h"
#endif

typedef enum {
  AURORA_NONE,
  AURORA_EXIT,
  AURORA_SDL_EVENT,
  AURORA_WINDOW_MOVED,
  AURORA_WINDOW_RESIZED,
  AURORA_CONTROLLER_ADDED,
  AURORA_CONTROLLER_REMOVED,
  AURORA_PAUSED,
  AURORA_UNPAUSED,
  AURORA_DISPLAY_SCALE_CHANGED,
} AuroraEventType;

struct AuroraEvent {
  AuroraEventType type;
  union {
#if !defined(__SWITCH__)
    SDL_Event sdl;
#endif
    AuroraWindowPos windowPos;
    AuroraWindowSize windowSize;
    AuroraControllerID controller;
  };
};

#ifdef __cplusplus
}
#endif

#endif
