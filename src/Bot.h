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
//   strong  - alpha-beta search four plies deep over the most promising moves,
//             evaluating cells, pending captures and capture threats (so it
//             sets up forks and avoids traps several moves ahead).
//   very strong - the same search with iterative deepening up to 8 plies,
//             wider, with a much larger budget and a capture-only extension
//             at the leaves (several seconds per move).
#pragma once

#include "GameState.h"

class Bot {
 public:
  explicit Bot(uint32_t seed = 0x2545F491u);
  void Seed(uint32_t seed) { state_ = seed ? seed : 0x9E3779B9u; }

  // Lets another thread stop a long search: ChooseMove returns its best
  // move so far as soon as *flag != value.
  void SetCancel(const volatile long* flag, long value) {
    cancelFlag_ = flag;
    cancelValue_ = value;
  }

  // Chooses a free cell for the current player of `game` according to that
  // player's bot level. Returns false only if the board has no free cell.
  bool ChooseMove(const GameState& game, uint32_t* x, uint32_t* y);

 private:
  struct Candidate {
    uint32_t x, y;
    int score;
    int gain;  // what the move captures (search nodes only)
  };

  // A move of some player that captures something right now.
  struct Capture {
    uint32_t x, y;
    int gain;  // enclosed cells plus other players' cells among them
  };

  // Figures gathered while ordering the moves of a search node, reused by
  // the static evaluation.
  struct NodeStats {
    int moverBest;     // best capture of the player to move
    int moverThreats;  // number of capturing cells of the player to move
    int nextBest;      // best capture of the player after
    int nextThreats;
  };
  struct SearchContext;

  bool ChooseWeak(const GameState& game, uint32_t* x, uint32_t* y);
  bool ChooseMedium(const GameState& game, uint32_t* x, uint32_t* y);
  bool ChooseStrong(const GameState& game, uint32_t* x, uint32_t* y);
  bool ChooseVeryStrong(const GameState& game, uint32_t* x, uint32_t* y);
  bool ChooseBySearch(const GameState& game, uint32_t* x, uint32_t* y,
                      const void* params);
  bool Cancelled() const;

  // Candidate moves of a search node, best first (at most `limit`).
  int OrderMoves(const GameState& state, SearchContext* ctx, Candidate* out,
                 int limit, NodeStats* stats);
  int Evaluate(const GameState& state, const SearchContext& ctx,
               const NodeStats& stats);
  int Search(const GameState& state, int depth, int ply, int alpha, int beta,
             SearchContext* ctx);

  // Candidates ranked by static score, what they capture now and what they
  // block (captures of others); the best `limit` go to `top`.
  int RankOnePly(const GameState& game, const PodVec<Candidate>& candidates,
                 Candidate* top, int limit);

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
  const volatile long* cancelFlag_;
  long cancelValue_;
};
