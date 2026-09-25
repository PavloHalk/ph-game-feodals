// Small shared UI helpers: fonts, number formatting, palette.
#pragma once

#include <windows.h>

#include "GameState.h"
#include "Lang.h"

extern const COLORREF kDefaultPalette[kMaxPlayers];

// Name of palette color `index` ("Червоний", "Red", ...).
const wchar_t* PaletteName(int index);

// System message font (Tahoma on XP, Segoe UI later). Caller owns the font.
HFONT CreateUiFont(bool bold, int sizeDelta = 0);

// "1 234 567" (Ukrainian) or "1,234,567" (English) into `out` (at least 32
// chars).
void FormatCount(uint64_t value, wchar_t* out);

// "12,3%" or "12.3%" of part/total into `out` (at least 16 chars).
void FormatPercent(uint64_t part, uint64_t total, wchar_t* out);

// The word "cells" in the form that goes with the number (Ukrainian has
// three: 1 клітинка, 2 клітинки, 5 клітинок).
const wchar_t* CellsWord(uint64_t count);

// Default "Гравець N" / "Player N" name (N is 1-based).
void DefaultPlayerName(int index, wchar_t* out,
                       Language language = CurrentLanguage());

// Default "Комп'ютер N" / "Computer N" name of a computer player.
void DefaultBotName(int index, wchar_t* out,
                    Language language = CurrentLanguage());

// A player's name as shown on screen: a default name ("Гравець 2",
// "Computer 3", ...) in the current language, whichever language made it;
// any other name as it is. The game itself keeps the name unchanged, so a
// save or a network game shows each viewer the defaults in their language.
// Uses `buffer` (kMaxNameLen) when it has to translate.
const wchar_t* DisplayName(const wchar_t* name, wchar_t* buffer);

// "Людина", "Комп'ютер: слабкий", ... for a BotLevel.
const wchar_t* BotLevelName(int level);
// "", "слабкий", ... for a BotLevel (empty for a person).
const wchar_t* BotLevelShortName(int level);
