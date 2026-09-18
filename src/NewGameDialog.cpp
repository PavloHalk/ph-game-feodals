#include "NewGameDialog.h"

#include "DialogKit.h"
#include "NetGame.h"
#include "Renderer.h"
#include "UiCommon.h"

namespace {

enum {
  IDC_PRESET = 1000,
  IDC_WIDTH,
  IDC_HEIGHT,
  IDC_COUNT,
  IDC_PORT,
  IDC_LABEL0 = 1100,
  IDC_NAME0 = 1200,
  IDC_COLOR0 = 1300,
  IDC_KIND0 = 1400,  // human or computer level, per player
};

struct Preset {
  uint32_t width, height;
  const wchar_t* label;
};

const Preset kPresets[] = {
    {10, 10, L"10 × 10"},     {20, 20, L"20 × 20"},
    {30, 30, L"30 × 30"},     {50, 50, L"50 × 50"},
    {100, 100, L"100 × 100"}, {1000, 1000, L"1000 × 1000"},
    {0, 0, L"Своє"},
};
const int kNumPresets = sizeof(kPresets) / sizeof(kPresets[0]);
const int kCustomPreset = kNumPresets - 1;

const uint64_t kLargeBoardCells = 25000000;  // warn above ~5000 x 5000

// Layout in pixels.
const int kMargin = 12;
const int kLabelWidth = 130;
const int kFieldX = kMargin + kLabelWidth;
const int kClientWidth = 480;
const int kSeparatorTop = 114;
const int kPlayersTop = 124;
const int kPortRowHeight = 34;  // extra space for the port row (network)
const int kRowHeight = 30;
const int kButtonHeight = 28;

struct DialogData {
  NewGameSettings* result;
  NewGameSettings work;
  HFONT font;
  bool updatingSize;
  bool network;  // host dialog: port and only the host's own player row
};

int PlayersTop(const DialogData* data) {
  return kPlayersTop + (data->network ? kPortRowHeight : 0);
}

// Player rows editable in the dialog.
int VisibleRows(const DialogData* data) {
  return data->network ? 1 : data->work.numPlayers;
}

DialogData* GetData(HWND dlg) {
  return (DialogData*)GetWindowLongPtrW(dlg, DWLP_USER);
}

void ReadNames(HWND dlg, DialogData* data) {
  for (int i = 0; i < kMaxPlayers; ++i) {
    GetDlgItemTextW(dlg, IDC_NAME0 + i, data->work.players[i].name,
                    kMaxNameLen);
    int level = (int)SendDlgItemMessageW(dlg, IDC_KIND0 + i, CB_GETCURSEL, 0, 0);
    if (level < 0 || level >= kBotLevelCount || data->network) level = kBotHuman;
    data->work.players[i].botLevel = (uint8_t)level;
  }
}

// Switching between a person and the computer also switches an untouched
// default name ("Гравець N" <-> "Комп'ютер N").
void OnKindChanged(HWND dlg, int player) {
  wchar_t name[kMaxNameLen], human[kMaxNameLen], bot[kMaxNameLen];
  GetDlgItemTextW(dlg, IDC_NAME0 + player, name, kMaxNameLen);
  DefaultPlayerName(player, human);
  DefaultBotName(player, bot);
  if (name[0] && lstrcmpW(name, human) != 0 && lstrcmpW(name, bot) != 0) {
    return;
  }
  int level =
      (int)SendDlgItemMessageW(dlg, IDC_KIND0 + player, CB_GETCURSEL, 0, 0);
  SetDlgItemTextW(dlg, IDC_NAME0 + player, level > kBotHuman ? bot : human);
}

// Gives visible players that share a color the first free palette color.
void FixDuplicateColors(DialogData* data) {
  for (int i = 1; i < data->work.numPlayers; ++i) {
    bool duplicate = false;
    for (int j = 0; j < i; ++j) {
      if (data->work.players[j].color == data->work.players[i].color) {
        duplicate = true;
      }
    }
    if (!duplicate) continue;
    for (int k = 0; k < kMaxPlayers; ++k) {
      bool taken = false;
      for (int j = 0; j < data->work.numPlayers; ++j) {
        if (j != i && data->work.players[j].color == kDefaultPalette[k]) {
          taken = true;
        }
      }
      if (!taken) {
        data->work.players[i].color = kDefaultPalette[k];
        break;
      }
    }
  }
}

void SelectMatchingPreset(HWND dlg) {
  BOOL okW, okH;
  UINT w = GetDlgItemInt(dlg, IDC_WIDTH, &okW, FALSE);
  UINT h = GetDlgItemInt(dlg, IDC_HEIGHT, &okH, FALSE);
  int sel = kCustomPreset;
  for (int i = 0; i < kCustomPreset; ++i) {
    if (okW && okH && kPresets[i].width == w && kPresets[i].height == h) {
      sel = i;
    }
  }
  SendDlgItemMessageW(dlg, IDC_PRESET, CB_SETCURSEL, sel, 0);
}

void Layout(HWND dlg, DialogData* data, bool center) {
  int n = VisibleRows(data);
  for (int i = 0; i < kMaxPlayers; ++i) {
    int show = i < n ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(dlg, IDC_LABEL0 + i), show);
    ShowWindow(GetDlgItem(dlg, IDC_NAME0 + i), show);
    ShowWindow(GetDlgItem(dlg, IDC_KIND0 + i), data->network ? SW_HIDE : show);
    ShowWindow(GetDlgItem(dlg, IDC_COLOR0 + i), show);
  }
  int buttonsY = PlayersTop(data) + n * kRowHeight + 10;
  int cancelX = kClientWidth - kMargin - 100;
  MoveWindow(GetDlgItem(dlg, IDCANCEL), cancelX, buttonsY, 100, kButtonHeight,
             TRUE);
  MoveWindow(GetDlgItem(dlg, IDOK), cancelX - 8 - 110, buttonsY, 110,
             kButtonHeight, TRUE);
  SizeDialog(dlg, kClientWidth, buttonsY + kButtonHeight + kMargin, center);
}

