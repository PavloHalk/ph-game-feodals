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
  switch (game.Player(game.CurrentPlayer()).botLevel) {
    case kBotWeak:
      return ChooseWeak(game, x, y);
    case kBotMedium:
      return ChooseMedium(game, x, y);
    default:  // very strong is not written yet: it plays like strong
      return ChooseStrong(game, x, y);
  }
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

int Bot::RankOnePly(const GameState& game,
                    const PodVec<Candidate>& candidates, Candidate* top,
                    int limit) {
  int me = game.CurrentPlayer();
  int next = (me + 1) % game.NumPlayers();
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

  int numTop = 0;
  for (size_t i = 0; i < candidates.Size(); ++i) {
    Candidate c = candidates[i];
    uint64_t key = MakeCellKey(c.x, c.y);
    int own = mine.Get(key), blocked = threat.Get(key);
    c.score = StaticScore(game, me, c.x, c.y, 0) +
              kGainWeight * (own > 0 ? own : 0) +
              kThreatWeight * (blocked > 0 ? blocked : 0);
    InsertTop(top, &numTop, limit, c);
  }

  return numTop;
}


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

  Candidate top[kDeepMoves];
  int numTop = RankOnePly(game, candidates, top, kDeepMoves);

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

// ---- Strong -----------------------------------------------------------------

namespace {

const int kStrongDepth = 4;            // plies: me, reply, me, reply
const int kStrongRootBeam = 12;        // moves tried at the root
const int kStrongInnerBeam = 6;        // moves tried at deeper nodes
const int kStrongNodeBudget = 4000;    // positions played out per move
const int kStrongPool = 48;            // root candidates reused in the search
const size_t kMaxStrongCells = 5000;   // above: too slow, plays like medium
const int kWin = 1000000;
const int kInfinity = 2000000000;
const int kMaxPath = 8;

// Evaluation weights (per cell).
const int kCellValue = 10;       // a cell owned now
const int kPendingValue = 8;     // capture the player to move will make
const int kThreatValue = 3;      // each capturing cell (forks count double)
// An empty cell closer to us than to others, as a divisor: that many such
// cells are worth one point (a cell owned now is worth kCellValue points).
// (Chosen by 40-game matches against medium: none 31, 1/4 34, 1/8 36,
// 1/16 34 wins.)
const int kInfluenceDivisor = 8;
const int kInfluenceMargin = 3;  // cells around the action taken into account
const uint64_t kInfluenceMaxArea = 4096;

}  // namespace

struct Bot::SearchContext {
  int me;
  int nodes;
  int budget;
  PodVec<uint64_t> pool;  // root candidates, still relevant deeper down
  uint64_t path[kMaxPath];
  int pathLen;
};

namespace {

int OwnAround(const GameState& s, int player, uint32_t x, uint32_t y) {
  int own = 0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      int64_t nx = (int64_t)x + dx, ny = (int64_t)y + dy;
      if ((dx || dy) && nx >= 0 && ny >= 0 && nx < s.Width() &&
          ny < s.Height() && s.Owner((uint32_t)nx, (uint32_t)ny) == player) {
        ++own;
      }
    }
  }
  return own;
}

// Cells of `me` minus the strongest other player (paranoid view).
int64_t Material(const GameState& s, int me) {
  uint64_t other = 0;
  for (int p = 0; p < s.NumPlayers(); ++p) {
    if (p != me && s.CellCount(p) > other) other = s.CellCount(p);
  }
  return (int64_t)s.CellCount(me) - (int64_t)other;
}

