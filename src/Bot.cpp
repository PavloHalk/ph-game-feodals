#include "Bot.h"

namespace {

// Neighbourhood used to pick candidate cells around claimed ones.
const int kCandidateRadius = 2;
// Caps that keep a move fast on huge boards.
const size_t kMaxAnchors = 3000;
const size_t kMaxCandidates = 4000;
const size_t kMaxCaptureChecks = 20000;

// Weak player's sloppiness.
const uint32_t kMissCapturePercent = 15;  // does not notice a capture
const uint32_t kRandomPickPercent = 20;   // plays one of the top moves at random
const int kTopMoves = 8;
const int kNoise = 6;

// Medium player.
const int kDeepMoves = 16;             // candidates checked one reply ahead
const size_t kMaxDeepCells = 60000;    // above this: no look-ahead (too slow)
// Cells won or lost outweigh the positional heuristic (which stays below ~25).
const int kGainWeight = 30;            // per cell won by the move
const int kReplyWeight = 28;           // per cell the opponent wins in reply
const int kThreatWeight = 25;          // blocking a capture (1-ply estimate)
const uint32_t kForkPercent = 50;      // how often a fork is valued at all
const uint32_t kSecondBestPercent = 5; // small chance of a slightly worse move

const int kDX4[4] = {0, 1, 0, -1};
const int kDY4[4] = {-1, 0, 1, 0};
const int kDXd[4] = {1, 1, -1, -1};
const int kDYd[4] = {-1, 1, 1, -1};

// Inserts `c` into `top` (sorted by score, descending, at most `limit`).
template <typename T>
void InsertTop(T* top, int* count, int limit, const T& c) {
  int pos = *count;
  while (pos > 0 && top[pos - 1].score < c.score) --pos;
  if (pos >= limit) return;
  int last = *count < limit ? *count : limit - 1;
  for (int j = last; j > pos; --j) top[j] = top[j - 1];
  top[pos] = c;
  if (*count < limit) ++*count;
}

int Clamp(uint64_t v, int limit) { return v > (uint64_t)limit ? limit : (int)v; }

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
  if (game.Player(game.CurrentPlayer()).botLevel == kBotWeak) {
    return ChooseWeak(game, x, y);
  }
  // Strong levels are not written yet: they play like the medium one.
  return ChooseMedium(game, x, y);
}

// ---- Shared helpers -----------------------------------------------------------

bool Bot::Opening(const GameState& game, uint32_t* x, uint32_t* y) {
  int64_t nx = (int64_t)(game.Width() / 2) + (int)Random(7) - 3;
  int64_t ny = (int64_t)(game.Height() / 2) + (int)Random(7) - 3;
  if (nx < 0) nx = 0;
  if (ny < 0) ny = 0;
  if (nx >= game.Width()) nx = game.Width() - 1;
  if (ny >= game.Height()) ny = game.Height() - 1;
  *x = (uint32_t)nx;
  *y = (uint32_t)ny;
  return true;
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

// Free cells around claimed ones (a random sample of them on crowded
// boards), always including the area of the last move.
void Bot::GatherCandidates(const GameState& game, PodVec<Candidate>* out) {
  const CellMap& cells = game.Cells();
  CellMap seen;
  if (game.HasLastMove()) {
    AddAround(game, game.LastMoveX(), game.LastMoveY(), kCandidateRadius,
              &seen, out);
  }
  size_t capacity = cells.Capacity();
  if (cells.Count() <= kMaxAnchors) {
    for (size_t i = 0; i < capacity; ++i) {
      if (!cells.SlotUsed(i)) continue;
      uint64_t key = cells.SlotKey(i);
      AddAround(game, CellKeyX(key), CellKeyY(key), kCandidateRadius, &seen,
                out);
    }
  } else {
    for (size_t n = 0; n < kMaxAnchors; ++n) {
      size_t i = Random((uint32_t)capacity);
      while (!cells.SlotUsed(i)) i = (i + 1) % capacity;
      uint64_t key = cells.SlotKey(i);
      AddAround(game, CellKeyX(key), CellKeyY(key), kCandidateRadius, &seen,
                out);
    }
  }
}

// Every free cell where `player` would capture something now. A capture
// needs at least two of the player's cells around the new one, which rules
// out almost all cells before the (more expensive) encirclement check.
void Bot::FindCaptures(const GameState& game, int player,
                       PodVec<Capture>* out) {
  const CellMap& cells = game.Cells();
  CellMap seen;
  size_t checks = 0;
  for (size_t i = 0; i < cells.Capacity() && checks < kMaxCaptureChecks; ++i) {
    if (!cells.SlotUsed(i) || cells.SlotValue(i) != player) continue;
    uint32_t px = CellKeyX(cells.SlotKey(i)), py = CellKeyY(cells.SlotKey(i));
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        int64_t nx = (int64_t)px + dx, ny = (int64_t)py + dy;
        if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= game.Width() ||
            ny >= game.Height()) {
          continue;
        }
        uint64_t key = MakeCellKey((uint32_t)nx, (uint32_t)ny);
        if (cells.Get(key) >= 0 || seen.Get(key) >= 0) continue;
        seen.Set(key, 1);
        int own = 0;
        for (int ry = -1; ry <= 1; ++ry) {
          for (int rx = -1; rx <= 1; ++rx) {
            int64_t ax = nx + rx, ay = ny + ry;
            if ((rx || ry) && ax >= 0 && ay >= 0 && ax < game.Width() &&
                ay < game.Height() &&
                game.Owner((uint32_t)ax, (uint32_t)ay) == player) {
              ++own;
            }
          }
        }
        if (own < 2) continue;
        ++checks;
        uint64_t others = 0;
        uint64_t captured =
            game.EvaluateClaim((uint32_t)nx, (uint32_t)ny, player, &others);
        if (!captured) continue;
        Capture c = {(uint32_t)nx, (uint32_t)ny,
                     Clamp(captured + others, 100000)};
        out->Push(c);
      }
    }
  }
}