void CreateControls(HWND dlg, DialogData* data) {
  HFONT f = data->font;
  const DWORD label = SS_LEFT;
  const DWORD tab = WS_TABSTOP;

  AddControl(dlg, f, L"STATIC", L"Розмір поля:", label, 0, kMargin, 15,
             kLabelWidth, 20, -1);
  HWND preset = AddControl(dlg, f, L"COMBOBOX", L"",
                           CBS_DROPDOWNLIST | WS_VSCROLL | tab, 0, kFieldX, 12,
                           150, 240, IDC_PRESET);
  for (int i = 0; i < kNumPresets; ++i) {
    SendMessageW(preset, CB_ADDSTRING, 0, (LPARAM)kPresets[i].label);
  }

  AddControl(dlg, f, L"STATIC", L"Ширина × висота:", label, 0, kMargin, 49,
             kLabelWidth, 20, -1);
  HWND width = AddControl(dlg, f, L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | tab,
                          WS_EX_CLIENTEDGE, kFieldX, 46, 80, 23, IDC_WIDTH);
  AddControl(dlg, f, L"STATIC", L"×", SS_CENTER, 0, kFieldX + 80, 49, 20, 20,
             -1);
  HWND height = AddControl(dlg, f, L"EDIT", L"",
                           ES_NUMBER | ES_AUTOHSCROLL | tab, WS_EX_CLIENTEDGE,
                           kFieldX + 100, 46, 80, 23, IDC_HEIGHT);
  SendMessageW(width, EM_LIMITTEXT, 7, 0);
  SendMessageW(height, EM_LIMITTEXT, 7, 0);

  AddControl(dlg, f, L"STATIC", L"Кількість гравців:", label, 0, kMargin, 84,
             kLabelWidth, 20, -1);
  HWND count = AddControl(dlg, f, L"COMBOBOX", L"",
                          CBS_DROPDOWNLIST | WS_VSCROLL | tab, 0, kFieldX, 81,
                          80, 240, IDC_COUNT);
  for (int i = kMinPlayers; i <= kMaxPlayers; ++i) {
    wchar_t text[8];
    wsprintfW(text, L"%d", i);
    SendMessageW(count, CB_ADDSTRING, 0, (LPARAM)text);
  }

  int separatorTop = kSeparatorTop;
  if (data->network) {
    AddControl(dlg, f, L"STATIC", L"Порт:", label, 0, kMargin, 118,
               kLabelWidth, 20, -1);
    HWND port = AddControl(dlg, f, L"EDIT", L"", ES_NUMBER | tab,
                           WS_EX_CLIENTEDGE, kFieldX, 115, 80, 23, IDC_PORT);
    SendMessageW(port, EM_LIMITTEXT, 5, 0);
    SetDlgItemInt(dlg, IDC_PORT, data->work.port, FALSE);
    separatorTop += kPortRowHeight;
  }
  AddControl(dlg, f, L"STATIC", L"", SS_ETCHEDHORZ, 0, kMargin, separatorTop,
             kClientWidth - 2 * kMargin, 2, -1);

  for (int i = 0; i < kMaxPlayers; ++i) {
    int y = PlayersTop(data) + i * kRowHeight;
    wchar_t number[8];
    wsprintfW(number, L"%d.", i + 1);
    AddControl(dlg, f, L"STATIC", data->network ? L"Ви:" : number, SS_RIGHT,
               0, kMargin, y + 5, 26, 20, IDC_LABEL0 + i);
    HWND name = AddControl(dlg, f, L"EDIT", data->work.players[i].name,
                           ES_AUTOHSCROLL | tab, WS_EX_CLIENTEDGE,
                           kMargin + 30, y + 2, 160, 23, IDC_NAME0 + i);
    SendMessageW(name, EM_LIMITTEXT, kMaxNameLen - 1, 0);
    HWND kind = AddControl(dlg, f, L"COMBOBOX", L"",
                           CBS_DROPDOWNLIST | WS_VSCROLL | tab, 0,
                           kMargin + 196, y + 1, 176, 240, IDC_KIND0 + i);
    for (int k = 0; k < kBotLevelCount; ++k) {
      SendMessageW(kind, CB_ADDSTRING, 0, (LPARAM)kBotLevelNames[k]);
    }
    int level = data->work.players[i].botLevel;
    SendMessageW(kind, CB_SETCURSEL,
                 level < kBotLevelCount && !data->network ? level : 0, 0);
    AddControl(dlg, f, L"BUTTON", L"", BS_OWNERDRAW | tab, 0,
               kClientWidth - kMargin - 76, y + 1, 76, 25, IDC_COLOR0 + i);
  }

  AddControl(dlg, f, L"BUTTON", data->network ? L"Створити" : L"Почати гру",
             BS_DEFPUSHBUTTON | tab, 0, 0, 0, 110, kButtonHeight, IDOK);
  AddControl(dlg, f, L"BUTTON", L"Скасувати", BS_PUSHBUTTON | tab, 0, 0, 0,
             100, kButtonHeight, IDCANCEL);

  data->updatingSize = true;
  SetDlgItemInt(dlg, IDC_WIDTH, data->work.width, FALSE);
  SetDlgItemInt(dlg, IDC_HEIGHT, data->work.height, FALSE);
  data->updatingSize = false;
  SelectMatchingPreset(dlg);
  SendMessageW(count, CB_SETCURSEL, data->work.numPlayers - kMinPlayers, 0);
}