// Territory estimate: empty cells around the action (bounding box of claimed
// cells plus a margin) that are closer to `me` than to anyone else, minus
// those closer to another player. Distance counts steps across cell sides
// through empty cells, from all claimed cells at once (multi-source BFS).
int Influence(const GameState& s, int me) {
  const CellMap& cells = s.Cells();
  uint32_t x0 = 0xFFFFFFFFu, y0 = 0xFFFFFFFFu, x1 = 0, y1 = 0;
  for (size_t i = 0; i < cells.Capacity(); ++i) {
    if (!cells.SlotUsed(i)) continue;
    uint32_t x = CellKeyX(cells.SlotKey(i)), y = CellKeyY(cells.SlotKey(i));
    if (x < x0) x0 = x;
    if (x > x1) x1 = x;
    if (y < y0) y0 = y;
    if (y > y1) y1 = y;
  }
  if (x0 > x1) return 0;
  x0 = x0 >= (uint32_t)kInfluenceMargin ? x0 - kInfluenceMargin : 0;
  y0 = y0 >= (uint32_t)kInfluenceMargin ? y0 - kInfluenceMargin : 0;
  x1 = x1 + kInfluenceMargin < s.Width() ? x1 + kInfluenceMargin
                                           : s.Width() - 1;
  y1 = y1 + kInfluenceMargin < s.Height() ? y1 + kInfluenceMargin
                                            : s.Height() - 1;
  uint32_t w = x1 - x0 + 1, h = y1 - y0 + 1;
  if ((uint64_t)w * h > kInfluenceMaxArea) return 0;

  int area = (int)(w * h);
  signed char* owner = (signed char*)malloc(area);
  int* queue = (int*)malloc(area * sizeof(int));
  if (!owner || !queue) {
    free(owner);
    free(queue);
    return 0;
  }
  const signed char kUnseen = -1, kContested = -2;
  memset(owner, kUnseen, area);
  unsigned short* dist = (unsigned short*)malloc(area * sizeof(unsigned short));
  if (!dist) {
    free(owner);
    free(queue);
    return 0;
  }
  int head = 0, tail = 0;
  for (int i = 0; i < area; ++i) {
    int o = s.Owner(x0 + i % w, y0 + i / w);
    if (o >= 0) {
      owner[i] = (signed char)o;
      dist[i] = 0;
      queue[tail++] = i;
    }
  }
  int balance = 0;
  while (head < tail) {
    int i = queue[head++];
    int cx = i % w, cy = i / w;
    for (int d = 0; d < 4; ++d) {
      int nx = cx + kDX4[d], ny = cy + kDY4[d];
      if (nx < 0 || ny < 0 || nx >= (int)w || ny >= (int)h) continue;
      int j = ny * (int)w + nx;
      if (owner[j] == kUnseen) {
        owner[j] = owner[i];
        dist[j] = (unsigned short)(dist[i] + 1);
        queue[tail++] = j;
      } else if (dist[j] == dist[i] + 1 && owner[j] != owner[i]) {
        owner[j] = kContested;  // equally close to two players
      }
    }
  }
  for (int i = 0; i < area; ++i) {
    if (dist[i] == 0 || owner[i] < 0) continue;
    balance += owner[i] == me ? 1 : -1;
  }
  free(owner);
  free(queue);
  free(dist);
  return balance;
}

int ClampScore(int64_t v) {
  if (v > kWin / 2) return kWin / 2;
  if (v < -kWin / 2) return -kWin / 2;
  return (int)v;
}

}  // namespace

// Candidates of a node: the root pool plus the area around the moves played
// in the search so far. Captures and blocks are checked on these cells only,
// which keeps a node cheap; threats elsewhere were already in the pool.
int Bot::OrderMoves(const GameState& s, SearchContext* ctx, Candidate* out,
                    int limit, NodeStats* stats) {
  memset(stats, 0, sizeof(*stats));
  int mover = s.CurrentPlayer();
  int next = (mover + 1) % s.NumPlayers();

  CellMap seen;
  PodVec<Candidate> cands;
  for (size_t i = 0; i < ctx->pool.Size(); ++i) {
    uint64_t key = ctx->pool[i];
    if (s.Cells().Get(key) >= 0 || seen.Get(key) >= 0) continue;
    seen.Set(key, 1);
    Candidate c = {CellKeyX(key), CellKeyY(key), 0};
    cands.Push(c);
  }
  for (int i = 0; i < ctx->pathLen; ++i) {
    AddAround(s, CellKeyX(ctx->path[i]), CellKeyY(ctx->path[i]),
              kCandidateRadius, &seen, &cands);
  }

  int count = 0;
  for (size_t i = 0; i < cands.Size(); ++i) {
    Candidate c = cands[i];
    int own = 0;
    int score = StaticScore(s, mover, c.x, c.y, &own);
    if (own >= 2) {
      uint64_t others = 0;
      uint64_t captured = s.EvaluateClaim(c.x, c.y, mover, &others);
      if (captured) {
        int gain = Clamp(captured + others, 100000);
        score += kGainWeight * gain;
        ++stats->moverThreats;
        if (gain > stats->moverBest) stats->moverBest = gain;
      }
    }
    if (OwnAround(s, next, c.x, c.y) >= 2) {
      uint64_t others = 0;
      uint64_t captured = s.EvaluateClaim(c.x, c.y, next, &others);
      if (captured) {
        int gain = Clamp(captured + others, 100000);
        score += kThreatWeight * gain;
        ++stats->nextThreats;
        if (gain > stats->nextBest) stats->nextBest = gain;
      }
    }
    c.score = score;
    if (limit > 0) InsertTop(out, &count, limit, c);
  }
  return count;
}

