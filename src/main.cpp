// WinMain / window procedures: the thin glue between Win32 messages and the
// game modules (GameState, Renderer, InputHandler, SaveManager, NetGame).
#include <windows.h>
#include <commctrl.h>

#include "GameState.h"
#include "InputHandler.h"
#include "NetDialogs.h"
#include "NetGame.h"
#include "NewGameDialog.h"
#include "Renderer.h"
#include "SaveManager.h"
#include "UiCommon.h"

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif
#ifndef GET_KEYSTATE_WPARAM
#define GET_KEYSTATE_WPARAM(w) (LOWORD(w))
#endif
#ifndef GET_WHEEL_DELTA_WPARAM
#define GET_WHEEL_DELTA_WPARAM(w) ((short)HIWORD(w))
#endif

namespace {

enum {
  IDM_NEW = 101,
  IDM_SAVE,
  IDM_SAVE_AS,
  IDM_LOAD,
  IDM_EXIT,
  IDM_RULES,
  IDM_ABOUT,
  IDM_NET_HOST,
  IDM_NET_HOST_SAVED,
  IDM_NET_JOIN,
  IDM_NET_LEAVE,
};

const UINT WM_APP_START = WM_APP + 1;
const int kPanelWidth = 240;

const wchar_t kMainClass[] = L"FeodalsMainWindow";
const wchar_t kBoardClass[] = L"FeodalsBoard";
const wchar_t kPanelClass[] = L"FeodalsPanel";
const wchar_t kAppTitle[] = L"Feodals";

struct App {
  HINSTANCE instance;
  HWND main, board, panel;
  GameState game;
  Renderer renderer;
  NewGameSettings settings;  // last used, prefills the dialog
  int scrollX, scrollY;
  HoverCell hover;
  bool trackingLeave;
  bool panning;
  POINT panStart;
  int panScrollX, panScrollY;
  int wheelV, wheelH;
  wchar_t filePath[MAX_PATH];
  bool dirty;
  HFONT font, boldFont, titleFont;

