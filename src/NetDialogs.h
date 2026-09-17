// Dialogs of the network game: connecting to a host and choosing a player.
#pragma once

#include <windows.h>

#include "GameState.h"

struct ConnectSettings {
  wchar_t address[64];
  uint16_t port;
};

// "Приєднатися до гри": host address and port.
bool ShowConnectDialog(HWND owner, ConnectSettings* settings);

struct PlayerSetupRequest {
  const wchar_t* title;
  const wchar_t* okText;
  const wchar_t* info;  // optional line at the top

  bool askPort;  // host of a saved game
  uint16_t port;

  // Seat selection (saved or running game): colors are fixed, the player
  // picks one of the available seats. Otherwise name + free color choice.
  bool chooseSlot;
  int numSlots;
  PlayerInfo slots[kMaxPlayers];
  bool available[kMaxPlayers];

  COLORREF taken[kMaxPlayers];  // colors of other players (color mode)
  int numTaken;

  // In/out.
  PlayerInfo player;
  int slot;
};

bool ShowPlayerSetupDialog(HWND owner, PlayerSetupRequest* request);