// Static value of a position for ctx.me: cells, the capture the player to
// move is about to make, and the balance of capture threats (two or more
// threats against one reply is a fork).
int Bot::Evaluate(const GameState& s, const SearchContext& ctx,
                  const NodeStats& stats) {
  int64_t v = kCellValue * Material(s, ctx.me) +
              Influence(s, ctx.me) / kInfluenceDivisor;
  int mover = s.CurrentPlayer();
  int next = (mover + 1) % s.NumPlayers();
  if (mover == ctx.me) {
    v += kPendingValue * stats.moverBest;
    v += kThreatValue * (stats.moverThreats - stats.nextThreats);
  } else {
    v -= kPendingValue * stats.moverBest + kThreatValue * stats.moverThreats;
    if (next == ctx.me) {
      v += kThreatValue * stats.nextThreats;
      // A fork survives the opponent's reply: the second threat stays.
      if (stats.nextThreats >= 2) v += kPendingValue * 2;
    }
  }
  return ClampScore(v);
}

int Bot::Search(const GameState& s, int depth, int alpha, int beta,
                SearchContext* ctx) {
  if (s.IsGameOver()) {
    int64_t diff = Material(s, ctx->me);
    if (diff > 0) return kWin + ClampScore(diff);
    if (diff < 0) return -kWin + ClampScore(diff);
    return 0;
  }
  NodeStats stats;
  Candidate moves[kStrongInnerBeam];
  bool leaf = depth <= 0 || ctx->nodes >= ctx->budget;
  int n = OrderMoves(s, ctx, moves, leaf ? 0 : kStrongInnerBeam, &stats);
  if (leaf || !n) return Evaluate(s, *ctx, stats);

  bool maximize = s.CurrentPlayer() == ctx->me;
  int best = maximize ? -kInfinity : kInfinity;
  GameState child;
  for (int i = 0; i < n; ++i) {
    if (!child.CopyFrom(s)) break;
    if (!child.TryClaimCell(moves[i].x, moves[i].y).accepted) continue;
    ++ctx->nodes;
    bool pushed = ctx->pathLen < kMaxPath;
    if (pushed) ctx->path[ctx->pathLen++] = MakeCellKey(moves[i].x, moves[i].y);
    int v = Search(child, depth - 1, alpha, beta, ctx);
    if (pushed) --ctx->pathLen;
    if (maximize) {
      if (v > best) best = v;
      if (best > alpha) alpha = best;
    } else {
      if (v < best) best = v;
      if (best < beta) beta = best;
    }
    if (alpha >= beta) break;
  }
  if (best == kInfinity || best == -kInfinity) return Evaluate(s, *ctx, stats);
  return best;
}

bool Bot::ChooseStrong(const GameState& game, uint32_t* x, uint32_t* y) {
  if (!game.Cells().Count()) return Opening(game, x, y);
  if (game.Cells().Count() > kMaxStrongCells) return ChooseMedium(game, x, y);

  PodVec<Candidate> candidates;
  GatherCandidates(game, &candidates);
  if (!candidates.Size()) return AnyFreeCell(game, x, y);

  Candidate pool[kStrongPool];
  int poolSize = RankOnePly(game, candidates, pool, kStrongPool);

  SearchContext ctx;
  ctx.me = game.CurrentPlayer();
  ctx.nodes = 0;
  ctx.budget = kStrongNodeBudget;
  ctx.pathLen = 0;
  for (int i = 0; i < poolSize; ++i) {
    ctx.pool.Push(MakeCellKey(pool[i].x, pool[i].y));
  }

  int rootMoves = poolSize < kStrongRootBeam ? poolSize : kStrongRootBeam;
  int alpha = -kInfinity;
  int bestIndex = 0, bestValue = -kInfinity;
  GameState child;
  for (int i = 0; i < rootMoves; ++i) {
    if (!child.CopyFrom(game)) break;
    if (!child.TryClaimCell(pool[i].x, pool[i].y).accepted) continue;
    ++ctx.nodes;
    ctx.path[0] = MakeCellKey(pool[i].x, pool[i].y);
    ctx.pathLen = 1;
    int v = Search(child, kStrongDepth - 1, alpha, kInfinity, &ctx);
    // Equal values: keep some variety between games.
    if (v > bestValue || (v == bestValue && Random(2))) {
      bestValue = v;
      bestIndex = i;
    }
    if (v > alpha) alpha = v;
  }
  *x = pool[bestIndex].x;
  *y = pool[bestIndex].y;
  return true;
}
