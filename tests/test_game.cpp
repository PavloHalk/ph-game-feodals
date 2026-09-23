// Console tests for the game core and the save format.
// Build: tests\build_tests.bat (MinGW), then run build\test_game.exe.
#include <stdio.h>
#include <windows.h>

#include "../src/Bot.h"
#include "../src/GameState.h"
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

static void InitGame(GameState* g, uint32_t w, uint32_t h, int players) {
  PlayerInfo info[kMaxPlayers];
  memset(info, 0, sizeof(info));
  for (int i = 0; i < players; ++i) {
    wsprintfW(info[i].name, L"P%d", i + 1);
    info[i].color = 0x101010u * (i + 1);
  }
  bool ok = g->Init(w, h, players, info);
  CHECK(ok);
}

// Places cells from an ASCII picture: '0'..'7' = owner, '.' = empty.
static void Draw(GameState* g, uint32_t ox, uint32_t oy, const char* rows[],
                 int n) {
  for (int y = 0; y < n; ++y) {
    for (int x = 0; rows[y][x]; ++x) {
      char c = rows[y][x];
      if (c >= '0' && c <= '7') CHECK(g->PlaceCell(ox + x, oy + y, c - '0'));
    }
  }
}

static MoveResult Claim(GameState* g, int player, uint32_t x, uint32_t y) {
  g->SetCurrentPlayer(player);
  return g->TryClaimCell(x, y);
}

static void TestBasicMove() {
  GameState g;
  InitGame(&g, 10, 10, 3);
  MoveResult r = g.TryClaimCell(3, 4);
  CHECK(r.accepted && r.captured == 0 && !r.gameOver);
  CHECK(g.Owner(3, 4) == 0);
  CHECK(g.CurrentPlayer() == 1);
  CHECK(g.CellCount(0) == 1);

  r = g.TryClaimCell(3, 4);  // occupied: ignored, turn does not pass
  CHECK(!r.accepted);
  CHECK(g.CurrentPlayer() == 1 && g.Owner(3, 4) == 0);

  r = g.TryClaimCell(10, 0);  // out of range
  CHECK(!r.accepted);
  CHECK(g.TryClaimCell(9, 9).accepted);
  CHECK(g.CurrentPlayer() == 2);
  CHECK(g.TryClaimCell(0, 0).accepted);
  CHECK(g.CurrentPlayer() == 0);

  // The last accepted move is remembered; rejected clicks do not change it.
  CHECK(g.HasLastMove() && g.LastMoveX() == 0 && g.LastMoveY() == 0);
  CHECK(!g.TryClaimCell(9, 9).accepted);
  CHECK(g.LastMoveX() == 0 && g.LastMoveY() == 0);
  GameState other;
  CHECK(!other.HasLastMove());
  other.Swap(g);
  CHECK(other.HasLastMove() && !g.HasLastMove());
  InitGame(&other, 10, 10, 2);
  CHECK(!other.HasLastMove());
}

static void TestSpecExample() {
  GameState g;
  InitGame(&g, 10, 10, 2);
  const char* pic[] = {"000", "01.", "000"};
  Draw(&g, 3, 3, pic, 3);
  MoveResult r = Claim(&g, 0, 5, 4);
  CHECK(r.accepted);
  CHECK(r.captured == 1);
  CHECK(g.Owner(4, 4) == 0);
  CHECK(g.CellCount(0) == 9 && g.CellCount(1) == 0);
  CHECK(g.Owner(2, 2) == -1);
  CHECK(r.minX == 4 && r.maxX == 5 && r.minY == 4 && r.maxY == 4);
}

static void TestEdgeIsNotWall() {
  GameState g;
  InitGame(&g, 10, 10, 2);
  const char* pic[] = {"0.0", "0.0", "00."};
  Draw(&g, 3, 0, pic, 3);
  // Region (4,0)-(4,1) touches the top edge.
  MoveResult r = Claim(&g, 0, 5, 2);
  CHECK(r.accepted && r.captured == 0);
  CHECK(g.Owner(4, 0) == -1 && g.Owner(4, 1) == -1);

  // Pocket in the corner, walls on two sides only.
  GameState c;
  InitGame(&c, 10, 10, 2);
  const char* corner[] = {".0", "0."};
  Draw(&c, 0, 0, corner, 2);
  r = Claim(&c, 0, 1, 1);
  CHECK(r.accepted && r.captured == 0 && c.Owner(0, 0) == -1);
}

