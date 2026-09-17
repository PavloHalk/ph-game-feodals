// "Нова гра" dialog: board size, player count, player names and colors.
#pragma once

#include <windows.h>

#include "GameState.h"

struct NewGameSettings {
  uint32_t width, height;
  int numPlayers;
  uint16_t port;  // network game only
  // All 8 slots are kept so names/colors survive changing the player count.
  PlayerInfo players[kMaxPlayers];
};

void DefaultNewGameSettings(NewGameSettings* settings);

// Modal dialog. On OK writes the validated settings back and returns true.
// With `network` it creates a hosted game: asks for the port and only for the
// host's own name and color (other players choose theirs when joining).
bool ShowNewGameDialog(HWND owner, NewGameSettings* settings, bool network);
