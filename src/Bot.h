// Computer players. Pure game logic (no Win32), deterministic for a given
// seed, so it can be tested and later reused elsewhere.
//
// Levels (BotLevel in GameState.h):
//   weak        - claims cells next to the action and grabs captures when it
//                 sees them, with a lot of randomness; no look-ahead, never
//                 defends its own territory.
//   medium, strong, very strong - not implemented yet; they fall back to
//                 the weak player.
#pragma once

#include "GameState.h"

class Bot {
 public:
  explicit Bot(uint32_t seed = 0x2545F491u);
  void Seed(uint32_t seed) { state_ = seed ? seed : 0x9E3779B9u; }

  // Chooses a free cell for the current player of `game` according to that
  // player's bot level. Returns false only if the board has no free cell.
  bool ChooseMove(const GameState& game, uint32_t* x, uint32_t* y);

 private:
  struct Candidate {
    uint32_t x, y;
    int score;
  };

  bool ChooseWeak(const GameState& game, uint32_t* x, uint32_t* y);
  bool AnyFreeCell(const GameState& game, uint32_t* x, uint32_t* y);
  void AddAround(const GameState& game, uint32_t cx, uint32_t cy, int radius,
                 CellMap* seen, PodVec<Candidate>* out);
  int ScoreWeak(const GameState& game, int player, uint32_t x, uint32_t y);
  uint32_t Random();
  uint32_t Random(uint32_t range) { return range ? Random() % range : 0; }

  uint32_t state_;
};
