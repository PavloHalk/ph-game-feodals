// Loopback tests of the network game: a host and clients in one process
// talk over 127.0.0.1. Build: tests\build_tests.bat.
#include <winsock2.h>

#include <stdio.h>

#include "../src/NetGame.h"
#include "../src/SaveManager.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(cond)) {                                                      \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

const uint16_t kPort = 57571;

struct Peer {
  NetGame net;
  GameState game;
  HWND window;
  int events[16];
  LPARAM lastDetail[16];
};

static LRESULT CALLBACK PeerProc(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam) {
  Peer* peer = (Peer*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (peer && msg == WM_NET_SOCKET) {
    peer->net.OnSocketMessage(wParam, lParam);
    return 0;
  }
  if (peer && msg == WM_NET_EVENT && wParam < 16) {
    ++peer->events[wParam];
    peer->lastDetail[wParam] = lParam;
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void InitPeer(Peer* peer) {
  memset(peer->events, 0, sizeof(peer->events));
  memset(peer->lastDetail, 0, sizeof(peer->lastDetail));
  peer->window = CreateWindowExW(0, L"FeodalsNetTest", L"", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, 0, GetModuleHandleW(0), 0);
  SetWindowLongPtrW(peer->window, GWLP_USERDATA, (LONG_PTR)peer);
  peer->net.SetWindow(peer->window);
}

static void Pump(DWORD ms) {
  DWORD start = GetTickCount();
  do {
    MSG msg;
    while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    Sleep(1);
  } while (GetTickCount() - start < ms);
}

// Pumps messages until `cond` holds (checked after every batch) or timeout.
#define PUMP_UNTIL(cond)                                                \
  do {                                                                  \
    DWORD start_ = GetTickCount();                                      \
    while (!(cond) && GetTickCount() - start_ < 5000) {                 \
      MSG msg_;                                                         \
      while (PeekMessageW(&msg_, 0, 0, 0, PM_REMOVE))                   \
        DispatchMessageW(&msg_);                                        \
      Sleep(1);                                                         \
    }                                                                   \
    CHECK(cond);                                                        \
  } while (0)

static PlayerInfo MakePlayer(const wchar_t* name, uint32_t color) {
  PlayerInfo p;
  memset(&p, 0, sizeof(p));
  lstrcpynW(p.name, name, kMaxNameLen);
  p.color = color;
  return p;
}

static bool SameBoard(const GameState& a, const GameState& b) {
  if (a.Width() != b.Width() || a.Height() != b.Height() ||
      a.NumPlayers() != b.NumPlayers() || a.FilledCells() != b.FilledCells() ||
      a.CurrentPlayer() != b.CurrentPlayer()) {
    return false;
  }
  for (int i = 0; i < a.NumPlayers(); ++i) {
    if (a.CellCount(i) != b.CellCount(i) ||
        a.Player(i).color != b.Player(i).color ||
        lstrcmpW(a.Player(i).name, b.Player(i).name) != 0) {
      return false;
    }
  }
  const CellMap& cells = a.Cells();
  for (size_t i = 0; i < cells.Capacity(); ++i) {
    if (!cells.SlotUsed(i)) continue;
    uint64_t k = cells.SlotKey(i);
    if (b.Owner(CellKeyX(k), CellKeyY(k)) != cells.SlotValue(i)) return false;
  }
  return true;
}

static uint32_t g_rng = 777;
static uint32_t Rand() {
  g_rng = g_rng * 1103515245u + 12345u;
  return g_rng >> 8;
}

// Plays until the game ends: whoever may move clicks a random free cell.
static void PlayRandomly(Peer* peers[], int n, int maxMoves) {
  for (int move = 0; move < maxMoves; ++move) {
    if (peers[0]->game.IsGameOver()) break;
    Peer* mover = 0;
    for (int i = 0; i < n; ++i) {
      if (peers[i]->net.CanLocalPlayerMove()) mover = peers[i];
    }
    if (!mover) {
      Pump(2);
      continue;
    }
    const GameState& g = mover->game;
    uint32_t x = 0, y = 0;
    for (int tries = 0; tries < 200; ++tries) {
      x = Rand() % g.Width();
      y = Rand() % g.Height();
      if (g.Owner(x, y) < 0) break;
    }
    if (g.Owner(x, y) >= 0) {
      for (uint64_t i = 0; i < g.TotalCells(); ++i) {
        x = (uint32_t)(i % g.Width());
        y = (uint32_t)(i / g.Width());
        if (g.Owner(x, y) < 0) break;
      }
    }
    MoveResult r;
    mover->net.LocalMove(x, y, &r);
    int before = peers[0]->events[kNetEvMoveApplied] +
                 peers[1]->events[kNetEvMoveApplied];
    PUMP_UNTIL(peers[0]->events[kNetEvMoveApplied] +
                   peers[1]->events[kNetEvMoveApplied] >
               before);
  }
}

static void TestNewGame() {
  Peer host, client;
  InitPeer(&host);
  InitPeer(&client);

  int err = host.net.HostNewGame(&host.game, kPort, 12, 10, 2,
                                 MakePlayer(L"Хост", 0x0000FF));
  CHECK(err == 0);
  CHECK(host.net.Role() == kNetHost && !host.net.IsStarted());
  CHECK(host.game.Width() == 12 && host.game.Height() == 10);
  CHECK(!host.net.CanLocalPlayerMove());

  CHECK(client.net.Connect(&client.game, L"127.0.0.1", kPort) == 0);
  PUMP_UNTIL(client.events[kNetEvJoinPrompt] == 1);
  CHECK(client.net.NumSlots() == 2);
  CHECK(client.net.Slot(0).state == kSlotConnected);
  CHECK(client.net.Slot(1).state == kSlotOpen);
  CHECK(client.game.Width() == 12);

  // Color already used by the host.
  client.net.Join(-1, MakePlayer(L"Клієнт", 0x0000FF));
  PUMP_UNTIL(client.events[kNetEvJoinRejected] == 1);
  CHECK(client.lastDetail[kNetEvJoinRejected] == kJoinColorTaken);
  CHECK(!client.net.IsJoined());

  client.net.Join(-1, MakePlayer(L"Клієнт", 0xFF0000));
  PUMP_UNTIL(client.events[kNetEvGameStarted] == 1 &&
             host.events[kNetEvGameStarted] == 1);
  CHECK(client.net.LocalSlot() == 1);
  CHECK(host.net.IsStarted() && client.net.IsStarted());
  CHECK(SameBoard(host.game, client.game));
  CHECK(lstrcmpW(host.game.Player(1).name, L"Клієнт") == 0);
  CHECK(host.net.CanLocalPlayerMove() && !client.net.CanLocalPlayerMove());

  // A client click out of turn is ignored locally.
  MoveResult r;
  CHECK(!client.net.LocalMove(3, 3, &r));

  // A third connection finds the game full.
  Peer late;
  InitPeer(&late);
  CHECK(late.net.Connect(&late.game, L"127.0.0.1", kPort) == 0);
  PUMP_UNTIL(late.events[kNetEvJoinPrompt] == 1);
  late.net.Join(-1, MakePlayer(L"Зайвий", 0x00FF00));
  PUMP_UNTIL(late.events[kNetEvJoinRejected] == 1);
  CHECK(late.lastDetail[kNetEvJoinRejected] == kJoinGameFull);
  late.net.Close();

  Peer* peers[2] = {&host, &client};
  PlayRandomly(peers, 2, 400);
  PUMP_UNTIL(host.game.IsGameOver() && client.game.IsGameOver());
  CHECK(SameBoard(host.game, client.game));
  CHECK(host.lastDetail[kNetEvMoveApplied] == 1);
  CHECK(client.lastDetail[kNetEvMoveApplied] == 1);

  host.net.Close();
  PUMP_UNTIL(client.events[kNetEvClosed] == 1);
  CHECK(client.lastDetail[kNetEvClosed] == kCloseServerStopped);
  CHECK(!client.net.IsActive());
  DestroyWindow(host.window);
  DestroyWindow(client.window);
  DestroyWindow(late.window);
}

static void TestSavedGameAndRejoin() {
  // Prepare a half-played 3-player game.
  PlayerInfo players[3] = {MakePlayer(L"A", 0x0000FF),
                           MakePlayer(L"B", 0x00FF00),
                           MakePlayer(L"C", 0xFF0000)};
  Peer host, c1, c2;
  InitPeer(&host);
  InitPeer(&c1);
  InitPeer(&c2);
  CHECK(host.game.Init(15, 15, 3, players));
  for (int i = 0; i < 60; ++i) host.game.TryClaimCell(Rand() % 15, Rand() % 15);
  int current = host.game.CurrentPlayer();

  CHECK(host.net.HostSavedGame(&host.game, kPort, 1, L"Хост B") == 0);
  CHECK(lstrcmpW(host.game.Player(1).name, L"Хост B") == 0);
  CHECK(host.net.IsSavedGame() && !host.net.IsStarted());

  CHECK(c1.net.Connect(&c1.game, L"127.0.0.1", kPort) == 0);
  PUMP_UNTIL(c1.events[kNetEvJoinPrompt] == 1 &&
             c1.events[kNetEvStateReplaced] >= 1);
  CHECK(c1.net.IsSavedGame());
  CHECK(c1.net.Slot(1).state == kSlotConnected);
  CHECK(c1.net.Slot(0).state == kSlotWaiting);
  CHECK(c1.game.FilledCells() == host.game.FilledCells());

  // The host's seat cannot be taken; colors of a saved game are fixed.
  c1.net.Join(1, MakePlayer(L"X", 0x123456));
  PUMP_UNTIL(c1.events[kNetEvJoinRejected] == 1);
  CHECK(c1.lastDetail[kNetEvJoinRejected] == kJoinSlotTaken);
  c1.net.Join(2, MakePlayer(L"Гість C", 0x123456));
  PUMP_UNTIL(c1.net.IsJoined());
  CHECK(c1.net.LocalSlot() == 2);
  PUMP_UNTIL(host.net.Slot(2).state == kSlotConnected);
  CHECK(host.net.Slot(2).info.color == 0xFF0000);
  CHECK(!host.net.IsStarted());

  CHECK(c2.net.Connect(&c2.game, L"127.0.0.1", kPort) == 0);
  PUMP_UNTIL(c2.events[kNetEvJoinPrompt] == 1);
  c2.net.Join(0, MakePlayer(L"", 0));
  PUMP_UNTIL(host.events[kNetEvGameStarted] == 1 &&
             c1.events[kNetEvGameStarted] == 1 &&
             c2.events[kNetEvGameStarted] == 1);
  CHECK(host.game.CurrentPlayer() == current);
  CHECK(lstrcmpW(host.game.Player(0).name, L"A") == 0);  // empty name kept
  CHECK(lstrcmpW(host.game.Player(2).name, L"Гість C") == 0);
  CHECK(SameBoard(host.game, c1.game) && SameBoard(host.game, c2.game));

  Peer* peers3[3] = {&host, &c1, &c2};
  for (int m = 0; m < 20; ++m) {
    Peer* mover = 0;
    for (int i = 0; i < 3; ++i)
      if (peers3[i]->net.CanLocalPlayerMove()) mover = peers3[i];
    CHECK(mover != 0);
    if (!mover) break;
    uint32_t x, y;
    do {
      x = Rand() % 15;
      y = Rand() % 15;
    } while (mover->game.Owner(x, y) >= 0);
    int before = c2.events[kNetEvMoveApplied];
    MoveResult r;
    mover->net.LocalMove(x, y, &r);
    PUMP_UNTIL(c2.events[kNetEvMoveApplied] > before &&
               SameBoard(host.game, c1.game) && SameBoard(host.game, c2.game));
    if (host.game.IsGameOver()) break;
  }

  // c1 leaves: the game pauses for everybody.
  c1.net.Close();
  PUMP_UNTIL(host.net.IsPaused() && c2.net.IsPaused());
  CHECK(host.net.Slot(2).state == kSlotWaiting);
  CHECK(!host.net.CanLocalPlayerMove() && !c2.net.CanLocalPlayerMove());

  // Rejoins the same seat and receives the current state.
  Peer back;
  InitPeer(&back);
  CHECK(back.net.Connect(&back.game, L"127.0.0.1", kPort) == 0);
  PUMP_UNTIL(back.events[kNetEvJoinPrompt] == 1);
  back.net.Join(2, MakePlayer(L"Повернувся", 0));
  PUMP_UNTIL(!host.net.IsPaused() && !c2.net.IsPaused() &&
             back.net.IsJoined() && SameBoard(host.game, back.game) &&
             SameBoard(host.game, c2.game));
  CHECK(back.net.IsStarted());
  CHECK(lstrcmpW(c2.game.Player(2).name, L"Повернувся") == 0);

  // The host saves; the file restores the same game.
  wchar_t path[MAX_PATH];
  GetTempPathW(MAX_PATH, path);
  lstrcatW(path, L"feodals_net_test.feo");
  CHECK(SaveGame(path, host.game) == kSaveOk);
  GameState loaded;
  CHECK(LoadGame(path, &loaded) == kSaveOk);
  CHECK(SameBoard(host.game, loaded));
  DeleteFileW(path);

  host.net.Close();
  PUMP_UNTIL(back.events[kNetEvClosed] == 1 && c2.events[kNetEvClosed] == 1);
  DestroyWindow(host.window);
  DestroyWindow(c1.window);
  DestroyWindow(c2.window);
  DestroyWindow(back.window);
}

static void TestConnectFailure() {
  Peer client;
  InitPeer(&client);
  CHECK(client.net.Connect(&client.game, L"127.0.0.1", kPort + 1) == 0);
  CHECK(client.net.IsConnecting());
  PUMP_UNTIL(client.events[kNetEvClosed] == 1);
  CHECK(client.lastDetail[kNetEvClosed] == kCloseConnectFailed);

  Peer a, b;
  InitPeer(&a);
  InitPeer(&b);
  PlayerInfo p = MakePlayer(L"A", 1);
  CHECK(a.net.HostNewGame(&a.game, kPort, 10, 10, 2, p) == 0);
  CHECK(b.net.HostNewGame(&b.game, kPort, 10, 10, 2, p) != 0);  // port busy
  a.net.Close();
  DestroyWindow(client.window);
  DestroyWindow(a.window);
  DestroyWindow(b.window);
}

int main() {
  WNDCLASSW wc;
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = PeerProc;
  wc.hInstance = GetModuleHandleW(0);
  wc.lpszClassName = L"FeodalsNetTest";
  RegisterClassW(&wc);

  TestNewGame();
  TestSavedGameAndRejoin();
  TestConnectFailure();
  printf("net: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
