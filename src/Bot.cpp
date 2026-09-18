#include "Bot.h"

namespace {

// Neighbourhood used to pick candidate cells around claimed ones.
const int kCandidateRadius = 2;
// Caps that keep a move fast on huge boards.
const size_t kMaxAnchors = 3000;
const size_t kMaxCandidates = 4000;

// Weak player's sloppiness.
const uint32_t kMissCapturePercent = 15;  // does not notice a capture
const uint32_t kRandomPickPercent = 20;   // plays one of the top moves at random
const int kTopMoves = 8;
const int kNoise = 6;

const int kDX4[4] = {0, 1, 0, -1};
const int kDY4[4] = {-1, 0, 1, 0};
const int kDXd[4] = {1, 1, -1, -1};
const int kDYd[4] = {-1, 1, 1, -1};

}  // namespace

Bot::Bot(uint32_t seed) : state_(seed ? seed : 0x9E3779B9u) {}

uint32_t Bot::Random() {
  // xorshift32
  uint32_t x = state_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state_ = x;
  return x;
}

bool Bot::ChooseMove(const GameState& game, uint32_t* x, uint32_t* y) {
  if (!game.Width() || game.IsGameOver()) return false;
  // Only the weak level exists so far; stronger levels play like it until
  // they are written (the new-game dialog does not offer them yet).
  return ChooseWeak(game, x, y);
}

bool Bot::AnyFreeCell(const GameState& game, uint32_t* x, uint32_t* y) {
  for (int tries = 0; tries < 2000; ++tries) {
    uint32_t cx = Random(game.Width()), cy = Random(game.Height());
    if (game.Owner(cx, cy) < 0) {
      *x = cx;
      *y = cy;
      return true;
    }
  }
  // Nearly full board: scan (only reachable on boards small enough to fill).
  for (uint32_t cy = 0; cy < game.Height(); ++cy) {
    for (uint32_t cx = 0; cx < game.Width(); ++cx) {
      if (game.Owner(cx, cy) < 0) {
        *x = cx;
        *y = cy;
        return true;
      }
    }
  }
  return false;
}

void Bot::AddAround(const GameState& game, uint32_t cx, uint32_t cy,
                    int radius, CellMap* seen, PodVec<Candidate>* out) {
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      int64_t nx = (int64_t)cx + dx, ny = (int64_t)cy + dy;
      if (nx < 0 || ny < 0 || nx >= game.Width() || ny >= game.Height()) {
        continue;
      }
      uint64_t key = MakeCellKey((uint32_t)nx, (uint32_t)ny);
      if (game.Cells().Get(key) >= 0 || seen->Get(key) >= 0) continue;
      if (out->Size() >= kMaxCandidates || seen->Set(key, 1) == -2) return;
      Candidate c = {(uint32_t)nx, (uint32_t)ny, 0};
      out->Push(c);
    }
  }
}

// Plain local heuristic, no look-ahead:
//  + captures (more for opponents' cells),
//  + cells touching opponents (walls around them),
//  + cells touching both own and opponent cells (extending an attack),
//  - cells deep inside own territory.
int Bot::ScoreWeak(const GameState& game, int player, uint32_t x, uint32_t y) {
  int ownSide = 0, ownDiag = 0, oppSide = 0, oppDiag = 0;
  for (int d = 0; d < 4; ++d) {
    int64_t nx = (int64_t)x + kDX4[d], ny = (int64_t)y + kDY4[d];
    if (nx >= 0 && ny >= 0 && nx < game.Width() && ny < game.Height()) {
      int owner = game.Owner((uint32_t)nx, (uint32_t)ny);
      if (owner == player) ++ownSide;
      else if (owner >= 0) ++oppSide;
    }
    nx = (int64_t)x + kDXd[d];
    ny = (int64_t)y + kDYd[d];
    if (nx >= 0 && ny >= 0 && nx < game.Width() && ny < game.Height()) {
      int owner = game.Owner((uint32_t)nx, (uint32_t)ny);
      if (owner == player) ++ownDiag;
      else if (owner >= 0) ++oppDiag;
    }
  }

  int score = 1 + 3 * oppSide + 2 * oppDiag + ownSide + ownDiag;
  if (ownSide + ownDiag > 0 && oppSide + oppDiag > 0) score += 6;
  if (oppSide + oppDiag == 0 && ownSide + ownDiag >= 5) score -= 4;

  // A capture needs at least two own cells around the new one.
  if (ownSide + ownDiag >= 2 && Random(100) >= kMissCapturePercent) {
    uint64_t opponentCells = 0;
    uint64_t captured = game.EvaluateClaim(x, y, player, &opponentCells);
    if (captured) {
      if (captured > 1000) captured = 1000;
      if (opponentCells > 1000) opponentCells = 1000;
      score += 40 + (int)captured + 5 * (int)opponentCells;
    }
  }
  return score + (int)Random(kNoise);
}

bool Bot::ChooseWeak(const GameState& game, uint32_t* x, uint32_t* y) {
  int player = game.CurrentPlayer();
  const CellMap& cells = game.Cells();

  if (!cells.Count()) {  // opening move: somewhere near the middle
    uint32_t cx = game.Width() / 2, cy = game.Height() / 2;
    int64_t nx = (int64_t)cx + (int)Random(7) - 3;
    int64_t ny = (int64_t)cy + (int)Random(7) - 3;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx >= game.Width()) nx = game.Width() - 1;
    if (ny >= game.Height()) ny = game.Height() - 1;
    *x = (uint32_t)nx;
    *y = (uint32_t)ny;
    return true;
  }

  // Candidates: free cells around claimed ones (a random sample of them on
  // crowded boards), always including the area of the last move.
  CellMap seen;
  PodVec<Candidate> candidates;
  if (game.HasLastMove()) {
    AddAround(game, game.LastMoveX(), game.LastMoveY(), kCandidateRadius,
              &seen, &candidates);
  }
  size_t capacity = cells.Capacity();
  if (cells.Count() <= kMaxAnchors) {
    for (size_t i = 0; i < capacity; ++i) {
      if (!cells.SlotUsed(i)) continue;
      uint64_t key = cells.SlotKey(i);
      AddAround(game, CellKeyX(key), CellKeyY(key), kCandidateRadius, &seen,
                &candidates);
    }
  } else {
    for (size_t n = 0; n < kMaxAnchors; ++n) {
      size_t i = Random((uint32_t)capacity);
      while (!cells.SlotUsed(i)) i = (i + 1) % capacity;
      uint64_t key = cells.SlotKey(i);
      AddAround(game, CellKeyX(key), CellKeyY(key), kCandidateRadius, &seen,
                &candidates);
    }
  }
  if (!candidates.Size()) return AnyFreeCell(game, x, y);

  // Keep the best few, then usually play the best one.
  Candidate top[kTopMoves];
  int numTop = 0;
  for (size_t i = 0; i < candidates.Size(); ++i) {
    Candidate c = candidates[i];
    c.score = ScoreWeak(game, player, c.x, c.y);
    int pos = numTop;
    while (pos > 0 && top[pos - 1].score < c.score) --pos;
    if (pos >= kTopMoves) continue;
    int last = numTop < kTopMoves ? numTop : kTopMoves - 1;
    for (int j = last; j > pos; --j) top[j] = top[j - 1];
    top[pos] = c;
    if (numTop < kTopMoves) ++numTop;
  }

  int pick = 0;
  if (Random(100) < kRandomPickPercent) pick = (int)Random((uint32_t)numTop);
  *x = top[pick].x;
  *y = top[pick].y;
  return true;
}
