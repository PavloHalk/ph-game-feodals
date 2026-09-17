#include "NetDialogs.h"

#include "DialogKit.h"
#include "Renderer.h"
#include "UiCommon.h"

namespace {

enum {
  IDC_ADDRESS = 2000,
  IDC_PORT,
  IDC_NAME,
  IDC_COLOR,
  IDC_SLOTS,
};

const int kMargin = 12;
const int kLabelWidth = 110;
const int kFieldX = kMargin + kLabelWidth;
const int kClientWidth = 380;
const int kButtonHeight = 28;
const int kListItemHeight = 28;

void AddButtons(HWND dlg, HFONT font, const wchar_t* okText, int y) {
  int cancelX = kClientWidth - kMargin - 100;
  AddControl(dlg, font, L"BUTTON", okText, BS_DEFPUSHBUTTON | WS_TABSTOP, 0,
             cancelX - 8 - 120, y, 120, kButtonHeight, IDOK);
  AddControl(dlg, font, L"BUTTON", L"Скасувати", BS_PUSHBUTTON | WS_TABSTOP, 0,
             cancelX, y, 100, kButtonHeight, IDCANCEL);
}

bool ReadPort(HWND dlg, uint16_t* port) {
  BOOL ok;
  UINT value = GetDlgItemInt(dlg, IDC_PORT, &ok, FALSE);
  if (!ok || value < 1 || value > 65535) {
    MessageBoxW(dlg, L"Порт має бути числом від 1 до 65535.", L"Мережева гра",
                MB_OK | MB_ICONWARNING);
    SetFocus(GetDlgItem(dlg, IDC_PORT));
    return false;
  }
  *port = (uint16_t)value;
  return true;
}

// ---- Connect ------------------------------------------------------------------

struct ConnectData {
  ConnectSettings* settings;
  HFONT font;
};

INT_PTR CALLBACK ConnectProc(HWND dlg, UINT msg, WPARAM wParam,
                             LPARAM lParam) {
  ConnectData* data = (ConnectData*)GetWindowLongPtrW(dlg, DWLP_USER);
  switch (msg) {
    case WM_INITDIALOG: {
      data = (ConnectData*)lParam;
      SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)data);
      SetWindowTextW(dlg, L"Приєднатися до мережевої гри");
      HFONT f = data->font;
      AddControl(dlg, f, L"STATIC",
                 L"Адреса комп'ютера, що створив гру (наприклад, 192.168.1.10):",
                 SS_LEFT, 0, kMargin, 12, kClientWidth - 2 * kMargin, 20, -1);
      AddControl(dlg, f, L"STATIC", L"IP-адреса:", SS_LEFT, 0, kMargin, 44,
                 kLabelWidth, 20, -1);
      HWND address = AddControl(dlg, f, L"EDIT", data->settings->address,
                                ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
                                kFieldX, 41, 170, 23, IDC_ADDRESS);
      SendMessageW(address, EM_LIMITTEXT, 63, 0);
      AddControl(dlg, f, L"STATIC", L"Порт:", SS_LEFT, 0, kMargin, 78,
                 kLabelWidth, 20, -1);
      HWND port = AddControl(dlg, f, L"EDIT", L"", ES_NUMBER | WS_TABSTOP,
                             WS_EX_CLIENTEDGE, kFieldX, 75, 80, 23, IDC_PORT);
      SendMessageW(port, EM_LIMITTEXT, 5, 0);
      SetDlgItemInt(dlg, IDC_PORT, data->settings->port, FALSE);
      AddButtons(dlg, f, L"Приєднатися", 116);
      SizeDialog(dlg, kClientWidth, 116 + kButtonHeight + kMargin, true);
      SetFocus(address);
      SendMessageW(address, EM_SETSEL, 0, -1);
      return FALSE;
    }
    case WM_COMMAND:
      if (LOWORD(wParam) == IDOK) {
        wchar_t address[64];
        GetDlgItemTextW(dlg, IDC_ADDRESS, address, 64);
        if (!address[0]) {
          MessageBoxW(dlg, L"Введіть IP-адресу комп'ютера, що створив гру.",
                      L"Мережева гра", MB_OK | MB_ICONWARNING);
          SetFocus(GetDlgItem(dlg, IDC_ADDRESS));
          return TRUE;
        }
        uint16_t port;
        if (!ReadPort(dlg, &port)) return TRUE;
        lstrcpyW(data->settings->address, address);
        data->settings->port = port;
        EndDialog(dlg, IDOK);
      } else if (LOWORD(wParam) == IDCANCEL) {
        EndDialog(dlg, IDCANCEL);
      }
      return TRUE;
  }
  return FALSE;
}

