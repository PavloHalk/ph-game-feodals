// Computer players. Pure game logic (no Win32), deterministic for a given
// seed, so it can be tested and later reused elsewhere.
//
// Levels (BotLevel in GameState.h):
//   weak    - claims cells next to the action and grabs captures when it sees
//             them, with a lot of randomness; no look-ahead, never defends
//             its own territory.
//   medium  - also blocks the next opponent's captures, looks one reply ahead
//             (its move, then the opponent's best capture), and now and then
//             sets up a fork (two capture threats at once).
//   strong, very strong - not implemented yet; they play like medium.
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

  // A move of some player that captures something right now.
  struct Capture {
    uint32_t x, y;
    int gain;  // enclosed cells plus other players' cells among them
  };

  bool ChooseWeak(const GameState& game, uint32_t* x, uint32_t* y);
  bool ChooseMedium(const GameState& game, uint32_t* x, uint32_t* y);

  bool Opening(const GameState& game, uint32_t* x, uint32_t* y);
  bool AnyFreeCell(const GameState& game, uint32_t* x, uint32_t* y);
  void GatherCandidates(const GameState& game, PodVec<Candidate>* out);
  void AddAround(const GameState& game, uint32_t cx, uint32_t cy, int radius,
                 CellMap* seen, PodVec<Candidate>* out);
  void FindCaptures(const GameState& game, int player, PodVec<Capture>* out);

  int StaticScore(const GameState& game, int player, uint32_t x, uint32_t y,
                  int* ownAround);
  int ScoreWeak(const GameState& game, int player, uint32_t x, uint32_t y);

  uint32_t Random();
  uint32_t Random(uint32_t range) { return range ? Random() % range : 0; }

  uint32_t state_;
};