static void TestDiagonalWalls() {
  // The case from the playtest: a "+" of blue around a red cell captures it,
  // cells touching by corners form a closed wall.
  GameState g;
  InitGame(&g, 20, 20, 2);
  const char* plus[] = {".1.", "101", "..."};
  Draw(&g, 8, 8, plus, 3);
  MoveResult r = Claim(&g, 1, 9, 10);
  CHECK(r.accepted && r.captured == 1);
  CHECK(g.Owner(9, 9) == 1);
  CHECK(g.CellCount(0) == 0 && g.CellCount(1) == 5);
  CHECK(g.Owner(8, 8) == -1 && g.Owner(10, 10) == -1);

  // A diamond made only of corner-touching cells encloses its interior.
  GameState d;
  InitGame(&d, 12, 12, 2);
  const char* diamond[] = {".....", ".0.0.", "0...0", ".0.0.", "..0.."};
  Draw(&d, 3, 3, diamond, 5);
  r = Claim(&d, 0, 5, 3);
  CHECK(r.captured == 5);
  CHECK(d.Owner(5, 5) == 0 && d.Owner(4, 5) == 0 && d.Owner(5, 6) == 0);
  CHECK(d.Owner(4, 3) == -1 && d.Owner(4, 7) == -1);

  // A real gap (no touch at all) still leaks.
  GameState l;
  InitGame(&l, 12, 12, 2);
  const char* leak[] = {"000", "0.0", "..."};
  Draw(&l, 3, 3, leak, 3);
  r = Claim(&l, 0, 3, 5);
  CHECK(r.accepted && r.captured == 0 && l.Owner(4, 4) == -1);
}

static void TestMultipleOpponents() {
  GameState g;
  InitGame(&g, 20, 20, 4);
  const char* pic[] = {
      "00000",
      "012.0",
      "0.3.0",
      "01.20",
      "000.0",
  };
  Draw(&g, 5, 5, pic, 5);
  MoveResult r = Claim(&g, 0, 8, 9);
  CHECK(r.captured == 9);
  for (uint32_t y = 6; y <= 8; ++y)
    for (uint32_t x = 6; x <= 8; ++x) CHECK(g.Owner(x, y) == 0);
  CHECK(g.CellCount(1) == 0 && g.CellCount(2) == 0 && g.CellCount(3) == 0);
  CHECK(g.CellCount(0) == 25);
  CHECK(g.FilledCells() == 25);
  CHECK(g.CurrentPlayer() == 1);
}

static void TestTwoPocketsAtOnce() {
  GameState g;
  InitGame(&g, 12, 12, 2);
  const char* pic[] = {"00000", "01.10", "00000"};
  Draw(&g, 2, 2, pic, 3);
  MoveResult r = Claim(&g, 0, 4, 3);  // splits the gap into two pockets
  CHECK(r.captured == 2);
  CHECK(g.Owner(3, 3) == 0 && g.Owner(5, 3) == 0);
  CHECK(g.CellCount(1) == 0 && g.CellCount(0) == 15);
}

static void TestNestedCapture() {
  GameState g;
  InitGame(&g, 20, 20, 2);
  const char* pic[] = {
      "0000000",
      "0.....0",
      "0.111.0",
      "0.111.0",
      "0.111.0",
      "0.....0",
      "000.000",
  };
  Draw(&g, 2, 2, pic, 7);
  MoveResult r = Claim(&g, 0, 5, 8);
  CHECK(r.captured == 25);
  CHECK(g.CellCount(1) == 0 && g.CellCount(0) == 49);
}

static void TestFullGame() {
  GameState g;
  InitGame(&g, 10, 10, 2);
  bool over = false;
  for (uint32_t y = 0; y < 10; ++y) {
    for (uint32_t x = 0; x < 10; ++x) {
      MoveResult r = g.TryClaimCell(x, y);
      if (r.gameOver) over = true;
    }
  }
  CHECK(over && g.IsGameOver());
  CHECK(g.FilledCells() == 100);
  CHECK(g.CellCount(0) + g.CellCount(1) == 100);
  CHECK(!g.TryClaimCell(0, 0).accepted);
  int order[kMaxPlayers];
  int top = g.Ranking(order);
  CHECK(top >= 1);
  CHECK(g.CellCount(order[0]) >= g.CellCount(order[1]));
}

// ---- Randomized comparison against a brute-force reference ---------------

static uint32_t g_rng = 12345;
static uint32_t Rand() {
  g_rng = g_rng * 1103515245u + 12345u;
  return g_rng >> 8;
}

// Reference: after player p moves, every 4-connected non-p component that
// does not touch the edge is captured (recomputed from scratch).
static void ReferenceCapture(signed char* b, int w, int h, int p) {
  static int stack[4096], comp[4096];
  static bool seen[4096];
  memset(seen, 0, sizeof(seen));
  for (int start = 0; start < w * h; ++start) {
    if (seen[start] || b[start] == p) continue;
    int sp = 0, n = 0;
    bool open = false;
    stack[sp++] = start;
    seen[start] = true;
    while (sp) {
      int c = stack[--sp];
      comp[n++] = c;
      int cx = c % w, cy = c / w;
      if (cx == 0 || cy == 0 || cx == w - 1 || cy == h - 1) open = true;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx && dy) continue;  // territory connects across sides only
          int nx = cx + dx, ny = cy + dy;
          if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
          int ni = ny * w + nx;
          if (seen[ni] || b[ni] == p) continue;
          seen[ni] = true;
          stack[sp++] = ni;
        }
    }
    if (!open)
      for (int i = 0; i < n; ++i) b[comp[i]] = (signed char)p;
  }
}