void OnPickColor(HWND dlg, DialogData* data, int player) {
  COLORREF taken[kMaxPlayers];
  int numTaken = 0;
  for (int j = 0; j < VisibleRows(data); ++j) {
    if (j != player) taken[numTaken++] = data->work.players[j].color;
  }
  PickColor(dlg, GetDlgItem(dlg, IDC_COLOR0 + player),
            &data->work.players[player].color, taken, numTaken);
  InvalidateRect(GetDlgItem(dlg, IDC_COLOR0 + player), 0, TRUE);
}

bool Validate(HWND dlg, DialogData* data) {
  BOOL okW, okH;
  UINT w = GetDlgItemInt(dlg, IDC_WIDTH, &okW, FALSE);
  UINT h = GetDlgItemInt(dlg, IDC_HEIGHT, &okH, FALSE);
  if (!okW || !okH || w < kMinBoardSize || h < kMinBoardSize ||
      w > kMaxBoardSize || h > kMaxBoardSize) {
    MessageBoxW(dlg,
                L"Ширина й висота поля мають бути від 10 до 1 000 000.",
                L"Нова гра", MB_OK | MB_ICONWARNING);
    SetFocus(GetDlgItem(dlg, okW && w >= kMinBoardSize && w <= kMaxBoardSize
                                 ? IDC_HEIGHT
                                 : IDC_WIDTH));
    return false;
  }

  BOOL okPort = TRUE;
  UINT port = data->network ? GetDlgItemInt(dlg, IDC_PORT, &okPort, FALSE) : 0;
  if (data->network && (!okPort || port < 1 || port > 65535)) {
    MessageBoxW(dlg, L"Порт має бути числом від 1 до 65535.", L"Мережева гра",
                MB_OK | MB_ICONWARNING);
    SetFocus(GetDlgItem(dlg, IDC_PORT));
    return false;
  }

  ReadNames(dlg, data);
  int n = VisibleRows(data);
  for (int i = 0; i < n; ++i) {
    if (!IsBotLevelAvailable(data->work.players[i].botLevel)) {
      MessageBoxW(dlg,
                  L"Рівні «сильний» і «дуже сильний» ще в розробці.\n"
                  L"Поки що можна грати проти слабкого або середнього "
                  L"комп'ютера.",
                  L"Нова гра", MB_OK | MB_ICONINFORMATION);
      SetFocus(GetDlgItem(dlg, IDC_KIND0 + i));
      return false;
    }
    if (!data->work.players[i].name[0]) {
      if (data->work.players[i].botLevel) {
        DefaultBotName(i, data->work.players[i].name);
      } else {
        DefaultPlayerName(i, data->work.players[i].name);
      }
    }
    for (int j = 0; j < i; ++j) {
      if (data->work.players[i].color == data->work.players[j].color) {
        wchar_t msg[160];
        wsprintfW(msg,
                  L"Гравці %d і %d мають однаковий колір.\n"
                  L"Оберіть кожному гравцю інший колір.",
                  j + 1, i + 1);
        MessageBoxW(dlg, msg, L"Нова гра", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(dlg, IDC_COLOR0 + i));
        return false;
      }
    }
    if (data->work.players[i].color == kEmptyCellColor) {
      wchar_t msg[160];
      wsprintfW(msg,
                L"Колір гравця %d збігається з кольором порожніх клітинок.\n"
                L"Оберіть інший колір.",
                i + 1);
      MessageBoxW(dlg, msg, L"Нова гра", MB_OK | MB_ICONWARNING);
      SetFocus(GetDlgItem(dlg, IDC_COLOR0 + i));
      return false;
    }
  }

  if ((uint64_t)w * h > kLargeBoardCells) {
    wchar_t cells[32], msg[400];
    FormatCount((uint64_t)w * h, cells);
    wsprintfW(msg,
              L"Поле %u × %u містить %s клітинок.\n\n"
              L"Порожні клітинки не займають пам'яті, але дуже велика кількість "
              L"ходів або захоплень може помітно навантажити пам'ять і "
              L"сповільнити гру.\n\nПродовжити?",
              w, h, cells);
    if (MessageBoxW(dlg, msg, L"Велике поле",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
      return false;
    }
  }

  data->work.width = w;
  data->work.height = h;
  if (data->network) data->work.port = (uint16_t)port;
  return true;
}

INT_PTR CALLBACK DialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  DialogData* data = GetData(dlg);
  switch (msg) {
    case WM_INITDIALOG: {
      data = (DialogData*)lParam;
      SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)data);
      SetWindowTextW(dlg,
                     data->network ? L"Створити мережеву гру" : L"Нова гра");
      SendMessageW(dlg, WM_SETFONT, (WPARAM)data->font, FALSE);
      CreateControls(dlg, data);
      Layout(dlg, data, true);
      SetFocus(GetDlgItem(dlg, IDC_PRESET));
      return FALSE;
    }

    case WM_COMMAND: {
      int id = LOWORD(wParam), code = HIWORD(wParam);
      if (id == IDC_PRESET && code == CBN_SELCHANGE) {
        int sel = (int)SendDlgItemMessageW(dlg, IDC_PRESET, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < kCustomPreset) {
          data->updatingSize = true;
          SetDlgItemInt(dlg, IDC_WIDTH, kPresets[sel].width, FALSE);
          SetDlgItemInt(dlg, IDC_HEIGHT, kPresets[sel].height, FALSE);
          data->updatingSize = false;
        } else {
          SetFocus(GetDlgItem(dlg, IDC_WIDTH));
          SendDlgItemMessageW(dlg, IDC_WIDTH, EM_SETSEL, 0, -1);
        }
      } else if ((id == IDC_WIDTH || id == IDC_HEIGHT) && code == EN_CHANGE) {
        if (!data->updatingSize) SelectMatchingPreset(dlg);
      } else if (id == IDC_COUNT && code == CBN_SELCHANGE) {
        ReadNames(dlg, data);
        int sel = (int)SendDlgItemMessageW(dlg, IDC_COUNT, CB_GETCURSEL, 0, 0);
        data->work.numPlayers = kMinPlayers + (sel < 0 ? 0 : sel);
        FixDuplicateColors(data);
        for (int i = 0; i < kMaxPlayers; ++i) {
          InvalidateRect(GetDlgItem(dlg, IDC_COLOR0 + i), 0, TRUE);
        }
        Layout(dlg, data, false);
      } else if (id >= IDC_KIND0 && id < IDC_KIND0 + kMaxPlayers &&
                 code == CBN_SELCHANGE) {
        OnKindChanged(dlg, id - IDC_KIND0);
      } else if (id >= IDC_COLOR0 && id < IDC_COLOR0 + kMaxPlayers &&
                 code == BN_CLICKED) {
        OnPickColor(dlg, data, id - IDC_COLOR0);
      } else if (id == IDOK) {
        if (Validate(dlg, data)) {
          *data->result = data->work;
          EndDialog(dlg, IDOK);
        }
      } else if (id == IDCANCEL) {
        EndDialog(dlg, IDCANCEL);
      }
      return TRUE;
    }

    case WM_MEASUREITEM:
      return MeasureColorMenuItem((MEASUREITEMSTRUCT*)lParam);

    case WM_DRAWITEM: {
      DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
      if (DrawColorMenuItem(dis, data->font)) return TRUE;
      if (dis->CtlID >= IDC_COLOR0 &&
          dis->CtlID < (UINT)(IDC_COLOR0 + kMaxPlayers)) {
        DrawColorButton(dis, data->work.players[dis->CtlID - IDC_COLOR0].color);
        return TRUE;
      }
      return FALSE;
    }
  }
  return FALSE;
}

}  // namespace

void DefaultNewGameSettings(NewGameSettings* settings) {
  memset(settings, 0, sizeof(*settings));
  settings->width = 30;
  settings->height = 30;
  settings->numPlayers = 2;
  settings->port = kDefaultNetPort;
  for (int i = 0; i < kMaxPlayers; ++i) {
    DefaultPlayerName(i, settings->players[i].name);
    settings->players[i].color = kDefaultPalette[i];
  }
}

bool ShowNewGameDialog(HWND owner, NewGameSettings* settings, bool network) {
  DialogData data;
  data.result = settings;
  data.work = *settings;
  data.font = CreateUiFont(false);
  data.updatingSize = false;
  data.network = network;
  if (!data.work.port) data.work.port = kDefaultNetPort;
  INT_PTR result = RunDialog(owner, DialogProc, (LPARAM)&data);
  DeleteObject(data.font);
  return result == IDOK;
}