// Plain local heuristic:
//  + cells touching opponents (walls around them),
//  + cells touching both own and opponent cells (extending an attack),
//  - cells deep inside own territory.
int Bot::StaticScore(const GameState& game, int player, uint32_t x, uint32_t y,
                     int* ownAround) {
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
  if (ownAround) *ownAround = ownSide + ownDiag;
  int score = 1 + 3 * oppSide + 2 * oppDiag + ownSide + ownDiag;
  if (ownSide + ownDiag > 0 && oppSide + oppDiag > 0) score += 6;
  if (oppSide + oppDiag == 0 && ownSide + ownDiag >= 5) score -= 4;
  return score;
}

// ---- Weak -------------------------------------------------------------------

// Static heuristic plus captures it happens to notice, plus noise.
int Bot::ScoreWeak(const GameState& game, int player, uint32_t x, uint32_t y) {
  int ownAround = 0;
  int score = StaticScore(game, player, x, y, &ownAround);
  if (ownAround >= 2 && Random(100) >= kMissCapturePercent) {
    uint64_t opponentCells = 0;
    uint64_t captured = game.EvaluateClaim(x, y, player, &opponentCells);
    if (captured) {
      score += 40 + Clamp(captured, 1000) + 5 * Clamp(opponentCells, 1000);
    }
  }
  return score + (int)Random(kNoise);
}

bool Bot::ChooseWeak(const GameState& game, uint32_t* x, uint32_t* y) {
  if (!game.Cells().Count()) return Opening(game, x, y);
  int player = game.CurrentPlayer();
  PodVec<Candidate> candidates;
  GatherCandidates(game, &candidates);
  if (!candidates.Size()) return AnyFreeCell(game, x, y);

  // Keep the best few, then usually play the best one.
  Candidate top[kTopMoves];
  int numTop = 0;
  for (size_t i = 0; i < candidates.Size(); ++i) {
    Candidate c = candidates[i];
    c.score = ScoreWeak(game, player, c.x, c.y);
    InsertTop(top, &numTop, kTopMoves, c);
  }
  int pick = 0;
  if (Random(100) < kRandomPickPercent) pick = (int)Random((uint32_t)numTop);
  *x = top[pick].x;
  *y = top[pick].y;
  return true;
}

// ---- Medium -----------------------------------------------------------------

