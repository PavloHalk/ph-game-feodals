#include "GameState.h"

namespace {

// The 8 neighbours in circular order: N, NE, E, SE, S, SW, W, NW.
const int kDX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int kDY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

int FindRoot(int* parent, int i) {
  while (parent[i] != i) i = parent[i] = parent[parent[i]];
  return i;
}

void ExtendBox(MoveResult* r, uint32_t x, uint32_t y) {
  if (x < r->minX) r->minX = x;
  if (x > r->maxX) r->maxX = x;
  if (y < r->minY) r->minY = y;
  if (y > r->maxY) r->maxY = y;
}

}  // namespace

GameState::GameState()
    : width_(0), height_(0), numPlayers_(0), current_(0), filled_(0),
      hasLastMove_(false), lastMoveX_(0), lastMoveY_(0) {
  memset(players_, 0, sizeof(players_));
  memset(counts_, 0, sizeof(counts_));
  memset(boundsMinX_, 0xFF, sizeof(boundsMinX_));
  memset(boundsMinY_, 0xFF, sizeof(boundsMinY_));
  memset(boundsMaxX_, 0, sizeof(boundsMaxX_));
  memset(boundsMaxY_, 0, sizeof(boundsMaxY_));
}

bool GameState::Init(uint32_t width, uint32_t height, int numPlayers,
                     const PlayerInfo* players) {
  if (width < kMinBoardSize || height < kMinBoardSize ||
      width > kMaxBoardSize || height > kMaxBoardSize ||
      numPlayers < kMinPlayers || numPlayers > kMaxPlayers) {
    return false;
  }
  cells_.Free();
  width_ = width;
  height_ = height;
  numPlayers_ = numPlayers;
  current_ = 0;
  filled_ = 0;
  hasLastMove_ = false;
  memset(players_, 0, sizeof(players_));
  memset(counts_, 0, sizeof(counts_));
  memset(boundsMinX_, 0xFF, sizeof(boundsMinX_));
  memset(boundsMinY_, 0xFF, sizeof(boundsMinY_));
  memset(boundsMaxX_, 0, sizeof(boundsMaxX_));
  memset(boundsMaxY_, 0, sizeof(boundsMaxY_));
  for (int i = 0; i < numPlayers; ++i) {
    players_[i] = players[i];
    players_[i].name[kMaxNameLen - 1] = 0;
  }
  return true;
}

MoveResult GameState::TryClaimCell(uint32_t x, uint32_t y) {
  MoveResult r;
  memset(&r, 0, sizeof(r));
  if (!width_ || x >= width_ || y >= height_ || IsGameOver()) return r;

  uint64_t key = MakeCellKey(x, y);
  if (cells_.Get(key) >= 0) return r;  // own or foreign cell: ignored

  uint8_t player = (uint8_t)current_;
  if (cells_.Set(key, player) == -2) return r;  // out of memory
  ++counts_[player];
  ++filled_;
  AddToBounds(player, x, y);

  r.accepted = true;
  SetLastMove(x, y);
  r.minX = r.maxX = x;
  r.minY = r.maxY = y;
  ResolveEncirclement(x, y, player, &r);

  r.gameOver = IsGameOver();
  if (!r.gameOver) current_ = (current_ + 1) % numPlayers_;
  return r;
}