static void TestRandomAgainstReference() {
  int mismatches = 0;
  for (int game = 0; game < 400 && mismatches < 3; ++game) {
    int w = 10 + Rand() % 8, h = 10 + Rand() % 8;
    int players = 2 + Rand() % 3;
    GameState g;
    InitGame(&g, w, h, players);
    signed char ref[4096];
    memset(ref, -1, sizeof(ref));
    // Bias moves towards a few "home" areas so real enclosures happen.
    while (!g.IsGameOver()) {
      int p = g.CurrentPlayer();
      uint32_t x, y;
      int tries = 0;
      do {
        x = Rand() % w;
        y = Rand() % h;
      } while (g.Owner(x, y) >= 0 && ++tries < 50);
      if (g.Owner(x, y) >= 0) {
        for (y = 0; y < (uint32_t)h; ++y) {
          for (x = 0; x < (uint32_t)w; ++x)
            if (g.Owner(x, y) < 0) break;
          if (x < (uint32_t)w) break;
        }
      }
      MoveResult r = g.TryClaimCell(x, y);
      if (!r.accepted) {
        ++mismatches;
        break;
      }
      ref[y * w + x] = (signed char)p;
      ReferenceCapture(ref, w, h, p);
      bool same = true;
      for (int i = 0; i < w * h && same; ++i)
        same = g.Owner(i % w, i / w) == ref[i];
      if (!same) {
        printf("  mismatch in game %d at move (%u,%u) by %d\n", game, x, y,
               p);
        ++mismatches;
        break;
      }
    }
  }
  CHECK(mismatches == 0);
}

// ---- Large boards ---------------------------------------------------------

static LARGE_INTEGER Now() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t;
}

static DWORD Ms(LARGE_INTEGER a, LARGE_INTEGER b) {
  static LARGE_INTEGER freq;
  if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
  return (DWORD)((b.QuadPart - a.QuadPart) * 1000 / freq.QuadPart);
}

static void TestHugeBoard() {
  GameState g;
  InitGame(&g, 100000, 100000, 2);
  CHECK(g.TotalCells() == 10000000000ULL);

  LARGE_INTEGER t0 = Now();
  for (int i = 0; i < 20000; ++i) g.TryClaimCell(Rand() % 100000, Rand() % 100000);
  DWORD scattered = Ms(t0, Now());

  // Long wall with a gap; player 0 has cells all over the board, so the
  // bounding box shortcut cannot help and both sides walk up to the limit.
  for (uint32_t x = 40000; x < 44000; ++x)
    if (x != 42000) g.PlaceCell(x, 50000, 0);
  t0 = Now();
  MoveResult r = Claim(&g, 0, 42000, 50000);
  DWORD worst = Ms(t0, Now());
  CHECK(r.accepted && r.captured == 0);

  // Small pocket closed in the middle of the huge board.
  const char* pic[] = {"00000", "0...0", "0...0", "0...0", "00.00"};
  Draw(&g, 60000, 60000, pic, 5);
  t0 = Now();
  r = Claim(&g, 0, 60002, 60004);
  DWORD pocket = Ms(t0, Now());
  CHECK(r.captured == 9);

  // The same wall when the player's cells are local: resolved at once.
  GameState local;
  InitGame(&local, 1000000, 1000000, 2);
  for (uint32_t x = 400000; x < 404000; ++x)
    if (x != 402000) local.PlaceCell(x, 500000, 0);
  t0 = Now();
  r = Claim(&local, 0, 402000, 500000);
  DWORD gap = Ms(t0, Now());
  CHECK(r.accepted && r.captured == 0);

  printf("  huge board: 20000 scattered moves %lu ms, worst-case gap %lu ms, "
         "local gap %lu ms, pocket %lu ms\n",
         (unsigned long)scattered, (unsigned long)worst, (unsigned long)gap,
         (unsigned long)pocket);
  CHECK(worst < 1000 && gap < 20 && pocket < 20);
}

static void DrawRing(GameState* g, uint32_t x0, uint32_t y0, uint32_t size,
                     bool leaveGap) {
  uint32_t x1 = x0 + size - 1, y1 = y0 + size - 1;
  for (uint32_t x = x0; x <= x1; ++x) {
    g->PlaceCell(x, y0, 0);
    if (!(leaveGap && x == x0 + 1)) g->PlaceCell(x, y1, 0);
  }
  for (uint32_t y = y0 + 1; y < y1; ++y) {
    g->PlaceCell(x0, y, 0);
    g->PlaceCell(x1, y, 0);
  }
}

static void TestFloodFillLimit() {
  // Interior 300x300 = 90000 cells: below the limit, captured.
  GameState a;
  InitGame(&a, 1000, 1000, 2);
  DrawRing(&a, 100, 100, 302, true);
  MoveResult r = Claim(&a, 0, 101, 401);
  CHECK(r.captured == 90000);

  // Interior 500x500 = 250000 cells: above the limit, left alone.
  GameState b;
  InitGame(&b, 1000, 1000, 2);
  DrawRing(&b, 100, 100, 502, true);
  r = Claim(&b, 0, 101, 601);
  CHECK(r.accepted && r.captured == 0);
}

