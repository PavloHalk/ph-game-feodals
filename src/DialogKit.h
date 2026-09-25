// Building blocks shared by the dialogs: in-memory dialog templates (no .rc
// needed), control creation, the color picker and owner-drawn swatches.
#pragma once

#include <windows.h>

#include "GameState.h"

// Runs a modal dialog with an empty template; `proc` creates the controls in
// WM_INITDIALOG (lParam = `param`).
INT_PTR RunDialog(HWND owner, DLGPROC proc, LPARAM param);

HWND AddControl(HWND dlg, HFONT font, const wchar_t* cls, const wchar_t* text,
                DWORD style, DWORD exStyle, int x, int y, int w, int h, int id);

// Sets the client size; optionally centers the dialog over its owner.
void SizeDialog(HWND dlg, int clientWidth, int clientHeight, bool center);

// Popup with the 8 palette colors (colors listed in `taken` are disabled)
// and "Other colour…". Returns true and updates `color` if one was chosen.
// The dialog must forward WM_MEASUREITEM/WM_DRAWITEM to the handlers below.
bool PickColor(HWND dlg, HWND anchor, uint32_t* color, const COLORREF* taken,
               int numTaken);
bool MeasureColorMenuItem(MEASUREITEMSTRUCT* mis);
bool DrawColorMenuItem(DRAWITEMSTRUCT* dis, HFONT font);

// Owner-drawn push button showing a color swatch.
void DrawColorButton(DRAWITEMSTRUCT* dis, COLORREF color);

// Owner-drawn list box item: swatch plus text.
void DrawSwatchListItem(DRAWITEMSTRUCT* dis, COLORREF color,
                        const wchar_t* text, HFONT font);

// Shows a warning and returns false when the color equals the empty cells.
bool CheckColorUsable(HWND dlg, COLORREF color, const wchar_t* title);