// 1. Every candidate gets a quick score: static heuristic, what it captures
//    now, and how much it blocks (cells where an opponent would capture).
// 2. The best few are played out on a copy of the board: what the move wins,
//    minus the best capture the next player can answer with, plus a bonus
//    for new capture threats of our own (a fork when there are two).
bool Bot::ChooseMedium(const GameState& game, uint32_t* x, uint32_t* y) {
  if (!game.Cells().Count()) return Opening(game, x, y);
  int me = game.CurrentPlayer();
  int next = (me + 1) % game.NumPlayers();

  PodVec<Candidate> candidates;
  GatherCandidates(game, &candidates);
  if (!candidates.Size()) return AnyFreeCell(game, x, y);

  // Where others would capture now (the next player counts fully, the rest
  // half), and where we would.
  CellMap threat, mine;  // cell -> gain, capped at 250
  for (int p = 0; p < game.NumPlayers(); ++p) {
    PodVec<Capture> captures;
    FindCaptures(game, p, &captures);
    for (size_t i = 0; i < captures.Size(); ++i) {
      uint64_t key = MakeCellKey(captures[i].x, captures[i].y);
      int gain = captures[i].gain;
      if (p == me) {
        mine.Set(key, (uint8_t)(gain > 250 ? 250 : gain));
        continue;
      }
      if (p != next) gain /= 2;
      if (gain > 250) gain = 250;
      if (gain > threat.Get(key)) threat.Set(key, (uint8_t)gain);
    }
  }

  Candidate top[kDeepMoves];
  int numTop = 0;
  for (size_t i = 0; i < candidates.Size(); ++i) {
    Candidate c = candidates[i];
    uint64_t key = MakeCellKey(c.x, c.y);
    int own = mine.Get(key), blocked = threat.Get(key);
    c.score = StaticScore(game, me, c.x, c.y, 0) +
              kGainWeight * (own > 0 ? own : 0) +
              kThreatWeight * (blocked > 0 ? blocked : 0);
    InsertTop(top, &numTop, kDeepMoves, c);
  }

  // One reply of look-ahead on the best candidates.
  if (game.Cells().Count() <= kMaxDeepCells) {
    GameState after;
    for (int i = 0; i < numTop; ++i) {
      if (!after.CopyFrom(game)) break;
      MoveResult r = after.TryClaimCell(top[i].x, top[i].y);
      if (!r.accepted) {
        top[i].score = -1000000;
        continue;
      }
      // Cells won: captured ones plus those taken away from others.
      uint64_t lost = 0;
      for (int p = 0; p < game.NumPlayers(); ++p) {
        if (p != me && game.CellCount(p) > after.CellCount(p)) {
          lost += game.CellCount(p) - after.CellCount(p);
        }
      }
      int gain = Clamp(r.captured + lost, 100000);

      int reply = 0;
      int threats = 0, bestThreat = 0, secondThreat = 0;
      if (!after.IsGameOver()) {
        PodVec<Capture> answers;
        FindCaptures(after, next, &answers);
        for (size_t k = 0; k < answers.Size(); ++k) {
          if (answers[k].gain > reply) reply = answers[k].gain;
        }
        PodVec<Capture> own;
        FindCaptures(after, me, &own);
        for (size_t k = 0; k < own.Size(); ++k) {
          ++threats;
          int g = own[k].gain;
          if (g > bestThreat) {
            secondThreat = bestThreat;
            bestThreat = g;
          } else if (g > secondThreat) {
            secondThreat = g;
          }
        }
      }

      int score = StaticScore(game, me, top[i].x, top[i].y, 0) +
                  kGainWeight * gain - kReplyWeight * reply +
                  3 * (bestThreat > 10 ? 10 : bestThreat);
      // A fork: two threats, the opponent can block only one.
      if (threats >= 2 && Random(100) < kForkPercent) {
        score += 10 + 20 * (secondThreat > 5 ? 5 : secondThreat);
      }
      top[i].score = score + (int)Random(3);
    }
  }

  int best = 0, second = -1;
  for (int i = 1; i < numTop; ++i) {
    if (top[i].score > top[best].score) {
      second = best;
      best = i;
    } else if (second < 0 || top[i].score > top[second].score) {
      second = i;
    }
  }
  int pick = best;
  if (second >= 0 && Random(100) < kSecondBestPercent &&
      top[second].score > -1000000) {
    pick = second;
  }
  *x = top[pick].x;
  *y = top[pick].y;
  return true;
}
