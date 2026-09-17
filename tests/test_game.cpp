// Console tests for the game core and the save format.
// Build: tests\build_tests.bat (MinGW), then run build\test_game.exe.
#include <stdio.h>
#include <windows.h>

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
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
