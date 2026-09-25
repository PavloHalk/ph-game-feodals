#include "Lang.h"

namespace {

const wchar_t* const kTexts[kStringCount][kLangCount] = {
#define STR(id, uk, en) {uk, en},
#include "Strings.inc"
#undef STR
};

// Codes written to feodals.ini, indexed by Language.
const wchar_t* const kLanguageCodes[kLangCount] = {L"uk", L"en"};

const wchar_t kIniSection[] = L"Feodals";
const wchar_t kIniKey[] = L"Language";

Language g_language = kLangUkrainian;

// feodals.ini next to the executable (where saves\ lives too).
bool GetIniPath(wchar_t* path) {
  DWORD len = GetModuleFileNameW(0, path, MAX_PATH);
  if (!len || len >= MAX_PATH) return false;
  while (len && path[len - 1] != L'\\' && path[len - 1] != L'/') --len;
  if (len + 12 >= MAX_PATH) return false;
  lstrcpyW(path + len, L"feodals.ini");
  return true;
}

}  // namespace

Language CurrentLanguage() { return g_language; }

void SetLanguage(Language language) {
  if (language >= 0 && language < kLangCount) g_language = language;
}

const wchar_t* Tr(StringId id) { return Tr(id, g_language); }

const wchar_t* Tr(StringId id, Language language) {
  if (id < 0 || id >= kStringCount) return L"";
  if (language < 0 || language >= kLangCount) language = kLangUkrainian;
  return kTexts[id][language];
}

Language LoadLanguageSetting() {
  wchar_t path[MAX_PATH], code[8];
  if (!GetIniPath(path)) return kLangUkrainian;
  GetPrivateProfileStringW(kIniSection, kIniKey, L"", code, 8, path);
  for (int i = 0; i < kLangCount; ++i) {
    if (lstrcmpiW(code, kLanguageCodes[i]) == 0) return (Language)i;
  }
  return kLangUkrainian;
}

void SaveLanguageSetting(Language language) {
  wchar_t path[MAX_PATH];
  if (language < 0 || language >= kLangCount || !GetIniPath(path)) return;
  WritePrivateProfileStringW(kIniSection, kIniKey, kLanguageCodes[language],
                             path);
}