// Capture check after `player` claimed (cx, cy). See spec 9.1.
//
// Like dots on paper, player cells touching by a corner form a wall: walls
// are 8-connected, so the enclosed territory is 4-connected (across cell
// sides only). The outside of the board counts as open space.
//
// Claiming a cell can only create a new enclosed region if it splits the
// surrounding non-player territory. The side neighbours are grouped by
// connectivity through the ring of 8 neighbours; with fewer than two groups
// nothing can be enclosed and no flood fill runs at all (the common case on
// big boards).
//
// Otherwise every group is flood-filled in parallel (round robin). Groups
// that meet are merged; a group touching the board edge is open. A group that
// is exhausted without touching the edge is enclosed and gets captured. Since
// the territory around the clicked cell was open before the move, once only
// one unresolved group is left and none was found open yet, that group must
// be the open one, so the search stops without walking the huge outer region.
//
// Two shortcuts keep the check cheap on huge boards: a region reaching outside
// the player's bounding box is open (a straight line to the edge crosses no
// player cell), and a region growing past kFloodFillLimit is treated as open.
//
// (cx, cy) counts as the player's cell even if it is still empty, so the same
// code answers "what would this move capture?" without changing the state.
void GameState::CollectEnclosed(uint32_t cx, uint32_t cy, uint8_t player,
                                PodVec<uint64_t>* out) const {
  const uint64_t claimed = MakeCellKey(cx, cy);
  uint32_t bx0 = boundsMinX_[player], bx1 = boundsMaxX_[player];
  uint32_t by0 = boundsMinY_[player], by1 = boundsMaxY_[player];
  if (cx < bx0) bx0 = cx;
  if (cx > bx1) bx1 = cx;
  if (cy < by0) by0 = cy;
  if (cy > by1) by1 = cy;
  bool nonPlayer[8], inside[8], edge[8];
  uint64_t keys[8];
  for (int i = 0; i < 8; ++i) {
    int64_t nx = (int64_t)cx + kDX[i];
    int64_t ny = (int64_t)cy + kDY[i];
    inside[i] = nx >= 0 && ny >= 0 && nx < width_ && ny < height_;
    if (inside[i]) {
      keys[i] = MakeCellKey((uint32_t)nx, (uint32_t)ny);
      nonPlayer[i] = cells_.Get(keys[i]) != player;
      edge[i] = nx == 0 || ny == 0 || nx == width_ - 1 ||
                ny == height_ - 1 || nx < bx0 || nx > bx1 || ny < by0 ||
                ny > by1;
    } else {
      keys[i] = 0;
      nonPlayer[i] = true;
      edge[i] = true;
    }
  }

  // Ring positions next to each other in circular order share a side; all
  // positions outside the board are connected through the outer space.
  int group[8];
  for (int i = 0; i < 8; ++i) group[i] = i;
  for (int i = 0; i < 8; ++i) {
    if (!nonPlayer[i]) continue;
    for (int j = i + 1; j < 8; ++j) {
      if (!nonPlayer[j]) continue;
      bool adjacent = (!inside[i] && !inside[j]) || j == i + 1 ||
                      (i == 0 && j == 7);
      if (adjacent) group[FindRoot(group, j)] = FindRoot(group, i);
    }
  }
  // Only side neighbours (even indices) are part of the split territory.
  bool seenRoot[8] = {false};
  int groups = 0;
  for (int i = 0; i < 8; i += 2) {
    if (!nonPlayer[i]) continue;
    int root = FindRoot(group, i);
    if (!seenRoot[root]) {
      seenRoot[root] = true;
      ++groups;
    }
  }
  if (groups < 2) return;

  // Flood fill ids are the local group roots (0..7), merged via `parent`.
  CellMap visited;  // cell -> id of the group that reached it first
  PodVec<uint64_t> queue[8];
  size_t head[8] = {0};
  int parent[8];
  size_t regionSize[8] = {0};  // cells per merged region (valid at roots)
  bool used[8] = {false}, open[8] = {false};
  for (int g = 0; g < 8; ++g) parent[g] = g;

  bool aborted = false;
  for (int i = 0; i < 8 && !aborted; i += 2) {
    if (!nonPlayer[i]) continue;
    int g = FindRoot(group, i);
    used[g] = true;
    if (edge[i]) open[g] = true;
    if (!inside[i]) continue;
    if (visited.Set(keys[i], (uint8_t)g) == -2 || !queue[g].Push(keys[i])) {
      aborted = true;
    }
    ++regionSize[g];
  }

  while (!aborted) {
    bool anyOpen = false, rootLive[8] = {false};
    for (int g = 0; g < 8; ++g) {
      if (!used[g]) continue;
      int r = FindRoot(parent, g);
      if (open[r]) {
        anyOpen = true;
      } else if (head[g] < queue[g].Size()) {
        rootLive[r] = true;
      }
    }
    int live = 0;
    for (int r = 0; r < 8; ++r) live += rootLive[r];
    if (live == 0 || (live == 1 && !anyOpen)) break;

    for (int g = 0; g < 8 && !aborted; ++g) {
      if (!used[g]) continue;
      for (int step = 0; step < 256 && !aborted; ++step) {
        int r = FindRoot(parent, g);
        if (open[r] || head[g] >= queue[g].Size()) break;
        uint64_t key = queue[g][head[g]++];
        int64_t x = CellKeyX(key), y = CellKeyY(key);
        for (int d = 0; d < 8; d += 2) {  // side neighbours only
          int64_t nx = x + kDX[d], ny = y + kDY[d];
          if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) {
            open[r] = true;
            continue;
          }
          uint64_t nkey = MakeCellKey((uint32_t)nx, (uint32_t)ny);
          if (nkey == claimed || cells_.Get(nkey) == player) continue;
          int seen = visited.Get(nkey);
          if (seen >= 0) {
            int other = FindRoot(parent, seen);
            if (other != r) {
              parent[other] = r;
              regionSize[r] += regionSize[other];
              if (open[other]) open[r] = true;
            }
            continue;
          }
          if (visited.Set(nkey, (uint8_t)g) == -2 || !queue[g].Push(nkey)) {
            aborted = true;  // out of memory: no capture this move
            break;
          }
          // Touching the edge, leaving the player's bounding box or growing
          // past the safety limit means open.
          if (nx == 0 || ny == 0 || nx == width_ - 1 || ny == height_ - 1 ||
              nx < bx0 || nx > bx1 || ny < by0 || ny > by1 ||
              ++regionSize[r] > kFloodFillLimit) {
            open[r] = true;
          }
        }
      }
    }
  }

  if (aborted) return;  // out of memory: nothing is captured

  // A region is captured only if it is closed and was completely explored.
  bool capture[8] = {false};
  for (int r = 0; r < 8; ++r) {
    if (!used[r] || FindRoot(parent, r) != r || open[r]) continue;
    bool exhausted = true;
    for (int g = 0; g < 8; ++g) {
      if (used[g] && FindRoot(parent, g) == r && head[g] < queue[g].Size()) {
        exhausted = false;
      }
    }
    capture[r] = exhausted;
  }

  for (int g = 0; g < 8; ++g) {
    if (!used[g] || !capture[FindRoot(parent, g)]) continue;
    for (size_t k = 0; k < queue[g].Size(); ++k) {
      if (!out->Push(queue[g][k])) return;
    }
  }
}

