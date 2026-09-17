#include "DialogKit.h"

#include <commdlg.h>

#include "Renderer.h"
#include "UiCommon.h"

namespace {

enum { IDM_OTHER_COLOR = 100 };

COLORREF g_customColors[16];

}  // namespace

INT_PTR RunDialog(HWND owner, DLGPROC proc, LPARAM param) {
  // DLGTEMPLATE header followed by empty menu, class and title (zeros).
  WORD tmpl[16];
  memset(tmpl, 0, sizeof(tmpl));
  DLGTEMPLATE* t = (DLGTEMPLATE*)tmpl;
  t->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
  t->cx = 200;
  t->cy = 150;
  return DialogBoxIndirectParamW(GetModuleHandleW(0), (LPCDLGTEMPLATE)tmpl,
                                 owner, proc, param);
}

HWND AddControl(HWND dlg, HFONT font, const wchar_t* cls, const wchar_t* text,
                DWORD style, DWORD exStyle, int x, int y, int w, int h,
                int id) {
  HWND ctl = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, dlg, (HMENU)(INT_PTR)id,
                             GetModuleHandleW(0), 0);
  SendMessageW(ctl, WM_SETFONT, (WPARAM)font, FALSE);
  return ctl;
}

void SizeDialog(HWND dlg, int clientWidth, int clientHeight, bool center) {
  RECT rc = {0, 0, clientWidth, clientHeight};
  AdjustWindowRectEx(&rc, (DWORD)GetWindowLongW(dlg, GWL_STYLE), FALSE,
                     (DWORD)GetWindowLongW(dlg, GWL_EXSTYLE));
  int w = rc.right - rc.left, h = rc.bottom - rc.top;
  if (center) {
    RECT area;
    HWND owner = GetWindow(dlg, GW_OWNER);
    if (!owner || !GetWindowRect(owner, &area)) {
      SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    }
    int x = (area.left + area.right - w) / 2;
    int y = (area.top + area.bottom - h) / 2;
    SetWindowPos(dlg, 0, x, y < 0 ? 0 : y, w, h, SWP_NOZORDER);
  } else {
    SetWindowPos(dlg, 0, 0, 0, w, h, SWP_NOZORDER | SWP_NOMOVE);
  }
  InvalidateRect(dlg, 0, TRUE);
}

bool PickColor(HWND dlg, HWND anchor, uint32_t* color, const COLORREF* taken,
               int numTaken) {
  HMENU menu = CreatePopupMenu();
  for (int k = 0; k < kMaxPlayers; ++k) {
    UINT flags = MF_OWNERDRAW;
    for (int t = 0; t < numTaken; ++t) {
      if (taken[t] == kDefaultPalette[k]) flags |= MF_GRAYED;
    }
    if (*color == kDefaultPalette[k]) flags |= MF_CHECKED;
    AppendMenuW(menu, flags, 1 + k, (LPCWSTR)(ULONG_PTR)k);
  }
  AppendMenuW(menu, MF_SEPARATOR, 0, 0);
  AppendMenuW(menu, MF_STRING, IDM_OTHER_COLOR, L"Інший колір…");

  RECT rc;
  GetWindowRect(anchor, &rc);
  int cmd = (int)TrackPopupMenu(
      menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON,
      rc.left, rc.bottom, 0, dlg, 0);
  DestroyMenu(menu);

  if (cmd >= 1 && cmd <= kMaxPlayers) {
    *color = kDefaultPalette[cmd - 1];
    return true;
  }
  if (cmd != IDM_OTHER_COLOR) return false;

  if (!g_customColors[0] && !g_customColors[1]) {
    for (int k = 0; k < 16; ++k) {
      g_customColors[k] =
          k < kMaxPlayers ? kDefaultPalette[k] : RGB(255, 255, 255);
    }
  }
  CHOOSECOLORW cc;
  memset(&cc, 0, sizeof(cc));
  cc.lStructSize = sizeof(cc);
  cc.hwndOwner = dlg;
  cc.rgbResult = *color;
  cc.lpCustColors = g_customColors;
  cc.Flags = CC_RGBINIT | CC_FULLOPEN;
  if (!ChooseColorW(&cc)) return false;
  *color = cc.rgbResult;
  return true;
}

