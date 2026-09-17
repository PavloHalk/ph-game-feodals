#include "UiCommon.h"

const COLORREF kDefaultPalette[kMaxPlayers] = {
    RGB(220, 40, 40),   RGB(40, 90, 220),  RGB(30, 160, 60),
    RGB(245, 140, 0),   RGB(140, 60, 190), RGB(0, 170, 180),
    RGB(230, 70, 160),  RGB(120, 80, 40),
};

const wchar_t* const kPaletteNames[kMaxPlayers] = {
    L"Червоний",   L"Синій",     L"Зелений",  L"Помаранчевий",
    L"Фіолетовий", L"Бірюзовий", L"Малиновий", L"Коричневий",
};

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
  wchar_t digits[32];
  int n = 0;
  do {
    digits[n++] = (wchar_t)(L'0' + value % 10);
    value /= 10;
  } while (value);
  int pos = 0;
  for (int i = n - 1; i >= 0; --i) {
    out[pos++] = digits[i];
    if (i && i % 3 == 0) out[pos++] = L'\x00A0';
  }
  out[pos] = 0;
}

void FormatPercent(uint64_t part, uint64_t total, wchar_t* out) {
  uint64_t tenths = total ? (part * 1000 + total / 2) / total : 0;
  FormatCount(tenths / 10, out);
  int len = lstrlenW(out);
  out[len] = L',';
  out[len + 1] = (wchar_t)(L'0' + tenths % 10);
  out[len + 2] = L'%';
  out[len + 3] = 0;
}

const wchar_t* CellsWord(uint64_t count) {
  uint64_t lastTwo = count % 100, last = count % 10;
  if (lastTwo >= 11 && lastTwo <= 14) return L"клітинок";
  if (last == 1) return L"клітинка";
  if (last >= 2 && last <= 4) return L"клітинки";
  return L"клітинок";
}

void DefaultPlayerName(int index, wchar_t* out) {
  wsprintfW(out, L"Гравець %d", index + 1);
}
