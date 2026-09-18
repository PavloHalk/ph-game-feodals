// Small shared UI helpers: fonts, number formatting, palette.
#pragma once

#include <windows.h>

#include "GameState.h"

extern const COLORREF kDefaultPalette[kMaxPlayers];
extern const wchar_t* const kPaletteNames[kMaxPlayers];

// System message font (Tahoma on XP, Segoe UI later). Caller owns the font.
HFONT CreateUiFont(bool bold, int sizeDelta = 0);

// "1 234 567" into `out` (at least 32 chars).
void FormatCount(uint64_t value, wchar_t* out);

// "12,3%" of part/total into `out` (at least 16 chars).
void FormatPercent(uint64_t part, uint64_t total, wchar_t* out);

// Ukrainian plural of "клітинка" for a number.
const wchar_t* CellsWord(uint64_t count);

// Default "Гравець N" name (N is 1-based).
void DefaultPlayerName(int index, wchar_t* out);

// Default "Комп'ютер N" name of a computer player (N is 1-based).
void DefaultBotName(int index, wchar_t* out);

// "Людина", "Комп'ютер: слабкий", ... indexed by BotLevel.
extern const wchar_t* const kBotLevelNames[kBotLevelCount];
// "", "слабкий", ... indexed by BotLevel.
extern const wchar_t* const kBotLevelShortNames[kBotLevelCount];
