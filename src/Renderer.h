// GDI rendering of the visible part of the board into a back buffer.
#pragma once

#include <windows.h>

#include "GameState.h"

const int kCellSize = 20;  // pixels, grid line included
const COLORREF kEmptyCellColor = RGB(255, 255, 255);

struct HoverCell {
  bool valid;
  uint32_t x, y;
};

// Full board size in pixels (cells plus the closing grid line). Fits in int
// for the largest supported board (1 000 000 * 20 + 1).
inline int BoardPixelWidth(const GameState& g) {
  return (int)g.Width() * kCellSize + 1;
}
inline int BoardPixelHeight(const GameState& g) {
  return (int)g.Height() * kCellSize + 1;
}

class Renderer {
 public:
  Renderer();
  ~Renderer();

  // Draws the part of the board visible through a client area of the given
  // size scrolled to (scrollX, scrollY). Cost depends on the window size
  // only, never on the board size. With `markLastMove` the cell of the last
  // move gets a contrasting frame.
  void Paint(HDC target, int width, int height, const GameState& game,
             int scrollX, int scrollY, const HoverCell& hover,
             bool markLastMove);

 private:
  Renderer(const Renderer&);
  void operator=(const Renderer&);

  bool EnsureBuffer(HDC target, int width, int height);
  HBRUSH PlayerBrush(const GameState& game, int player);

  HDC memDC_;
  HBITMAP bitmap_, oldBitmap_;
  int bufferW_, bufferH_;
  HBRUSH brushes_[kMaxPlayers];
  COLORREF brushColors_[kMaxPlayers];
  HBRUSH emptyBrush_, gridBrush_, outsideBrush_;
};