// ---- Player setup ---------------------------------------------------------------

struct SetupData {
  PlayerSetupRequest* request;
  PlayerInfo player;
  HFONT font;
};

INT_PTR CALLBACK SetupProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  SetupData* data = (SetupData*)GetWindowLongPtrW(dlg, DWLP_USER);
  switch (msg) {
    case WM_INITDIALOG: {
      data = (SetupData*)lParam;
      SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)data);
      PlayerSetupRequest* req = data->request;
      SetWindowTextW(dlg, req->title);
      HFONT f = data->font;
      int y = 12;
      if (req->info && req->info[0]) {
        AddControl(dlg, f, L"STATIC", req->info, SS_LEFT | SS_NOPREFIX, 0,
                   kMargin, y, kClientWidth - 2 * kMargin, 36, -1);
        y += 44;
      }
      if (req->askPort) {
        AddControl(dlg, f, L"STATIC", L"Порт:", SS_LEFT, 0, kMargin, y + 3,
                   kLabelWidth, 20, -1);
        HWND port = AddControl(dlg, f, L"EDIT", L"", ES_NUMBER | WS_TABSTOP,
                               WS_EX_CLIENTEDGE, kFieldX, y, 80, 23, IDC_PORT);
        SendMessageW(port, EM_LIMITTEXT, 5, 0);
        SetDlgItemInt(dlg, IDC_PORT, req->port, FALSE);
        y += 34;
      }

      HWND focus = 0;
      if (req->chooseSlot) {
        AddControl(dlg, f, L"STATIC", L"Оберіть, за кого грати:", SS_LEFT, 0,
                   kMargin, y, kClientWidth - 2 * kMargin, 20, -1);
        y += 24;
        int count = 0;
        for (int i = 0; i < req->numSlots; ++i) count += req->available[i];
        int listHeight = (count < 3 ? 3 : count) * kListItemHeight + 4;
        HWND list = AddControl(
            dlg, f, L"LISTBOX", L"",
            LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY | WS_VSCROLL |
                WS_TABSTOP | LBS_NOINTEGRALHEIGHT,
            WS_EX_CLIENTEDGE, kMargin, y, kClientWidth - 2 * kMargin,
            listHeight, IDC_SLOTS);
        int select = -1;
        for (int i = 0; i < req->numSlots; ++i) {
          if (!req->available[i]) continue;
          int index = (int)SendMessageW(list, LB_ADDSTRING, 0,
                                        (LPARAM)req->slots[i].name);
          SendMessageW(list, LB_SETITEMDATA, index, i);
          if (i == req->slot || select < 0) select = index;
        }
        SendMessageW(list, LB_SETCURSEL, select, 0);
        focus = list;
        y += listHeight + 12;
      }

      AddControl(dlg, f, L"STATIC", L"Ваше ім'я:", SS_LEFT, 0, kMargin, y + 3,
                 kLabelWidth, 20, -1);
      HWND name = AddControl(dlg, f, L"EDIT", data->player.name,
                             ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
                             kFieldX, y, 200, 23, IDC_NAME);
      SendMessageW(name, EM_LIMITTEXT, kMaxNameLen - 1, 0);
      y += 34;
      if (!focus) focus = name;
      if (req->chooseSlot) {  // start with the saved name of the chosen seat
        SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDC_SLOTS, LBN_SELCHANGE),
                     (LPARAM)focus);
      }

      if (!req->chooseSlot) {
        AddControl(dlg, f, L"STATIC", L"Колір:", SS_LEFT, 0, kMargin, y + 4,
                   kLabelWidth, 20, -1);
        AddControl(dlg, f, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, 0,
                   kFieldX, y, 76, 25, IDC_COLOR);
        y += 36;
      }

      y += 6;
      AddButtons(dlg, f, req->okText, y);
      SizeDialog(dlg, kClientWidth, y + kButtonHeight + kMargin, true);
      SetFocus(focus);
      return FALSE;
    }

    case WM_COMMAND: {
      int id = LOWORD(wParam), code = HIWORD(wParam);
      PlayerSetupRequest* req = data->request;
      if (id == IDC_COLOR && code == BN_CLICKED) {
        PickColor(dlg, GetDlgItem(dlg, IDC_COLOR), &data->player.color,
                  req->taken, req->numTaken);
        InvalidateRect(GetDlgItem(dlg, IDC_COLOR), 0, TRUE);
      } else if (id == IDC_SLOTS && code == LBN_SELCHANGE) {
        int index = (int)SendDlgItemMessageW(dlg, IDC_SLOTS, LB_GETCURSEL, 0, 0);
        if (index >= 0) {
          int slot = (int)SendDlgItemMessageW(dlg, IDC_SLOTS, LB_GETITEMDATA,
                                              index, 0);
          SetDlgItemTextW(dlg, IDC_NAME, req->slots[slot].name);
        }
      } else if (id == IDOK) {
        if (req->askPort && !ReadPort(dlg, &req->port)) return TRUE;
        GetDlgItemTextW(dlg, IDC_NAME, data->player.name, kMaxNameLen);
        if (req->chooseSlot) {
          int index =
              (int)SendDlgItemMessageW(dlg, IDC_SLOTS, LB_GETCURSEL, 0, 0);
          if (index < 0) {
            MessageBoxW(dlg, L"Оберіть гравця зі списку.", req->title,
                        MB_OK | MB_ICONWARNING);
            return TRUE;
          }
          req->slot = (int)SendDlgItemMessageW(dlg, IDC_SLOTS, LB_GETITEMDATA,
                                               index, 0);
          data->player.color = req->slots[req->slot].color;
        } else {
          for (int i = 0; i < req->numTaken; ++i) {
            if (req->taken[i] == data->player.color) {
              MessageBoxW(dlg,
                          L"Цей колір уже зайнятий іншим гравцем.\n"
                          L"Оберіть інший колір.",
                          req->title, MB_OK | MB_ICONWARNING);
              return TRUE;
            }
          }
          if (!CheckColorUsable(dlg, data->player.color, req->title)) {
            return TRUE;
          }
          req->slot = -1;
        }
        req->player = data->player;
        EndDialog(dlg, IDOK);
      } else if (id == IDCANCEL) {
        EndDialog(dlg, IDCANCEL);
      }
      return TRUE;
    }

    case WM_MEASUREITEM: {
      MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
      if (mis->CtlType == ODT_LISTBOX) {
        mis->itemHeight = kListItemHeight;
        return TRUE;
      }
      return MeasureColorMenuItem(mis);
    }

    case WM_DRAWITEM: {
      DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
      if (DrawColorMenuItem(dis, data->font)) return TRUE;
      if (dis->CtlID == IDC_COLOR) {
        DrawColorButton(dis, data->player.color);
        return TRUE;
      }
      if (dis->CtlID == IDC_SLOTS && dis->itemID != (UINT)-1) {
        int slot = (int)dis->itemData;
        DrawSwatchListItem(dis, data->request->slots[slot].color,
                           data->request->slots[slot].name, data->font);
        return TRUE;
      }
      return FALSE;
    }
  }
  return FALSE;
}

}  // namespace

bool ShowConnectDialog(HWND owner, ConnectSettings* settings) {
  ConnectData data;
  data.settings = settings;
  data.font = CreateUiFont(false);
  INT_PTR result = RunDialog(owner, ConnectProc, (LPARAM)&data);
  DeleteObject(data.font);
  return result == IDOK;
}

bool ShowPlayerSetupDialog(HWND owner, PlayerSetupRequest* request) {
  SetupData data;
  data.request = request;
  data.player = request->player;
  data.font = CreateUiFont(false);
  // Default to the first free palette color in color mode.
  if (!request->chooseSlot) {
    bool clash = data.player.color == kEmptyCellColor;
    for (int i = 0; i < request->numTaken; ++i) {
      if (request->taken[i] == data.player.color) clash = true;
    }
    for (int k = 0; k < kMaxPlayers && clash; ++k) {
      clash = false;
      for (int i = 0; i < request->numTaken; ++i) {
        if (request->taken[i] == kDefaultPalette[k]) clash = true;
      }
      if (!clash) data.player.color = kDefaultPalette[k];
    }
  }
  INT_PTR result = RunDialog(owner, SetupProc, (LPARAM)&data);
  DeleteObject(data.font);
  return result == IDOK;
}
