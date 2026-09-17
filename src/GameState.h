// Pure game logic: board, players, turn order, moves and encirclement capture.
// Has no Win32 dependency so it can later be driven by a network layer.
#pragma once

#include <stdint.h>
#include <wchar.h>

#include "Containers.h"

const int kMinPlayers = 2;
const int kMaxPlayers = 8;
const int kMaxNameLen = 32;  // including the terminating zero
const uint32_t kMinBoardSize = 10;
const uint32_t kMaxBoardSize = 1000000;

// Safety limit for encirclement checks (spec, section 10): a region whose
// flood fill grows past this many cells is treated as open (not captured).
const uint32_t kFloodFillLimit = 100000;

struct PlayerInfo {
  wchar_t name[kMaxNameLen];
  uint32_t color;  // 0x00BBGGRR, same layout as COLORREF
};

struct MoveResult {
  bool accepted;
  bool gameOver;
  uint64_t captured;  // cells taken by encirclement (not counting the click)
  // Bounding box of every cell changed by the move (valid if accepted).
  uint32_t minX, minY, maxX, maxY;
};

class GameState {
 public:
  GameState();

  // Starts a new empty game. Returns false on invalid parameters.
  bool Init(uint32_t width, uint32_t height, int numPlayers,
            const PlayerInfo* players);

  // A move of the current player. Rejected for occupied or out-of-range cells
  // and after the game is over.
  MoveResult TryClaimCell(uint32_t x, uint32_t y);

  // Used when restoring a saved game: sets an unclaimed cell without running
  // capture logic. Returns false for invalid input or an occupied cell.
  bool PlaceCell(uint32_t x, uint32_t y, int owner);
  bool ReserveCells(uint64_t count);
  bool SetCurrentPlayer(int player);
  // Renames/recolors a player (network lobby); cells are not touched.
  bool SetPlayerInfo(int player, const PlayerInfo& info);

  void Swap(GameState& other);

  uint32_t Width() const { return width_; }
  uint32_t Height() const { return height_; }
  int NumPlayers() const { return numPlayers_; }
  int CurrentPlayer() const { return current_; }
  const PlayerInfo& Player(int i) const { return players_[i]; }
  uint64_t CellCount(int player) const { return counts_[player]; }
  uint64_t FilledCells() const { return filled_; }
  uint64_t TotalCells() const { return (uint64_t)width_ * height_; }
  bool IsGameOver() const { return width_ != 0 && filled_ == TotalCells(); }
  const CellMap& Cells() const { return cells_; }

  // Cell clicked by the most recent accepted move of this session. Not part
  // of the save format: a loaded game starts without one.
  bool HasLastMove() const { return hasLastMove_; }
  uint32_t LastMoveX() const { return lastMoveX_; }
  uint32_t LastMoveY() const { return lastMoveY_; }
  void SetLastMove(uint32_t x, uint32_t y) {
    hasLastMove_ = x < width_ && y < height_;
    lastMoveX_ = x;
    lastMoveY_ = y;
  }

  // Owner index or -1 for an unclaimed cell.
  int Owner(uint32_t x, uint32_t y) const {
    return cells_.Get(MakeCellKey(x, y));
  }

  // Fills `order` with player indices sorted by cell count (descending,
  // stable). Returns how many players share the top score.
  int Ranking(int order[kMaxPlayers]) const;

 private:
  GameState(const GameState&);
  void operator=(const GameState&);

  void ResolveEncirclement(uint32_t x, uint32_t y, uint8_t player,
                           MoveResult* result);
  void AddToBounds(int player, uint32_t x, uint32_t y);

  uint32_t width_;
  uint32_t height_;
  int numPlayers_;
  int current_;
  PlayerInfo players_[kMaxPlayers];
  uint64_t counts_[kMaxPlayers];
  uint64_t filled_;
  bool hasLastMove_;
  uint32_t lastMoveX_, lastMoveY_;
  // Bounding box of every cell each player ever owned (a superset of the
  // current cells). Anything outside it can reach the board edge freely.
  uint32_t boundsMinX_[kMaxPlayers], boundsMinY_[kMaxPlayers];
  uint32_t boundsMaxX_[kMaxPlayers], boundsMaxY_[kMaxPlayers];
  CellMap cells_;
};