bool MeasureColorMenuItem(MEASUREITEMSTRUCT* mis) {
  if (mis->CtlType != ODT_MENU) return false;
  mis->itemWidth = 170;
  mis->itemHeight = 24;
  return true;
}

bool DrawColorMenuItem(DRAWITEMSTRUCT* dis, HFONT font) {
  if (dis->CtlType != ODT_MENU) return false;
  int k = (int)dis->itemData;
  if (k < 0 || k >= kMaxPlayers) return false;
  bool selected = (dis->itemState & ODS_SELECTED) != 0;
  bool grayed = (dis->itemState & ODS_GRAYED) != 0;
  bool checked = (dis->itemState & ODS_CHECKED) != 0;
  HDC dc = dis->hDC;
  RECT rc = dis->rcItem;

  FillRect(dc, &rc,
           GetSysColorBrush(selected && !grayed ? COLOR_HIGHLIGHT : COLOR_MENU));

  RECT swatch = {rc.left + 8, rc.top + 4, rc.left + 40, rc.bottom - 4};
  HBRUSH brush = CreateSolidBrush(kDefaultPalette[k]);
  FillRect(dc, &swatch, brush);
  DeleteObject(brush);
  HBRUSH frame = (HBRUSH)GetStockObject(checked ? BLACK_BRUSH : GRAY_BRUSH);
  FrameRect(dc, &swatch, frame);
  if (checked) {
    InflateRect(&swatch, -1, -1);
    FrameRect(dc, &swatch, frame);
  }

  wchar_t text[64];
  lstrcpyW(text, kPaletteNames[k]);
  if (grayed) lstrcatW(text, L" (зайнятий)");
  HGDIOBJ oldFont = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, GetSysColor(grayed     ? COLOR_GRAYTEXT
                               : selected ? COLOR_HIGHLIGHTTEXT
                                          : COLOR_MENUTEXT));
  RECT textRect = {rc.left + 50, rc.top, rc.right - 8, rc.bottom};
  DrawTextW(dc, text, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  SelectObject(dc, oldFont);
  return true;
}

void DrawColorButton(DRAWITEMSTRUCT* dis, COLORREF color) {
  RECT rc = dis->rcItem;
  UINT state = DFCS_BUTTONPUSH;
  if (dis->itemState & ODS_SELECTED) state |= DFCS_PUSHED;
  if (dis->itemState & ODS_DISABLED) state |= DFCS_INACTIVE;
  DrawFrameControl(dis->hDC, &rc, DFC_BUTTON, state);

  RECT swatch = {rc.left + 5, rc.top + 5, rc.right - 5, rc.bottom - 5};
  if (dis->itemState & ODS_SELECTED) OffsetRect(&swatch, 1, 1);
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(dis->hDC, &swatch, brush);
  DeleteObject(brush);
  FrameRect(dis->hDC, &swatch, (HBRUSH)GetStockObject(GRAY_BRUSH));
  if (dis->itemState & ODS_FOCUS) {
    RECT focus = {rc.left + 3, rc.top + 3, rc.right - 3, rc.bottom - 3};
    DrawFocusRect(dis->hDC, &focus);
  }
}

void DrawSwatchListItem(DRAWITEMSTRUCT* dis, COLORREF color,
                        const wchar_t* text, HFONT font) {
  HDC dc = dis->hDC;
  RECT rc = dis->rcItem;
  bool selected = (dis->itemState & ODS_SELECTED) != 0;
  FillRect(dc, &rc, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));

  RECT swatch = {rc.left + 6, rc.top + 4, rc.left + 34, rc.bottom - 4};
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(dc, &swatch, brush);
  DeleteObject(brush);
  FrameRect(dc, &swatch, (HBRUSH)GetStockObject(GRAY_BRUSH));

  HGDIOBJ oldFont = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
  RECT textRect = {rc.left + 44, rc.top, rc.right - 4, rc.bottom};
  DrawTextW(dc, text, -1, &textRect,
            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
  SelectObject(dc, oldFont);
  if (dis->itemState & ODS_FOCUS) DrawFocusRect(dc, &rc);
}

bool CheckColorUsable(HWND dlg, COLORREF color, const wchar_t* title) {
  if (color != kEmptyCellColor) return true;
  MessageBoxW(dlg,
              L"Колір збігається з кольором порожніх клітинок.\n"
              L"Оберіть інший колір.",
              title, MB_OK | MB_ICONWARNING);
  return false;
}
