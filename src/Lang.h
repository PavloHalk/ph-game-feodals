// Interface language. Every text the player sees comes from Tr(), in Ukrainian
// or English; the language can be switched at any moment from the «Мова»
// menu, and the choice is kept in feodals.ini next to the executable.
// Game data - player names, saves - does not depend on the language.
#pragma once

#include <windows.h>

enum Language { kLangUkrainian, kLangEnglish, kLangCount };

enum StringId {
#define STR(id, uk, en) id,
#include "Strings.inc"
#undef STR
  kStringCount
};

Language CurrentLanguage();

// Only switches the texts; the caller refreshes whatever is on screen.
void SetLanguage(Language language);

// Text `id` in the current language, or in `language`.
const wchar_t* Tr(StringId id);
const wchar_t* Tr(StringId id, Language language);

// The language chosen last time (Ukrainian when nothing was saved yet), and
// saving a new choice. Saving is best effort: a read-only folder just means
// the choice is not remembered.
Language LoadLanguageSetting();
void SaveLanguageSetting(Language language);