void GameState::ResolveEncirclement(uint32_t cx, uint32_t cy, uint8_t player,
                                    MoveResult* result) {
  PodVec<uint64_t> enclosed;
  CollectEnclosed(cx, cy, player, &enclosed);
  if (!enclosed.Size()) return;

  size_t newEntries = 0;
  for (size_t k = 0; k < enclosed.Size(); ++k) {
    if (cells_.Get(enclosed[k]) < 0) ++newEntries;
  }
  if (!cells_.Reserve(cells_.Count() + newEntries)) return;

  for (size_t k = 0; k < enclosed.Size(); ++k) {
    uint64_t key = enclosed[k];
    int old = cells_.Set(key, player);
    if (old >= 0) {
      --counts_[old];
    } else {
      ++filled_;
    }
    ++counts_[player];
    ++result->captured;
    ExtendBox(result, CellKeyX(key), CellKeyY(key));
  }
}

uint64_t GameState::EvaluateClaim(uint32_t x, uint32_t y, int player,
                                  uint64_t* opponentCells) const {
  if (opponentCells) *opponentCells = 0;
  if (x >= width_ || y >= height_ || player < 0 || player >= numPlayers_ ||
      cells_.Get(MakeCellKey(x, y)) >= 0) {
    return 0;
  }
  PodVec<uint64_t> enclosed;
  CollectEnclosed(x, y, (uint8_t)player, &enclosed);
  if (opponentCells) {
    for (size_t k = 0; k < enclosed.Size(); ++k) {
      if (cells_.Get(enclosed[k]) >= 0) ++*opponentCells;
    }
  }
  return enclosed.Size();
}

