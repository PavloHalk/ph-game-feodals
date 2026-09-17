// Binary save format (.feo) and the saves/ folder next to the executable.
//
// Layout, little endian:
//   char[4]  magic "FEOD"
//   uint16   format version (kSaveFormatVersion)
//   uint32   board width
//   uint32   board height
//   uint8    player count
//   per player: uint8 name length, UTF-8 name bytes, uint8 R, G, B
//   uint8    index of the player whose turn it is
//   uint64   number of claimed cells
//   per claimed cell: uint32 x, uint32 y, uint8 owner
#pragma once

#include <windows.h>

#include "ByteBuffer.h"
#include "GameState.h"

const uint16_t kSaveFormatVersion = 1;

enum SaveResult {
  kSaveOk = 0,
  kSaveErrOpen,
  kSaveErrWrite,
  kSaveErrRead,
  kSaveErrFormat,
  kSaveErrVersion,
  kSaveErrMemory,
};

SaveResult SaveGame(const wchar_t* path, const GameState& game);

// The same format in memory (used for network snapshots too).
bool SerializeGame(const GameState& game, ByteBuffer* out);
SaveResult DeserializeGame(const uint8_t* data, size_t size, GameState* game);

// Name (uint8 length + UTF-8) followed by R, G, B.
void WritePlayerInfo(ByteBuffer* out, const PlayerInfo& player);
bool ReadPlayerInfo(ByteReader* in, PlayerInfo* player);

// Loads into `game` only if the whole file is valid; otherwise `game` is
// left untouched.
SaveResult LoadGame(const wchar_t* path, GameState* game);

// User-facing description of an error code.
const wchar_t* SaveResultText(SaveResult result);

// "<exe folder>\saves", optionally creating it.
bool GetSavesDir(wchar_t* buffer, bool create);

// Standard file dialogs starting in saves\. `path` is MAX_PATH wide and may
// hold the current file name as the suggestion.
bool PromptSavePath(HWND owner, wchar_t* path);
bool PromptLoadPath(HWND owner, wchar_t* path);