  NetGame net;
  ConnectSettings connect;  // last address used to join
  PlayerInfo netPlayer;     // last name/color used to join
  bool viewOnly;            // client whose network game has ended
  bool inJoinPrompt;
  wchar_t localAddresses[128];
};

App g_app;

// ---- Helpers --------------------------------------------------------------

void UpdateTitle() {
  wchar_t title[MAX_PATH + 96];
  const wchar_t* name = L"Нова гра";
  if (g_app.filePath[0]) {
    name = g_app.filePath;
    for (const wchar_t* p = g_app.filePath; *p; ++p) {
      if (*p == L'\\' || *p == L'/') name = p + 1;
    }
  }
  const wchar_t* network = L"";
  if (g_app.net.Role() == kNetHost) {
    network = L" (мережева гра, сервер)";
  } else if (g_app.net.Role() == kNetClient) {
    name = L"Мережева гра";
  } else if (g_app.viewOnly) {
    name = L"Мережева гра (завершено)";
  }
  wsprintfW(title, L"%s%s%s — %s", g_app.dirty ? L"*" : L"", name, network,
            kAppTitle);
  SetWindowTextW(g_app.main, title);
}

// Whether a click on the board may make a move right now.
bool LocalInputAllowed() {
  if (g_app.net.IsActive()) return g_app.net.CanLocalPlayerMove();
  return !g_app.viewOnly && !g_app.game.IsGameOver();
}

void RefreshAll() {
  InvalidateRect(g_app.board, 0, FALSE);
  InvalidateRect(g_app.panel, 0, FALSE);
}

void ClampScroll(int* x, int* y) {
  RECT rc;
  GetClientRect(g_app.board, &rc);
  int maxX = BoardPixelWidth(g_app.game) - rc.right;
  int maxY = BoardPixelHeight(g_app.game) - rc.bottom;
  if (*x > maxX) *x = maxX;
  if (*y > maxY) *y = maxY;
  if (*x < 0) *x = 0;
  if (*y < 0) *y = 0;
}

void UpdateScrollBars() {
  RECT rc;
  GetClientRect(g_app.board, &rc);
  ClampScroll(&g_app.scrollX, &g_app.scrollY);

  SCROLLINFO si;
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
  si.nMin = 0;
  si.nMax = BoardPixelWidth(g_app.game) - 1;
  si.nPage = (UINT)rc.right;
  si.nPos = g_app.scrollX;
  SetScrollInfo(g_app.board, SB_HORZ, &si, TRUE);
  si.nMax = BoardPixelHeight(g_app.game) - 1;
  si.nPage = (UINT)rc.bottom;
  si.nPos = g_app.scrollY;
  SetScrollInfo(g_app.board, SB_VERT, &si, TRUE);
}

void UpdateHover(int mouseX, int mouseY, bool inside) {
  HoverCell next;
  next.valid = inside && LocalInputAllowed() &&
               ClientPointToCell(g_app.game, mouseX, mouseY, g_app.scrollX,
                                 g_app.scrollY, &next.x, &next.y);
  if (!next.valid) next.x = next.y = 0;
  if (next.valid != g_app.hover.valid || next.x != g_app.hover.x ||
      next.y != g_app.hover.y) {
    g_app.hover = next;
    InvalidateRect(g_app.board, 0, FALSE);
  }
}

void ScrollTo(int x, int y) {
  ClampScroll(&x, &y);
  if (x == g_app.scrollX && y == g_app.scrollY) return;
  g_app.scrollX = x;
  g_app.scrollY = y;
  SCROLLINFO si;
  si.cbSize = sizeof(si);
  si.fMask = SIF_POS;
  si.nPos = x;
  SetScrollInfo(g_app.board, SB_HORZ, &si, TRUE);
  si.nPos = y;
  SetScrollInfo(g_app.board, SB_VERT, &si, TRUE);

  POINT pt;
  GetCursorPos(&pt);
  ScreenToClient(g_app.board, &pt);
  RECT rc;
  GetClientRect(g_app.board, &rc);
  UpdateHover(pt.x, pt.y, g_app.hover.valid && PtInRect(&rc, pt));
  InvalidateRect(g_app.board, 0, FALSE);
}

void OnScroll(int bar, int code) {
  SCROLLINFO si;
  si.cbSize = sizeof(si);
  si.fMask = SIF_ALL;
  GetScrollInfo(g_app.board, bar, &si);
  int pos = bar == SB_HORZ ? g_app.scrollX : g_app.scrollY;
  int page = (int)si.nPage;
  int step = page > 3 * kCellSize ? page - 2 * kCellSize : kCellSize;
  switch (code) {
    case SB_LINEUP: pos -= kCellSize; break;
    case SB_LINEDOWN: pos += kCellSize; break;
    case SB_PAGEUP: pos -= step; break;
    case SB_PAGEDOWN: pos += step; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    case SB_TOP: pos = 0; break;
    case SB_BOTTOM: pos = si.nMax; break;
    default: return;
  }
  if (bar == SB_HORZ) {
    ScrollTo(pos, g_app.scrollY);
  } else {
    ScrollTo(g_app.scrollX, pos);
  }
}

void ScrollByWheel(int* accumulator, int delta, bool horizontal) {
  *accumulator += delta;
  int notches = *accumulator / WHEEL_DELTA;
  if (!notches) return;
  *accumulator -= notches * WHEEL_DELTA;
  int pixels = notches * 3 * kCellSize;
  if (horizontal) {
    ScrollTo(g_app.scrollX + pixels, g_app.scrollY);
  } else {
    ScrollTo(g_app.scrollX, g_app.scrollY - pixels);
  }
}

// ---- Game flow --------------------------------------------------------------

void ShowResults() {
  const GameState& g = g_app.game;
  int order[kMaxPlayers];
  int top = g.Ranking(order);

  wchar_t text[2048], line[128], count[32];
  lstrcpyW(text, L"Гру завершено — усі клітинки поля зафарбовано.\n\n");
  FormatCount(g.CellCount(order[0]), count);
  if (top == 1) {
    wsprintfW(line, L"Переможець: %s (%s %s)\n\n", g.Player(order[0]).name,
              count, CellsWord(g.CellCount(order[0])));
    lstrcatW(text, line);
  } else {
    lstrcatW(text, L"Нічия між гравцями: ");
    for (int i = 0; i < top; ++i) {
      if (i) lstrcatW(text, L", ");
      lstrcatW(text, g.Player(order[i]).name);
    }
    wsprintfW(line, L" (по %s %s)\n\n", count,
              CellsWord(g.CellCount(order[0])));
    lstrcatW(text, line);
  }
  lstrcatW(text, L"Підсумок:\n");
  for (int i = 0; i < g.NumPlayers(); ++i) {
    int p = order[i];
    wchar_t percent[16];
    FormatCount(g.CellCount(p), count);
    FormatPercent(g.CellCount(p), g.TotalCells(), percent);
    wsprintfW(line, L"%d. %s — %s %s (%s)\n", i + 1, g.Player(p).name, count,
              CellsWord(g.CellCount(p)), percent);
    lstrcatW(text, line);
  }
  MessageBoxW(g_app.main, text, L"Кінець гри", MB_OK | MB_ICONINFORMATION);
}

// Recomputes scroll bars after the board changed. With `center` the view
// jumps to the middle of the board, so players start far from its edges.
void ResetView(bool center) {
  g_app.hover.valid = false;
  g_app.panning = false;
  if (center) {
    RECT rc;
    GetClientRect(g_app.board, &rc);
    g_app.scrollX = (BoardPixelWidth(g_app.game) - rc.right) / 2;
    g_app.scrollY = (BoardPixelHeight(g_app.game) - rc.bottom) / 2;
  }
  UpdateScrollBars();
  UpdateTitle();
  RefreshAll();
}

bool StartGame(const NewGameSettings& settings) {
  if (!g_app.game.Init(settings.width, settings.height, settings.numPlayers,
                       settings.players)) {
    return false;
  }
  g_app.settings = settings;
  g_app.filePath[0] = 0;
  g_app.dirty = false;
  g_app.viewOnly = false;
  ResetView(true);
  return true;
}

bool SaveCurrent(bool askPath) {
  if (g_app.net.Role() == kNetClient || g_app.viewOnly) {
    MessageBoxW(g_app.main,
                L"Зберегти мережеву гру може лише гравець, який її створив "
                L"(сервер).",
                kAppTitle, MB_OK | MB_ICONINFORMATION);
    return false;
  }
  if (g_app.net.Role() == kNetHost && !g_app.net.IsStarted() &&
      !g_app.net.IsSavedGame()) {
    MessageBoxW(g_app.main,
                L"Гра ще не почалася: зберегти можна після приєднання всіх "
                L"гравців.",
                kAppTitle, MB_OK | MB_ICONINFORMATION);
    return false;
  }
  wchar_t path[MAX_PATH];
  lstrcpyW(path, g_app.filePath);
  if (askPath || !path[0]) {
    if (!PromptSavePath(g_app.main, path)) return false;
  }
  SaveResult result = SaveGame(path, g_app.game);
  if (result != kSaveOk) {
    MessageBoxW(g_app.main, SaveResultText(result), L"Помилка збереження",
                MB_OK | MB_ICONERROR);
    return false;
  }
  lstrcpyW(g_app.filePath, path);
  g_app.dirty = false;
  UpdateTitle();
  return true;
}

// Asks to save unsaved progress. Returns false if the user cancelled.
bool ConfirmDiscard() {
  if (!g_app.dirty || g_app.net.Role() == kNetClient) return true;
  int answer = MessageBoxW(g_app.main,
                           L"Поточну гру не збережено. Зберегти її?",
                           kAppTitle, MB_YESNOCANCEL | MB_ICONQUESTION);
  if (answer == IDCANCEL) return false;
  if (answer == IDYES) return SaveCurrent(false);
  return true;
}

// Before replacing the game: offers to save, then ends a network session.
bool ConfirmEndSession() {
  if (!ConfirmDiscard()) return false;
  if (!g_app.net.IsActive()) return true;
  const wchar_t* text = g_app.net.Role() == kNetHost
                            ? L"Мережеву гру буде завершено для всіх гравців. "
                              L"Продовжити?"
                            : L"Вийти з мережевої гри?";
  if (MessageBoxW(g_app.main, text, kAppTitle,
                  MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
    return false;
  }
  bool client = g_app.net.Role() == kNetClient;
  g_app.net.Close();
  if (client) g_app.viewOnly = true;
  UpdateTitle();
  RefreshAll();
  return true;
}

void NewGame() {
  if (!ConfirmEndSession()) return;
  NewGameSettings settings = g_app.settings;
  if (ShowNewGameDialog(g_app.main, &settings, false)) StartGame(settings);
}

void AfterGameLoaded(const wchar_t* path) {
  const GameState& g = g_app.game;
  g_app.settings.width = g.Width();
  g_app.settings.height = g.Height();
  g_app.settings.numPlayers = g.NumPlayers();
  for (int i = 0; i < g.NumPlayers(); ++i) {
    g_app.settings.players[i] = g.Player(i);
  }
  lstrcpyW(g_app.filePath, path);
  g_app.dirty = false;
  g_app.viewOnly = false;
  ResetView(true);
}

void LoadFromFile() {
  if (!ConfirmEndSession()) return;
  wchar_t path[MAX_PATH];
  if (!PromptLoadPath(g_app.main, path)) return;
  SaveResult result = LoadGame(path, &g_app.game);
  if (result != kSaveOk) {
    MessageBoxW(g_app.main, SaveResultText(result), L"Помилка завантаження",
                MB_OK | MB_ICONERROR);
    return;
  }
  AfterGameLoaded(path);
  if (g_app.game.IsGameOver()) {
    UpdateWindow(g_app.board);
    UpdateWindow(g_app.panel);
    ShowResults();
  }
}

// ---- Network game -------------------------------------------------------------

void ShowNetError(const wchar_t* title, int error) {
  wchar_t text[256];
  if (error == WSAEADDRINUSE) {
    lstrcpyW(text, L"Цей порт уже зайнятий іншою програмою. Оберіть інший.");
  } else if (error == WSAHOST_NOT_FOUND || error == WSANO_DATA) {
    lstrcpyW(text, L"Не вдалося знайти комп'ютер за цією адресою.");
  } else if (error == WSAEADDRNOTAVAIL || error == WSAEINVAL) {
    lstrcpyW(text, L"Неправильна адреса або порт.");
  } else {
    wsprintfW(text, L"Помилка мережі (код %d).", error);
  }
  MessageBoxW(g_app.main, text, title, MB_OK | MB_ICONERROR);
}

void RefreshLocalAddresses() {
  NetGame::LocalAddresses(g_app.localAddresses,
                          sizeof(g_app.localAddresses) /
                              sizeof(g_app.localAddresses[0]));
}

void HostNetworkGame() {
  if (!ConfirmEndSession()) return;
  NewGameSettings settings = g_app.settings;
  if (!ShowNewGameDialog(g_app.main, &settings, true)) return;
  int error = g_app.net.HostNewGame(&g_app.game, settings.port, settings.width,
                                    settings.height, settings.numPlayers,
                                    settings.players[0]);
  if (error) {
    ShowNetError(L"Не вдалося створити гру", error);
    return;
  }
  g_app.settings = settings;
  g_app.filePath[0] = 0;
  g_app.dirty = false;
  g_app.viewOnly = false;
  RefreshLocalAddresses();
  ResetView(true);
}

void HostSavedNetworkGame() {
  if (!ConfirmEndSession()) return;
  wchar_t path[MAX_PATH];
  if (!PromptLoadPath(g_app.main, path)) return;
  GameState loaded;
  SaveResult result = LoadGame(path, &loaded);
  if (result != kSaveOk) {
    MessageBoxW(g_app.main, SaveResultText(result), L"Помилка завантаження",
                MB_OK | MB_ICONERROR);
    return;
  }
  if (loaded.IsGameOver()) {
    MessageBoxW(g_app.main, L"Ця гра вже завершена.", kAppTitle,
                MB_OK | MB_ICONINFORMATION);
    return;
  }

  PlayerSetupRequest req;
  memset(&req, 0, sizeof(req));
  req.title = L"Створити мережеву гру зі збереження";
  req.okText = L"Створити";
  req.info = L"Оберіть свого гравця. Решта гравців під час підключення "
             L"оберуть серед інших кольорів.";
  req.askPort = true;
  req.port = g_app.settings.port ? g_app.settings.port : kDefaultNetPort;
  req.chooseSlot = true;
  req.numSlots = loaded.NumPlayers();
  for (int i = 0; i < loaded.NumPlayers(); ++i) {
    req.slots[i] = loaded.Player(i);
    req.available[i] = true;
  }
  req.slot = 0;
  req.player = loaded.Player(0);
  if (!ShowPlayerSetupDialog(g_app.main, &req)) return;

  g_app.game.Swap(loaded);
  int error = g_app.net.HostSavedGame(&g_app.game, req.port, req.slot,
                                      req.player.name);
  AfterGameLoaded(path);
  g_app.settings.port = req.port;
  if (error) {
    ShowNetError(L"Не вдалося створити гру", error);
    return;
  }
  RefreshLocalAddresses();
  UpdateTitle();
  RefreshAll();
}

void JoinNetworkGame() {
  if (!ConfirmEndSession()) return;
  if (!ShowConnectDialog(g_app.main, &g_app.connect)) return;
  int error =
      g_app.net.Connect(&g_app.game, g_app.connect.address, g_app.connect.port);
  if (error) {
    ShowNetError(L"Не вдалося підключитися", error);
    return;
  }
  g_app.filePath[0] = 0;
  g_app.dirty = false;
  g_app.viewOnly = false;
  UpdateTitle();
  RefreshAll();
}

void ShowJoinPrompt() {
  NetGame& net = g_app.net;
  if (g_app.inJoinPrompt || net.Role() != kNetClient || net.IsJoined()) return;

  PlayerSetupRequest req;
  memset(&req, 0, sizeof(req));
  req.title = L"Приєднання до гри";
  req.okText = L"Приєднатися";
  wchar_t info[256], width[32], height[32];
  FormatCount(net.LobbyWidth(), width);
  FormatCount(net.LobbyHeight(), height);
  req.chooseSlot = net.IsSavedGame() || net.IsStarted();
  wsprintfW(info, L"Поле %s × %s, гравців: %d.%s", width, height,
            net.NumSlots(),
            req.chooseSlot ? L"\nКольори гравців уже визначені: оберіть, за "
                             L"кого гратимете."
                           : L"");
  req.info = info;
  req.numSlots = net.NumSlots();
  int available = 0;
  for (int i = 0; i < net.NumSlots(); ++i) {
    const NetSlot& slot = net.Slot(i);
    req.slots[i] = slot.info;
    req.available[i] = slot.state == kSlotWaiting;
    available += req.available[i];
    if (slot.state != kSlotOpen) req.taken[req.numTaken++] = slot.info.color;
  }
  if (req.chooseSlot && !available) {
    net.Close();
    g_app.viewOnly = false;
    MessageBoxW(g_app.main, L"У цій грі вже немає вільних місць.", kAppTitle,
                MB_OK | MB_ICONINFORMATION);
    UpdateTitle();
    RefreshAll();
    return;
  }
  req.player = g_app.netPlayer;
  req.slot = -1;
  // An untouched default name follows the seat the player will most likely
  // get, so it does not clash with "Гравець 1" of the host.
  for (int k = 0; k < kMaxPlayers; ++k) {
    wchar_t name[kMaxNameLen];
    DefaultPlayerName(k, name);
    if (lstrcmpW(name, req.player.name) != 0) continue;
    for (int i = 0; i < net.NumSlots(); ++i) {
      if (net.Slot(i).state != kSlotConnected) {
        DefaultPlayerName(i, req.player.name);
        break;
      }
    }
    break;
  }

  g_app.inJoinPrompt = true;
  bool ok = ShowPlayerSetupDialog(g_app.main, &req);
  g_app.inJoinPrompt = false;
  if (net.Role() != kNetClient) return;  // connection lost meanwhile
  if (!ok) {
    net.Close();
    UpdateTitle();
    RefreshAll();
    return;
  }
  if (!req.chooseSlot) g_app.netPlayer = req.player;
  lstrcpyW(g_app.netPlayer.name, req.player.name);
  net.Join(req.slot, req.player);
}

void OnJoinRejected(int reason) {
  const wchar_t* text = L"Сервер відхилив запит на приєднання.";
  bool retry = true;
  switch (reason) {
    case kJoinColorTaken:
      text = L"Цей колір щойно зайняв інший гравець. Оберіть інший.";
      break;
    case kJoinSlotTaken:
      text = L"За цього гравця вже грає хтось інший. Оберіть іншого.";
      break;
    case kJoinGameFull:
      text = L"У цій грі вже немає вільних місць.";
      retry = false;
      break;
  }
  MessageBoxW(g_app.main, text, kAppTitle, MB_OK | MB_ICONWARNING);
  if (!retry) {
    g_app.net.Close();
    UpdateTitle();
    RefreshAll();
    return;
  }
  ShowJoinPrompt();
}

void OnNetClosed(int reason) {
  const wchar_t* text = 0;
  switch (reason) {
    case kCloseConnectFailed:
      text = L"Не вдалося підключитися до гри. Перевірте адресу, порт і те, що "
             L"гру створено (а брандмауер Windows дозволяє з'єднання).";
      break;
    case kCloseConnectionLost:
      text = L"З'єднання з сервером гри втрачено.";
      break;
    case kCloseServerStopped:
      text = L"Сервер завершив мережеву гру.";
      break;
    case kCloseVersionMismatch:
      text = L"Версія гри на сервері відрізняється від вашої.";
      break;
    default:
      text = L"Помилка обміну даними з сервером. Гру завершено.";
      break;
  }
  g_app.viewOnly = g_app.game.Width() != 0 && reason != kCloseConnectFailed;
  UpdateTitle();
  RefreshAll();
  MessageBoxW(g_app.main, text, kAppTitle, MB_OK | MB_ICONWARNING);
}

void OnNetEvent(WPARAM event, LPARAM detail) {
  switch (event) {
    case kNetEvLobbyChanged:
    case kNetEvConnected:
    case kNetEvJoined:
      UpdateTitle();
      RefreshAll();
      break;
    case kNetEvJoinPrompt:
      ShowJoinPrompt();
      break;
    case kNetEvJoinRejected:
      OnJoinRejected((int)detail);
      break;
    case kNetEvGameStarted:
      ResetView(true);
      MessageBeep(MB_OK);
      if (GetForegroundWindow() != g_app.main) FlashWindow(g_app.main, TRUE);
      break;
    case kNetEvStateReplaced:
      ResetView(detail != 0);
      break;
    case kNetEvMoveApplied:
      if (g_app.net.Role() == kNetHost && !g_app.dirty) {
        g_app.dirty = true;
        UpdateTitle();
      }
      RefreshAll();
      if (detail) {
        UpdateWindow(g_app.board);
        UpdateWindow(g_app.panel);
        ShowResults();
      } else if (g_app.net.CanLocalPlayerMove() &&
                 GetForegroundWindow() != g_app.main) {
        FlashWindow(g_app.main, TRUE);
      }
      break;
    case kNetEvClosed:
      OnNetClosed((int)detail);
      break;
  }
}

void ShowRules() {
  MessageBoxW(
      g_app.main,
      L"Гравці ходять по черзі. Хід — клік лівою кнопкою миші по нічиїй "
      L"клітинці: вона фарбується у ваш колір.\n\n"
      L"Захоплення оточенням: якщо ваші клітинки повністю оточують область "
      L"(нічиї та/або чужі клітинки), уся вона переходить до вас.\n"
      L"• Як у «Крапках» на папері, клітинки, що торкаються лише кутами, "
      L"теж утворюють суцільну стіну.\n"
      L"• Край поля не є стіною: область, що торкається краю, не "
      L"захоплюється.\n\n"
      L"Гра закінчується, коли всі клітинки зафарбовано. Перемагає гравець "
      L"з найбільшою кількістю клітинок.\n\n"
      L"Керування полем:\n"
      L"• колесо миші — вертикальна прокрутка, Shift+колесо — горизонтальна;\n"
      L"• права кнопка миші (затиснути й тягнути) — перетягування поля;\n"
      L"• стрілки, PageUp/PageDown, Home/End — прокрутка клавіатурою.",
      L"Правила гри", MB_OK | MB_ICONINFORMATION);
}

void ShowAbout() {
  MessageBoxW(g_app.main,
              L"Feodals 1.0\n\n"
              L"Покрокова гра на захоплення території для 2–8 гравців на "
              L"одному комп'ютері або по локальній мережі.\n"
              L"C++ і чистий Win32 API, без сторонніх бібліотек.",
              L"Про програму", MB_OK | MB_ICONINFORMATION);
}

// ---- Board window -----------------------------------------------------------

void OnBoardClick(int x, int y) {
  if (!LocalInputAllowed()) return;
  MoveResult r;
  if (g_app.net.IsActive()) {
    uint32_t cx, cy;
    if (!ClientPointToCell(g_app.game, x, y, g_app.scrollX, g_app.scrollY, &cx,
                           &cy) ||
        !g_app.net.LocalMove(cx, cy, &r)) {
      return;
    }
    g_app.hover.valid = false;
    if (!r.accepted) {  // client: the host applies the move and tells us
      RefreshAll();
      return;
    }
  } else {
    r = HandleBoardClick(g_app.game, x, y, g_app.scrollX, g_app.scrollY);
    if (!r.accepted) return;
  }
  if (!g_app.dirty) {
    g_app.dirty = true;
    UpdateTitle();
  }
  RefreshAll();
  if (r.gameOver) {
    UpdateWindow(g_app.board);
    UpdateWindow(g_app.panel);
    ShowResults();
  }
}

LRESULT CALLBACK BoardProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      g_app.renderer.Paint(dc, rc.right, rc.bottom, g_app.game, g_app.scrollX,
                           g_app.scrollY, g_app.hover);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      UpdateScrollBars();
      InvalidateRect(hwnd, 0, FALSE);
      return 0;
    case WM_HSCROLL:
      OnScroll(SB_HORZ, LOWORD(wParam));
      return 0;
    case WM_VSCROLL:
      OnScroll(SB_VERT, LOWORD(wParam));
      return 0;
    case WM_MOUSEWHEEL:
      ScrollByWheel(&g_app.wheelV, GET_WHEEL_DELTA_WPARAM(wParam),
                    (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0);
      return 0;
    case WM_MOUSEHWHEEL:
      ScrollByWheel(&g_app.wheelH, GET_WHEEL_DELTA_WPARAM(wParam), true);
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      if (!g_app.panning) {
        OnBoardClick((short)LOWORD(lParam), (short)HIWORD(lParam));
      }
      return 0;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
      SetFocus(hwnd);
      SetCapture(hwnd);
      g_app.panning = true;
      g_app.panStart.x = (short)LOWORD(lParam);
      g_app.panStart.y = (short)HIWORD(lParam);
      g_app.panScrollX = g_app.scrollX;
      g_app.panScrollY = g_app.scrollY;
      SetCursor(LoadCursor(0, IDC_SIZEALL));
      return 0;
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
      if (g_app.panning) ReleaseCapture();
      return 0;
    case WM_CAPTURECHANGED:
      g_app.panning = false;
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lParam) == HTCLIENT) {
        SetCursor(LoadCursor(0, g_app.panning ? IDC_SIZEALL : IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_MOUSEMOVE: {
      int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
      if (g_app.panning) {
        ScrollTo(g_app.panScrollX - (x - g_app.panStart.x),
                 g_app.panScrollY - (y - g_app.panStart.y));
        return 0;
      }
      if (!g_app.trackingLeave) {
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        tme.dwHoverTime = 0;
        g_app.trackingLeave = TrackMouseEvent(&tme) != 0;
      }
      UpdateHover(x, y, true);
      return 0;
    }
    case WM_MOUSELEAVE:
      g_app.trackingLeave = false;
      UpdateHover(0, 0, false);
      return 0;
    case WM_KEYDOWN: {
      RECT rc;
      GetClientRect(hwnd, &rc);
      int pageY =
          rc.bottom > 3 * kCellSize ? rc.bottom - 2 * kCellSize : kCellSize;
      int x = g_app.scrollX, y = g_app.scrollY;
      switch (wParam) {
        case VK_LEFT: x -= kCellSize; break;
        case VK_RIGHT: x += kCellSize; break;
        case VK_UP: y -= kCellSize; break;
        case VK_DOWN: y += kCellSize; break;
        case VK_PRIOR: y -= pageY; break;
        case VK_NEXT: y += pageY; break;
        case VK_HOME:
          x = 0;
          if (GetKeyState(VK_CONTROL) < 0) y = 0;
          break;
        case VK_END:
          x = BoardPixelWidth(g_app.game);
          if (GetKeyState(VK_CONTROL) < 0) y = BoardPixelHeight(g_app.game);
          break;
        default:
          return DefWindowProcW(hwnd, msg, wParam, lParam);
      }
      ScrollTo(x, y);
      return 0;
    }
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- Side panel -------------------------------------------------------------

void DrawTextAt(HDC dc, const wchar_t* text, int left, int top, int right,
                int bottom, UINT flags) {
  RECT rc = {left, top, right, bottom};
  DrawTextW(dc, text, -1, &rc,
            flags | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void PaintPanel(HWND hwnd) {
  PAINTSTRUCT ps;
  HDC target = BeginPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);
  int w = rc.right, h = rc.bottom;
  HDC dc = CreateCompatibleDC(target);
  HBITMAP bitmap = CreateCompatibleBitmap(target, w > 0 ? w : 1, h > 0 ? h : 1);
  HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
  HGDIOBJ oldFont = SelectObject(dc, g_app.font);

  FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
  RECT border = {0, 0, 1, h};
  FillRect(dc, &border, GetSysColorBrush(COLOR_BTNSHADOW));
  SetBkMode(dc, TRANSPARENT);

  const GameState& g = g_app.game;
  const NetGame& net = g_app.net;
  bool over = g.IsGameOver();
  bool lobby = net.IsActive() && !net.IsStarted() && net.NumSlots() > 0;
  bool paused = net.IsPaused();
  COLORREF text = GetSysColor(COLOR_BTNTEXT);
  COLORREF dim = GetSysColor(COLOR_GRAYTEXT);
  wchar_t buf[160], count[32], percent[16];

  int y = 12;
  SelectObject(dc, g_app.titleFont);
  SetTextColor(dc, text);
  const wchar_t* title = over      ? L"Гру завершено"
                         : lobby   ? L"Очікування гравців"
                         : paused  ? L"Пауза"
                                   : L"Гравці";
  DrawTextAt(dc, title, 14, y, w - 10, y + 24, DT_LEFT);
  y += 32;

  uint64_t best = 0;
  for (int i = 0; i < g.NumPlayers(); ++i) {
    if (g.CellCount(i) > best) best = g.CellCount(i);
  }

  int rows = lobby ? net.NumSlots() : g.NumPlayers();
  for (int i = 0; i < rows; ++i) {
    PlayerInfo p = lobby ? net.Slot(i).info : g.Player(i);
    int state = net.IsActive() && i < net.NumSlots() ? net.Slot(i).state
                                                     : kSlotConnected;
    bool open = lobby && state == kSlotOpen;
    bool active = over     ? g.CellCount(i) == best
                  : lobby  ? false
                           : i == g.CurrentPlayer();
    RECT row = {8, y, w - 8, y + 42};
    if (active) {
      HBRUSH highlight = CreateSolidBrush(RGB(255, 246, 204));
      FillRect(dc, &row, highlight);
      DeleteObject(highlight);
      HBRUSH accent = CreateSolidBrush(p.color);
      RECT bar = {row.left, row.top, row.left + 4, row.bottom};
      FillRect(dc, &bar, accent);
      DeleteObject(accent);
      FrameRect(dc, &row, GetSysColorBrush(COLOR_BTNSHADOW));
    }
    RECT swatch = {row.left + 12, y + 11, row.left + 32, y + 31};
    if (!open) {
      HBRUSH brush = CreateSolidBrush(p.color);
      FillRect(dc, &swatch, brush);
      DeleteObject(brush);
    }
    FrameRect(dc, &swatch, (HBRUSH)GetStockObject(GRAY_BRUSH));

    SelectObject(dc, active ? g_app.boldFont : g_app.font);
    SetTextColor(dc, active ? RGB(0, 0, 0) : open ? dim : text);
    if (open) {
      lstrcpyW(buf, L"Вільне місце");
    } else {
      wsprintfW(buf, L"%s%s%s", active && !over ? L"\x25BA " : L"", p.name,
                net.IsActive() && i == net.LocalSlot() ? L" (ви)" : L"");
    }
    DrawTextAt(dc, buf, row.left + 42, y + 4, row.right - 6, y + 22, DT_LEFT);

    SelectObject(dc, g_app.font);
    if (net.IsActive() && state == kSlotWaiting) {
      SetTextColor(dc, RGB(190, 60, 30));
      lstrcpyW(buf, net.IsStarted() ? L"від'єднався, очікуємо…"
                                    : L"очікуємо на гравця…");
    } else if (lobby) {
      SetTextColor(dc, dim);
      lstrcpyW(buf, open ? L"очікуємо на гравця…" : L"приєднався");
    } else {
      SetTextColor(dc, dim);
      FormatCount(g.CellCount(i), count);
      FormatPercent(g.CellCount(i), g.TotalCells(), percent);
      wsprintfW(buf, L"%s %s · %s", count, CellsWord(g.CellCount(i)), percent);
    }
    DrawTextAt(dc, buf, row.left + 42, y + 21, row.right - 6, y + 39, DT_LEFT);
    y += 46;
  }

  y += 6;
  RECT line = {14, y, w - 14, y + 1};
  FillRect(dc, &line, GetSysColorBrush(COLOR_BTNSHADOW));
  y += 10;

  SelectObject(dc, g_app.font);
  SetTextColor(dc, text);
  wchar_t width[32], height[32];
  FormatCount(g.Width(), width);
  FormatCount(g.Height(), height);
  wsprintfW(buf, L"Поле: %s × %s", width, height);
  DrawTextAt(dc, buf, 14, y, w - 10, y + 18, DT_LEFT);
  y += 20;
  FormatCount(g.FilledCells(), count);
  FormatPercent(g.FilledCells(), g.TotalCells(), percent);
  wsprintfW(buf, L"Зафарбовано: %s (%s)", count, percent);
  DrawTextAt(dc, buf, 14, y, w - 10, y + 18, DT_LEFT);
  y += 20;
  FormatCount(g.TotalCells() - g.FilledCells(), count);
  wsprintfW(buf, L"Вільних: %s", count);
  DrawTextAt(dc, buf, 14, y, w - 10, y + 18, DT_LEFT);
  y += 28;

  // Network status.
  if (net.IsActive() || g_app.viewOnly) {
    RECT sep = {14, y - 8, w - 14, y - 7};
    FillRect(dc, &sep, GetSysColorBrush(COLOR_BTNSHADOW));
    y += 2;
    SelectObject(dc, g_app.boldFont);
    SetTextColor(dc, text);
    if (net.Role() == kNetHost) {
      wsprintfW(buf, L"Мережева гра · сервер, порт %u", net.Port());
    } else if (net.Role() == kNetClient) {
      lstrcpyW(buf, L"Мережева гра · клієнт");
    } else {
      lstrcpyW(buf, L"Мережеву гру завершено");
    }
    DrawTextAt(dc, buf, 14, y, w - 10, y + 18, DT_LEFT);
    y += 20;
    SelectObject(dc, g_app.font);
    if (net.Role() == kNetHost && g_app.localAddresses[0]) {
      wsprintfW(buf, L"IP: %s", g_app.localAddresses);
      RECT ip = {14, y, w - 10, y + 36};
      DrawTextW(dc, buf, -1, &ip, DT_WORDBREAK | DT_NOPREFIX);
      y += lstrlenW(buf) > 30 ? 38 : 20;
    }
    buf[0] = 0;
    if (net.IsConnecting()) {
      lstrcpyW(buf, L"Підключення…");
    } else if (net.Role() == kNetClient && !net.IsJoined()) {
      lstrcpyW(buf, L"Вибір гравця…");
    } else if (lobby) {
      wsprintfW(buf, L"Приєдналося: %d з %d", net.ConnectedSlots(),
                net.NumSlots());
    } else if (paused) {
      lstrcpyW(buf, L"Гру призупинено: очікуємо гравців");
    }
    if (buf[0]) {
      SetTextColor(dc, dim);
      DrawTextAt(dc, buf, 14, y, w - 10, y + 18, DT_LEFT);
      y += 20;
    }
    y += 8;
  }

  if (!over && !lobby && g.NumPlayers() && !g_app.viewOnly &&
      !net.IsConnecting()) {
    SelectObject(dc, g_app.boldFont);
    SetTextColor(dc, text);
    if (net.IsActive() && net.CanLocalPlayerMove()) {
      SetTextColor(dc, RGB(0, 130, 40));
      lstrcpyW(buf, L"Ваш хід!");
    } else {
      wsprintfW(buf, L"Хід: %s", g.Player(g.CurrentPlayer()).name);
    }
    DrawTextAt(dc, buf, 14, y, w - 10, y + 18, DT_LEFT);
  }

  // Controls hint pinned to the bottom when there is room for it.
  static const wchar_t* const kHints[] = {
      L"ЛКМ — зайняти клітинку",
      L"ПКМ + тягнути — рухати поле",
      L"Колесо / Shift+колесо — прокрутка",
  };
  int hintsTop = h - 12 - 3 * 18;
  if (hintsTop > y + 30) {
    SelectObject(dc, g_app.font);
    SetTextColor(dc, dim);
    for (int i = 0; i < 3; ++i) {
      DrawTextAt(dc, kHints[i], 14, hintsTop + i * 18, w - 10,
                 hintsTop + i * 18 + 18, DT_LEFT);
    }
  }

  BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);
  SelectObject(dc, oldFont);
  SelectObject(dc, oldBitmap);
  DeleteObject(bitmap);
  DeleteDC(dc);
  EndPaint(hwnd, &ps);
}

LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_PAINT:
      PaintPanel(hwnd);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      InvalidateRect(hwnd, 0, FALSE);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- Main window ------------------------------------------------------------

HMENU BuildMenu() {
  HMENU game = CreatePopupMenu();
  AppendMenuW(game, MF_STRING, IDM_NEW, L"&Нова гра…\tCtrl+N");
  AppendMenuW(game, MF_SEPARATOR, 0, 0);
  AppendMenuW(game, MF_STRING, IDM_SAVE, L"&Зберегти\tCtrl+S");
  AppendMenuW(game, MF_STRING, IDM_SAVE_AS, L"Зберегти &як…\tCtrl+Shift+S");
  AppendMenuW(game, MF_STRING, IDM_LOAD, L"З&авантажити…\tCtrl+O");
  AppendMenuW(game, MF_SEPARATOR, 0, 0);
  AppendMenuW(game, MF_STRING, IDM_EXIT, L"&Вихід\tAlt+F4");

  HMENU network = CreatePopupMenu();
  AppendMenuW(network, MF_STRING, IDM_NET_HOST, L"&Створити мережеву гру…");
  AppendMenuW(network, MF_STRING, IDM_NET_HOST_SAVED,
              L"Створити мережеву гру зі &збереження…");
  AppendMenuW(network, MF_STRING, IDM_NET_JOIN, L"&Приєднатися до гри…");
  AppendMenuW(network, MF_SEPARATOR, 0, 0);
  AppendMenuW(network, MF_STRING, IDM_NET_LEAVE, L"&Вийти з мережевої гри");

  HMENU help = CreatePopupMenu();
  AppendMenuW(help, MF_STRING, IDM_RULES, L"&Правила гри\tF1");
  AppendMenuW(help, MF_STRING, IDM_ABOUT, L"Про &програму");

  HMENU bar = CreateMenu();
  AppendMenuW(bar, MF_POPUP, (UINT_PTR)game, L"&Гра");
  AppendMenuW(bar, MF_POPUP, (UINT_PTR)network, L"&Мережа");
  AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Довідка");
  return bar;
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE:
      g_app.main = hwnd;
      g_app.net.SetWindow(hwnd);
      g_app.board = CreateWindowExW(
          0, kBoardClass, L"", WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL,
          0, 0, 0, 0, hwnd, 0, g_app.instance, 0);
      g_app.panel = CreateWindowExW(0, kPanelClass, L"", WS_CHILD | WS_VISIBLE,
                                    0, 0, 0, 0, hwnd, 0, g_app.instance, 0);
      return 0;
    case WM_SIZE: {
      int w = LOWORD(lParam), h = HIWORD(lParam);
      int panel = w > kPanelWidth + 100 ? kPanelWidth : w / 3;
      MoveWindow(g_app.board, 0, 0, w - panel, h, TRUE);
      MoveWindow(g_app.panel, w - panel, 0, panel, h, TRUE);
      return 0;
    }
    case WM_GETMINMAXINFO: {
      MINMAXINFO* mmi = (MINMAXINFO*)lParam;
      mmi->ptMinTrackSize.x = 520;
      mmi->ptMinTrackSize.y = 460;
      return 0;
    }
    case WM_SETFOCUS:
      SetFocus(g_app.board);
      return 0;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
      return SendMessageW(g_app.board, msg, wParam, lParam);
    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case IDM_NEW: NewGame(); break;
        case IDM_SAVE: SaveCurrent(false); break;
        case IDM_SAVE_AS: SaveCurrent(true); break;
        case IDM_LOAD: LoadFromFile(); break;
        case IDM_EXIT: SendMessageW(hwnd, WM_CLOSE, 0, 0); break;
        case IDM_RULES: ShowRules(); break;
        case IDM_ABOUT: ShowAbout(); break;
        case IDM_NET_HOST: HostNetworkGame(); break;
        case IDM_NET_HOST_SAVED: HostSavedNetworkGame(); break;
        case IDM_NET_JOIN: JoinNetworkGame(); break;
        case IDM_NET_LEAVE: ConfirmEndSession(); break;
      }
      return 0;
    case WM_INITMENUPOPUP: {
      HMENU menu = GetMenu(hwnd);
      bool canSave = g_app.net.Role() != kNetClient && !g_app.viewOnly;
      UINT save = MF_BYCOMMAND | (canSave ? MF_ENABLED : MF_GRAYED);
      EnableMenuItem(menu, IDM_SAVE, save);
      EnableMenuItem(menu, IDM_SAVE_AS, save);
      EnableMenuItem(menu, IDM_NET_LEAVE,
                     MF_BYCOMMAND |
                         (g_app.net.IsActive() ? MF_ENABLED : MF_GRAYED));
      return 0;
    }
    case WM_NET_SOCKET:
      g_app.net.OnSocketMessage(wParam, lParam);
      return 0;
    case WM_NET_EVENT:
      OnNetEvent(wParam, lParam);
      return 0;
    case WM_APP_START: {
      NewGameSettings settings = g_app.settings;
      if (ShowNewGameDialog(hwnd, &settings, false)) StartGame(settings);
      return 0;
    }
    case WM_CLOSE:
      if (ConfirmEndSession()) DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool RegisterClasses(HINSTANCE instance) {
  WNDCLASSW wc;
  memset(&wc, 0, sizeof(wc));
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(0, IDC_ARROW);

  wc.lpfnWndProc = MainProc;
  wc.lpszClassName = kMainClass;
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  if (!wc.hIcon) wc.hIcon = LoadIcon(0, IDI_APPLICATION);
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  if (!RegisterClassW(&wc)) return false;

  wc.hIcon = 0;
  wc.hbrBackground = 0;
  wc.lpfnWndProc = BoardProc;
  wc.lpszClassName = kBoardClass;
  wc.style = CS_DBLCLKS;
  if (!RegisterClassW(&wc)) return false;

  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = PanelProc;
  wc.lpszClassName = kPanelClass;
  return RegisterClassW(&wc) != 0;
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCmd) {
  InitCommonControls();  // loads comctl32 so the v6 manifest styles apply

  g_app.instance = instance;
  g_app.font = CreateUiFont(false);
  g_app.boldFont = CreateUiFont(true);
  g_app.titleFont = CreateUiFont(true, 3);
  DefaultNewGameSettings(&g_app.settings);
  lstrcpyW(g_app.connect.address, L"192.168.");
  g_app.connect.port = kDefaultNetPort;
  DefaultPlayerName(0, g_app.netPlayer.name);
  g_app.netPlayer.color = kDefaultPalette[0];
  g_app.game.Init(g_app.settings.width, g_app.settings.height,
                  g_app.settings.numPlayers, g_app.settings.players);

  if (!RegisterClasses(instance)) return 1;

  HWND hwnd = CreateWindowExW(0, kMainClass, kAppTitle, WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1024, 740, 0,
                              BuildMenu(), instance, 0);
  if (!hwnd) return 1;
  UpdateTitle();
  UpdateScrollBars();
  ShowWindow(hwnd, showCmd);
  UpdateWindow(hwnd);
  PostMessageW(hwnd, WM_APP_START, 0, 0);

  ACCEL accels[] = {
      {FVIRTKEY | FCONTROL, 'N', IDM_NEW},
      {FVIRTKEY | FCONTROL, 'S', IDM_SAVE},
      {FVIRTKEY | FCONTROL | FSHIFT, 'S', IDM_SAVE_AS},
      {FVIRTKEY | FCONTROL, 'O', IDM_LOAD},
      {FVIRTKEY, VK_F1, IDM_RULES},
  };
  HACCEL accelTable =
      CreateAcceleratorTableW(accels, sizeof(accels) / sizeof(accels[0]));

  MSG msg;
  while (GetMessageW(&msg, 0, 0, 0) > 0) {
    if (!TranslateAcceleratorW(hwnd, accelTable, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  DestroyAcceleratorTable(accelTable);
  DeleteObject(g_app.font);
  DeleteObject(g_app.boldFont);
  DeleteObject(g_app.titleFont);
  return (int)msg.wParam;
}