bool GameState::PlaceCell(uint32_t x, uint32_t y, int owner) {
  if (x >= width_ || y >= height_ || owner < 0 || owner >= numPlayers_) {
    return false;
  }
  if (cells_.Set(MakeCellKey(x, y), (uint8_t)owner) != -1) return false;
  ++counts_[owner];
  ++filled_;
  AddToBounds(owner, x, y);
  return true;
}

void GameState::AddToBounds(int player, uint32_t x, uint32_t y) {
  if (x < boundsMinX_[player]) boundsMinX_[player] = x;
  if (x > boundsMaxX_[player]) boundsMaxX_[player] = x;
  if (y < boundsMinY_[player]) boundsMinY_[player] = y;
  if (y > boundsMaxY_[player]) boundsMaxY_[player] = y;
}

bool GameState::ReserveCells(uint64_t count) {
  if (count > (uint64_t)((size_t)-1) / 16) return false;
  return cells_.Reserve((size_t)count);
}

bool GameState::SetCurrentPlayer(int player) {
  if (player < 0 || player >= numPlayers_) return false;
  current_ = player;
  return true;
}

bool GameState::SetPlayerInfo(int player, const PlayerInfo& info) {
  if (player < 0 || player >= numPlayers_) return false;
  players_[player] = info;
  players_[player].name[kMaxNameLen - 1] = 0;
  return true;
}

void GameState::Swap(GameState& other) {
  uint32_t u;
  u = width_; width_ = other.width_; other.width_ = u;
  u = height_; height_ = other.height_; other.height_ = u;
  int i;
  i = numPlayers_; numPlayers_ = other.numPlayers_; other.numPlayers_ = i;
  i = current_; current_ = other.current_; other.current_ = i;
  uint64_t f = filled_; filled_ = other.filled_; other.filled_ = f;
  bool b = hasLastMove_; hasLastMove_ = other.hasLastMove_; other.hasLastMove_ = b;
  u = lastMoveX_; lastMoveX_ = other.lastMoveX_; other.lastMoveX_ = u;
  u = lastMoveY_; lastMoveY_ = other.lastMoveY_; other.lastMoveY_ = u;
  for (int p = 0; p < kMaxPlayers; ++p) {
    PlayerInfo info = players_[p];
    players_[p] = other.players_[p];
    other.players_[p] = info;
    uint64_t c = counts_[p];
    counts_[p] = other.counts_[p];
    other.counts_[p] = c;
    u = boundsMinX_[p]; boundsMinX_[p] = other.boundsMinX_[p]; other.boundsMinX_[p] = u;
    u = boundsMinY_[p]; boundsMinY_[p] = other.boundsMinY_[p]; other.boundsMinY_[p] = u;
    u = boundsMaxX_[p]; boundsMaxX_[p] = other.boundsMaxX_[p]; other.boundsMaxX_[p] = u;
    u = boundsMaxY_[p]; boundsMaxY_[p] = other.boundsMaxY_[p]; other.boundsMaxY_[p] = u;
  }
  cells_.Swap(other.cells_);
}

int GameState::Ranking(int order[kMaxPlayers]) const {
  for (int i = 0; i < numPlayers_; ++i) {
    int j = i;
    while (j > 0 && counts_[order[j - 1]] < counts_[i]) {
      order[j] = order[j - 1];
      --j;
    }
    order[j] = i;
  }
  int top = 0;
  while (top < numPlayers_ && counts_[order[top]] == counts_[order[0]]) ++top;
  return top;
}