static void TestSaveLoad() {
  GameState g;
  PlayerInfo info[kMaxPlayers];
  memset(info, 0, sizeof(info));
  wcscpy(info[0].name, L"Гравець 1");
  wcscpy(info[1].name, L"Ärger ☺");
  wcscpy(info[2].name, L"");
  info[0].color = 0x0000FF;
  info[1].color = 0xFF0000;
  info[2].color = 0x00FF00;
  info[1].botLevel = kBotWeak;
  info[2].botLevel = kBotVeryStrong;
  CHECK(g.Init(123456, 77, 3, info));
  for (int i = 0; i < 5000; ++i) g.TryClaimCell(Rand() % 123456, Rand() % 77);

  wchar_t path[MAX_PATH];
  GetTempPathW(MAX_PATH, path);
  wcscat(path, L"feodals_test.feo");
  CHECK(SaveGame(path, g) == kSaveOk);

  GameState l;
  CHECK(LoadGame(path, &l) == kSaveOk);
  CHECK(l.Width() == 123456 && l.Height() == 77 && l.NumPlayers() == 3);
  CHECK(l.CurrentPlayer() == g.CurrentPlayer());
  CHECK(l.FilledCells() == g.FilledCells());
  for (int p = 0; p < 3; ++p) {
    CHECK(wcscmp(l.Player(p).name, g.Player(p).name) == 0);
    CHECK(l.Player(p).color == g.Player(p).color);
    CHECK(l.CellCount(p) == g.CellCount(p));
    CHECK(l.Player(p).botLevel == g.Player(p).botLevel);
  }
  const CellMap& cells = g.Cells();
  bool same = true;
  for (size_t i = 0; i < cells.Capacity(); ++i) {
    if (!cells.SlotUsed(i)) continue;
    uint64_t k = cells.SlotKey(i);
    if (l.Owner(CellKeyX(k), CellKeyY(k)) != cells.SlotValue(i)) same = false;
  }
  CHECK(same);

  // Corrupted file must be rejected without touching the target game.
  HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
  SetFilePointer(f, -3, 0, FILE_END);
  SetEndOfFile(f);
  CloseHandle(f);
  CHECK(LoadGame(path, &l) != kSaveOk);
  CHECK(l.Width() == 123456);
  DeleteFileW(path);
  CHECK(LoadGame(path, &l) == kSaveErrOpen);
}

// A version 1 file (no bot levels) still loads, everybody human.
static void TestLoadVersion1() {
  ByteBuffer b;
  b.Bytes("FEOD", 4);
  b.U16(1);
  b.U32(10);
  b.U32(12);
  b.U8(2);
  b.U8(1); b.U8('A'); b.U8(255); b.U8(0); b.U8(0);
  b.U8(1); b.U8('B'); b.U8(0); b.U8(0); b.U8(255);
  b.U8(1);   // current player
  b.U64(2);  // cells
  b.U32(3); b.U32(4); b.U8(0);
  b.U32(5); b.U32(6); b.U8(1);
  GameState g;
  CHECK(DeserializeGame(b.Data(), b.Size(), &g) == kSaveOk);
  CHECK(g.Width() == 10 && g.Height() == 12 && g.CurrentPlayer() == 1);
  CHECK(g.Owner(3, 4) == 0 && g.Owner(5, 6) == 1);
  CHECK(g.Player(0).botLevel == kBotHuman && g.Player(1).botLevel == kBotHuman);
  CHECK(!g.HasLastMove());
}

// ---- Computer players -------------------------------------------------------

static void InitBots(GameState* g, uint32_t w, uint32_t h, int bot0,
                     int bot1) {
  PlayerInfo info[kMaxPlayers];
  memset(info, 0, sizeof(info));
  info[0].color = 1;
  info[1].color = 2;
  info[0].botLevel = (uint8_t)bot0;
  info[1].botLevel = (uint8_t)bot1;
  CHECK(g->Init(w, h, 2, info));
}

static void TestEvaluateClaim() {
  GameState g;
  InitGame(&g, 10, 10, 2);
  const char* pic[] = {"000", "01.", "000"};
  Draw(&g, 3, 3, pic, 3);
  uint64_t opp = 99;
  CHECK(g.EvaluateClaim(5, 4, 0, &opp) == 1 && opp == 1);
  CHECK(g.Owner(4, 4) == 1 && g.Owner(5, 4) == -1);  // nothing changed
  CHECK(g.EvaluateClaim(5, 4, 1, &opp) == 0 && opp == 0);
  CHECK(g.EvaluateClaim(3, 3, 0, &opp) == 0);  // occupied
  CHECK(g.EvaluateClaim(8, 8, 0, 0) == 0);
}

