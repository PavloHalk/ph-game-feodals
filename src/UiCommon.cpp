#include "UiCommon.h"

const COLORREF kDefaultPalette[kMaxPlayers] = {
    RGB(220, 40, 40),   RGB(40, 90, 220),  RGB(30, 160, 60),
    RGB(245, 140, 0),   RGB(140, 60, 190), RGB(0, 170, 180),
    RGB(230, 70, 160),  RGB(120, 80, 40),
};

namespace {

const StringId kPaletteNames[kMaxPlayers] = {
    kStrColorRed,    kStrColorBlue,      kStrColorGreen, kStrColorOrange,
    kStrColorPurple, kStrColorTurquoise, kStrColorPink,  kStrColorBrown,
};

const StringId kBotLevelNames[kBotLevelCount] = {
    kStrLevelHuman, kStrLevelWeak, kStrLevelMedium, kStrLevelStrong,
    kStrLevelVeryStrong,
};

const StringId kBotLevelShortNames[kBotLevelCount] = {
    kStrLevelHuman,  // not shown: a person has no level
    kStrShortWeak, kStrShortMedium, kStrShortStrong, kStrShortVeryStrong,
};

}  // namespace

const wchar_t* PaletteName(int index) {
  return index >= 0 && index < kMaxPlayers ? Tr(kPaletteNames[index]) : L"";
}

HFONT CreateUiFont(bool bold, int sizeDelta) {
  NONCLIENTMETRICSW metrics;
  memset(&metrics, 0, sizeof(metrics));
  // The pre-Vista structure size works everywhere (XP rejects the larger one).
  metrics.cbSize = (UINT)((char*)&metrics.lfMessageFont -
                          (char*)&metrics + sizeof(metrics.lfMessageFont));
  LOGFONTW font;
  if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics,
                            0)) {
    font = metrics.lfMessageFont;
  } else {
    GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(font), &font);
  }
  if (bold) font.lfWeight = FW_BOLD;
  if (sizeDelta) {
    font.lfHeight += font.lfHeight < 0 ? -sizeDelta : sizeDelta;
  }
  return CreateFontIndirectW(&font);
}

void FormatCount(uint64_t value, wchar_t* out) {
  // Groups of thousands: a non-breaking space in Ukrainian, a comma in English.
  wchar_t separator = CurrentLanguage() == kLangEnglish ? L',' : L'\x00A0';
  wchar_t digits[32];
  int n = 0;
  do {
    digits[n++] = (wchar_t)(L'0' + value % 10);
    value /= 10;
  } while (value);
  int pos = 0;
  for (int i = n - 1; i >= 0; --i) {
    out[pos++] = digits[i];
    if (i && i % 3 == 0) out[pos++] = separator;
  }
  out[pos] = 0;
}

void FormatPercent(uint64_t part, uint64_t total, wchar_t* out) {
  uint64_t tenths = total ? (part * 1000 + total / 2) / total : 0;
  FormatCount(tenths / 10, out);
  int len = lstrlenW(out);
  out[len] = CurrentLanguage() == kLangEnglish ? L'.' : L',';
  out[len + 1] = (wchar_t)(L'0' + tenths % 10);
  out[len + 2] = L'%';
  out[len + 3] = 0;
}

const wchar_t* CellsWord(uint64_t count) {
  if (CurrentLanguage() == kLangEnglish) {
    return Tr(count == 1 ? kStrCellsOne : kStrCellsMany);
  }
  uint64_t lastTwo = count % 100, last = count % 10;
  if (lastTwo >= 11 && lastTwo <= 14) return Tr(kStrCellsMany);
  if (last == 1) return Tr(kStrCellsOne);
  if (last >= 2 && last <= 4) return Tr(kStrCellsFew);
  return Tr(kStrCellsMany);
}

void DefaultPlayerName(int index, wchar_t* out, Language language) {
  wsprintfW(out, Tr(kStrDefaultPlayer, language), index + 1);
}

void DefaultBotName(int index, wchar_t* out, Language language) {
  wsprintfW(out, Tr(kStrDefaultBot, language), index + 1);
}

const wchar_t* DisplayName(const wchar_t* name, wchar_t* buffer) {
  wchar_t candidate[kMaxNameLen];
  for (int k = 0; k < kMaxPlayers; ++k) {
    for (int l = 0; l < kLangCount; ++l) {
      DefaultPlayerName(k, candidate, (Language)l);
      if (lstrcmpW(name, candidate) == 0) {
        DefaultPlayerName(k, buffer);
        return buffer;
      }
      DefaultBotName(k, candidate, (Language)l);
      if (lstrcmpW(name, candidate) == 0) {
        DefaultBotName(k, buffer);
        return buffer;
      }
    }
  }
  return name;
}

const wchar_t* BotLevelName(int level) {
  return level >= 0 && level < kBotLevelCount ? Tr(kBotLevelNames[level]) : L"";
}

const wchar_t* BotLevelShortName(int level) {
  return level > kBotHuman && level < kBotLevelCount
             ? Tr(kBotLevelShortNames[level])
             : L"";
}