static void TestBotPlaysLegalGames() {
  for (int game = 0; game < 30; ++game) {
    uint32_t w = 10 + game % 21, h = 10 + (game * 7) % 21;
    GameState g;
    InitBots(&g, w, h, kBotWeak, kBotWeak);
    Bot bot(1000 + game);
    uint64_t moves = 0;
    bool legal = true;
    while (!g.IsGameOver() && moves <= g.TotalCells()) {
      uint32_t x, y;
      if (!bot.ChooseMove(g, &x, &y) || g.Owner(x, y) >= 0) {
        legal = false;
        break;
      }
      legal = g.TryClaimCell(x, y).accepted;
      if (!legal) break;
      ++moves;
    }
    CHECK(legal && g.IsGameOver());
  }
}

static void TestBotTakesCaptures() {
  int taken = 0;
  for (int seed = 1; seed <= 100; ++seed) {
    GameState g;
    InitBots(&g, 20, 20, kBotWeak, kBotHuman);
    const char* pic[] = {"000", "01.", "000"};
    Draw(&g, 8, 8, pic, 3);
    g.SetCurrentPlayer(0);
    Bot bot(seed);
    uint32_t x, y;
    CHECK(bot.ChooseMove(g, &x, &y));
    if (x == 10 && y == 9) ++taken;
  }
  printf("  weak bot took an open capture in %d of 100 games\n", taken);
  CHECK(taken >= 60 && taken < 100);  // usually, but not always
}

static void TestBotBeatsRandomPlayer() {
  int wins = 0, games = 20;
  for (int game = 0; game < games; ++game) {
    GameState g;
    InitBots(&g, 20, 20, game % 2 ? kBotWeak : kBotHuman,
             game % 2 ? kBotHuman : kBotWeak);
    int botPlayer = game % 2 ? 0 : 1;
    Bot bot(77 + game);
    while (!g.IsGameOver()) {
      uint32_t x, y;
      if (g.CurrentPlayer() == botPlayer) {
        bot.ChooseMove(g, &x, &y);
      } else {
        do {
          x = Rand() % 20;
          y = Rand() % 20;
        } while (g.Owner(x, y) >= 0);
      }
      g.TryClaimCell(x, y);
    }
    if (g.CellCount(botPlayer) > g.CellCount(1 - botPlayer)) ++wins;
  }
  printf("  weak bot beat a random player in %d of %d games\n", wins, games);
  CHECK(wins >= games * 3 / 4);
}

static void TestBotOnHugeBoard() {
  GameState g;
  InitBots(&g, 1000000, 1000000, kBotWeak, kBotWeak);
  Bot bot(5);
  LARGE_INTEGER t0 = Now();
  for (int i = 0; i < 400; ++i) {
    uint32_t x, y;
    CHECK(bot.ChooseMove(g, &x, &y));
    CHECK(g.TryClaimCell(x, y).accepted);
  }
  DWORD ms = Ms(t0, Now());
  printf("  400 weak bot moves on 1000000x1000000: %lu ms\n",
         (unsigned long)ms);
  CHECK(ms < 4000);
}

// Plays a whole game between two levels (kBotHuman = random mover) and
// returns the winner (0/1) or -1 for a draw. `slowest` gets the longest move.
static int PlayMatch(uint32_t size, int level0, int level1, uint32_t seed,
                     DWORD* slowest) {
  GameState g;
  InitBots(&g, size, size, level0, level1);
  Bot bot(seed);
  while (!g.IsGameOver()) {
    uint32_t x, y;
    if (g.Player(g.CurrentPlayer()).botLevel == kBotHuman) {
      do {
        x = Rand() % size;
        y = Rand() % size;
      } while (g.Owner(x, y) >= 0);
    } else {
      LARGE_INTEGER t0 = Now();
      if (!bot.ChooseMove(g, &x, &y)) return -2;
      DWORD ms = Ms(t0, Now());
      if (slowest && ms > *slowest) *slowest = ms;
    }
    if (!g.TryClaimCell(x, y).accepted) return -2;
  }
  if (g.CellCount(0) == g.CellCount(1)) return -1;
  return g.CellCount(0) > g.CellCount(1) ? 0 : 1;
}

// The tactic from the playtest: the opponent hunts cells standing alone,
// surrounding them from four sides (four moves take one cell and win five),
// and otherwise just extends its own cells. Picks the free cell beside the
// bot cell that is surrounded the most already.
static void HunterMove(const GameState& g, int me, uint32_t* bx, uint32_t* by) {
  const CellMap& cells = g.Cells();
  int bestScore = -1;
  const int dx4[4] = {0, 1, 0, -1}, dy4[4] = {-1, 0, 1, 0};
  for (size_t i = 0; i < cells.Capacity(); ++i) {
    if (!cells.SlotUsed(i) || (int)cells.SlotValue(i) == me) continue;
    uint32_t x = CellKeyX(cells.SlotKey(i)), y = CellKeyY(cells.SlotKey(i));
    bool group = false;  // a cell with friends costs too much to surround
    for (int dy = -1; dy <= 1 && !group; ++dy) {
      for (int dx = -1; dx <= 1 && !group; ++dx) {
        int nx = (int)x + dx, ny = (int)y + dy;
        if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= (int)g.Width() ||
            ny >= (int)g.Height()) {
          continue;
        }
        group = g.Owner((uint32_t)nx, (uint32_t)ny) == (int)cells.SlotValue(i);
      }
    }
    if (group) continue;
    int taken = 0;
    uint32_t fx = 0, fy = 0;
    bool free = false;
    for (int d = 0; d < 4; ++d) {
      int nx = (int)x + dx4[d], ny = (int)y + dy4[d];
      if (nx < 0 || ny < 0 || nx >= (int)g.Width() || ny >= (int)g.Height()) {
        continue;
      }
      int owner = g.Owner((uint32_t)nx, (uint32_t)ny);
      if (owner == me) {
        ++taken;
      } else if (owner < 0) {
        free = true;
        fx = (uint32_t)nx;
        fy = (uint32_t)ny;
      }
    }
    if (free && taken > bestScore) {
      bestScore = taken;
      *bx = fx;
      *by = fy;
    }
  }
  if (bestScore >= 0) return;
  for (size_t i = 0; i < cells.Capacity(); ++i) {  // nothing to hunt: extend
    if (!cells.SlotUsed(i) || (int)cells.SlotValue(i) != me) continue;
    uint32_t x = CellKeyX(cells.SlotKey(i)), y = CellKeyY(cells.SlotKey(i));
    for (int d = 0; d < 4; ++d) {
      int nx = (int)x + dx4[d], ny = (int)y + dy4[d];
      if (nx < 0 || ny < 0 || nx >= (int)g.Width() || ny >= (int)g.Height()) {
        continue;
      }
      if (g.Owner((uint32_t)nx, (uint32_t)ny) < 0) {
        *bx = (uint32_t)nx;
        *by = (uint32_t)ny;
        return;
      }
    }
  }
  do {
    *bx = Rand() % g.Width();
    *by = Rand() % g.Height();
  } while (g.Owner(*bx, *by) >= 0);
}

// On a board far larger than the play, the strong bot must keep its cells
// together instead of sowing single ones the opponent can harvest.
static void TestKeepsCellsTogether() {
  GameState g;
  InitBots(&g, 60, 60, kBotHuman, kBotStrong);
  Bot bot(4242);
  for (int move = 0; move < 80 && !g.IsGameOver(); ++move) {
    uint32_t x = 0, y = 0;
    if (g.CurrentPlayer() == 1) {
      CHECK(bot.ChooseMove(g, &x, &y));
    } else {
      HunterMove(g, 0, &x, &y);
    }
    CHECK(g.TryClaimCell(x, y).accepted);
  }
  int alone = 0;
  const CellMap& cells = g.Cells();
  for (size_t i = 0; i < cells.Capacity(); ++i) {
    if (!cells.SlotUsed(i) || cells.SlotValue(i) != 1) continue;
    uint32_t x = CellKeyX(cells.SlotKey(i)), y = CellKeyY(cells.SlotKey(i));
    bool friends = false;
    for (int dy = -2; dy <= 2 && !friends; ++dy) {
      for (int dx = -2; dx <= 2 && !friends; ++dx) {
        int nx = (int)x + dx, ny = (int)y + dy;
        if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= 60 || ny >= 60) continue;
        friends = g.Owner((uint32_t)nx, (uint32_t)ny) == 1;
      }
    }
    if (!friends) ++alone;
  }
  printf("  strong bot against the lone-cell hunter: %lu cells to %lu, "
         "%d left standing alone\n",
         (unsigned long)g.CellCount(1), (unsigned long)g.CellCount(0), alone);
  CHECK(alone <= 5);  // the version before this one left 27 here
  CHECK(g.CellCount(1) >= g.CellCount(0));
}

// Best capture (enclosed cells + other players' cells in them) `player`
// could make in `g` right now, by brute force over the whole board.
static uint64_t BestCapture(const GameState& g, int player) {
  uint64_t best = 0;
  for (uint32_t y = 0; y < g.Height(); ++y) {
    for (uint32_t x = 0; x < g.Width(); ++x) {
      uint64_t others = 0;
      uint64_t captured = g.EvaluateClaim(x, y, player, &others);
      if (captured + others > best) best = captured + others;
    }
  }
  return best;
}

// Medium must find the best capture, and when threatened, a move after which
// the opponent's best capture is as small as possible (checked against a
// brute force over every free cell).
// `open` picks the position. The closed one (a ring with a gap around one
// cell) suits medium: its best defence by the one-move count is a move that
// only gets captured later, which the searching levels rightly avoid. The
// open one has a clear answer at any depth: a group of 6 whose only gap
// leads to its own free line - filling the gap saves all 6, anything else
// loses them.
static void TestBlocksAndCaptures(int level, const char* name, int seeds,
                                  bool open) {
  const char* closedPic[] = {"000", "01.", "000", "....", ".11."};
  const char* openPic[] = {"00000....", "0111.111.", "01110....", "00000....",
                           "........."};
  const char** pic = open ? openPic : closedPic;
  GameState base;
  InitBots(&base, 20, 20, kBotHuman, level);
  Draw(&base, 8, 8, pic, 5);

  // Defence: player 1 (medium) to move.
  uint64_t minLoss = ~0ULL;
  GameState after;
  uint64_t loss[20][20];
  for (uint32_t y = 0; y < 20; ++y) {
    for (uint32_t x = 0; x < 20; ++x) {
      loss[y][x] = ~0ULL;
      if (base.Owner(x, y) >= 0) continue;
      after.CopyFrom(base);
      after.SetCurrentPlayer(1);
      after.TryClaimCell(x, y);
      loss[y][x] = BestCapture(after, 0);
      if (loss[y][x] < minLoss) minLoss = loss[y][x];
    }
  }
  CHECK(BestCapture(base, 0) > minLoss);  // there is something to defend

  // Attack: the same picture with medium as player 0.
  GameState attack;
  InitBots(&attack, 20, 20, level, kBotHuman);
  Draw(&attack, 8, 8, pic, 5);
  attack.SetCurrentPlayer(0);
  uint64_t bestGain = BestCapture(attack, 0);
  CHECK(bestGain > 0);

  int defended = 0, captured = 0;
  for (int seed = 1; seed <= seeds; ++seed) {
    Bot bot(seed);
    uint32_t x, y;
    base.SetCurrentPlayer(1);
    CHECK(bot.ChooseMove(base, &x, &y));
    if (loss[y][x] == minLoss) ++defended;
    CHECK(bot.ChooseMove(attack, &x, &y));
    uint64_t others = 0;
    uint64_t gain = attack.EvaluateClaim(x, y, 0, &others);
    if (gain + others == bestGain) ++captured;
  }
  printf("  %s bot: best defence %d/%d, best capture %d/%d\n", name,
         defended, seeds, captured, seeds);
  CHECK(defended * 10 >= seeds * 9 && captured * 10 >= seeds * 9);
}

static void TestMediumStrength() {
  int beatWeak = 0, beatRandom = 0, games = 20;
  DWORD slowest = 0;
  for (int game = 0; game < games; ++game) {
    bool mediumFirst = game % 2 == 0;
    int winner = PlayMatch(20, mediumFirst ? kBotMedium : kBotWeak,
                           mediumFirst ? kBotWeak : kBotMedium, 300 + game,
                           &slowest);
    CHECK(winner != -2);
    if (winner == (mediumFirst ? 0 : 1)) ++beatWeak;
    winner = PlayMatch(20, mediumFirst ? kBotMedium : kBotHuman,
                       mediumFirst ? kBotHuman : kBotMedium, 500 + game,
                       &slowest);
    CHECK(winner != -2);
    if (winner == (mediumFirst ? 0 : 1)) ++beatRandom;
  }
  printf("  medium bot beat weak %d/%d, random %d/%d; slowest move %lu ms\n",
         beatWeak, games, beatRandom, games, (unsigned long)slowest);
  CHECK(beatWeak >= games * 7 / 10);
  CHECK(beatRandom == games);
  CHECK(slowest < 300);
}

// From the playtest: red (player 0) builds a wide ring, dots every other
// cell, around a mixed area, open on the right. The computer (blue) must not
// throw away moves deep inside the area about to be enclosed. A move at the
// ring, one that joins its own cells or one that attacks a red cell is fine
// - a cell dropped on its own in the middle is not.
static void TestRingIsNoticed() {
  int inside = 0, wasted = 0, games = 6;
  for (int seed = 1; seed <= games; ++seed) {
    GameState g;
    InitBots(&g, 30, 30, kBotHuman, kBotStrong);
    const int blue[][2] = {{14, 14}, {16, 14}, {15, 16}, {13, 16}, {17, 16}};
    const int red[][2] = {{15, 14}, {14, 16}, {16, 16}, {15, 12}};
    for (int i = 0; i < 5; ++i) g.PlaceCell(blue[i][0], blue[i][1], 1);
    for (int i = 0; i < 4; ++i) g.PlaceCell(red[i][0], red[i][1], 0);
    for (int i = 9; i <= 21; i += 2) {
      g.PlaceCell(i, 9, 0);
      g.PlaceCell(i, 21, 0);
      if (i != 9 && i != 21) g.PlaceCell(9, i, 0);
      if (i != 9 && i != 21 && (i < 13 || i > 19)) g.PlaceCell(21, i, 0);
    }
    g.SetCurrentPlayer(1);
    Bot bot(seed);
    uint32_t x, y;
    CHECK(bot.ChooseMove(g, &x, &y));
    if (x < 11 || x > 19 || y < 11 || y > 19) continue;  // at the wall: fine
    ++inside;
    bool useful = false;  // joins its own cells or attacks a red one
    for (int dy = -2; dy <= 2 && !useful; ++dy) {
      for (int dx = -2; dx <= 2 && !useful; ++dx) {
        int nx = (int)x + dx, ny = (int)y + dy;
        if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= 30 || ny >= 30) continue;
        int owner = g.Owner((uint32_t)nx, (uint32_t)ny);
        bool touching = dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
        useful = owner == 1 || (owner == 0 && touching);
      }
    }
    if (!useful) ++wasted;
  }
  printf("  strong bot inside a forming ring: %d/%d moves, %d of them idle\n",
         inside, games, wasted);
  CHECK(wasted == 0);
}

static void TestStrongStrength() {
  int beatMedium = 0, draws = 0, games = 12;
  DWORD slowest = 0;
  LARGE_INTEGER t0 = Now();
  for (int game = 0; game < games; ++game) {
    bool strongFirst = game % 2 == 0;
    int winner = PlayMatch(20, strongFirst ? kBotStrong : kBotMedium,
                           strongFirst ? kBotMedium : kBotStrong, 700 + game,
                           &slowest);
    CHECK(winner != -2);
    if (winner == (strongFirst ? 0 : 1)) ++beatMedium;
    if (winner == -1) ++draws;
  }
  printf("  strong bot beat medium %d/%d (draws %d); slowest move %lu ms, "
         "total %lu ms\n",
         beatMedium, games, draws, (unsigned long)slowest,
         (unsigned long)Ms(t0, Now()));
  CHECK(beatMedium >= games * 2 / 3);
  CHECK(slowest < 2000);

  DWORD big = 0;
  CHECK(PlayMatch(40, kBotStrong, kBotStrong, 21, &big) != -2);
  printf("  strong vs strong on 40x40: slowest move %lu ms\n",
         (unsigned long)big);
  CHECK(big < 3000);
}

// Very strong thinks for seconds, so only a short sanity match here; the
// strength was measured separately (18 of 20 games won against strong on
// 14x14).
static void TestVeryStrong() {
  DWORD slowest = 0;
  LARGE_INTEGER t0 = Now();
  int wins = 0;
  for (int game = 0; game < 2; ++game) {
    bool first = game == 0;
    int winner = PlayMatch(10, first ? kBotVeryStrong : kBotStrong,
                           first ? kBotStrong : kBotVeryStrong, 900 + game,
                           &slowest);
    CHECK(winner != -2);
    if (winner == (first ? 0 : 1)) ++wins;
  }
  printf("  very strong vs strong on 10x10: %d/2 won; slowest move %lu ms, "
         "total %lu ms\n",
         wins, (unsigned long)slowest, (unsigned long)Ms(t0, Now()));
  CHECK(slowest < 9000);

  // A cancel request stops the search at once, with a legal move.
  GameState g;
  InitBots(&g, 14, 14, kBotVeryStrong, kBotStrong);
  Bot helper(3);
  for (int i = 0; i < 20; ++i) {
    uint32_t x, y;
    helper.ChooseMove(g, &x, &y);
    g.TryClaimCell(x, y);
  }
  volatile long generation = 2;
  Bot bot(4);
  bot.SetCancel(&generation, 1);  // already outdated: stop immediately
  uint32_t x, y;
  t0 = Now();
  CHECK(bot.ChooseMove(g, &x, &y) && g.Owner(x, y) < 0);
  DWORD cancelled = Ms(t0, Now());
  printf("  cancelled very strong move: %lu ms\n", (unsigned long)cancelled);
  CHECK(cancelled < 1000);
}

static void TestMediumOnBigBoards() {
  DWORD slowest = 0;
  CHECK(PlayMatch(40, kBotMedium, kBotMedium, 9, &slowest) != -2);
  printf("  medium vs medium on 40x40: slowest move %lu ms\n",
         (unsigned long)slowest);
  CHECK(slowest < 500);

  GameState g;
  InitBots(&g, 1000000, 1000000, kBotMedium, kBotMedium);
  Bot bot(11);
  LARGE_INTEGER t0 = Now();
  for (int i = 0; i < 200; ++i) {
    uint32_t x, y;
    CHECK(bot.ChooseMove(g, &x, &y));
    CHECK(g.TryClaimCell(x, y).accepted);
  }
  DWORD ms = Ms(t0, Now());
  printf("  200 medium bot moves on 1000000x1000000: %lu ms\n",
         (unsigned long)ms);
  CHECK(ms < 10000);
}

int main() {
  TestBasicMove();
  TestSpecExample();
  TestEdgeIsNotWall();
  TestDiagonalWalls();
  TestMultipleOpponents();
  TestTwoPocketsAtOnce();
  TestNestedCapture();
  TestFullGame();
  TestRandomAgainstReference();
  TestHugeBoard();
  TestFloodFillLimit();
  TestSaveLoad();
  TestLoadVersion1();
  TestEvaluateClaim();
  TestBotPlaysLegalGames();
  TestBotTakesCaptures();
  TestBotBeatsRandomPlayer();
  TestBotOnHugeBoard();
  TestBlocksAndCaptures(kBotMedium, "medium", 30, false);
  TestBlocksAndCaptures(kBotStrong, "strong", 20, true);
  TestBlocksAndCaptures(kBotVeryStrong, "very strong", 4, true);
  TestMediumStrength();
  TestMediumOnBigBoards();
  TestRingIsNoticed();
  TestKeepsCellsTogether();
  TestStrongStrength();
  TestVeryStrong();
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
