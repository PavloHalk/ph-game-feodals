#include "Renderer.h"

namespace {

const COLORREF kGridColor = RGB(214, 214, 214);
const COLORREF kOutsideColor = RGB(96, 100, 108);

COLORREF Blend(COLORREF a, COLORREF b, int percentA) {
  int pb = 100 - percentA;
  return RGB((GetRValue(a) * percentA + GetRValue(b) * pb) / 100,
             (GetGValue(a) * percentA + GetGValue(b) * pb) / 100,
             (GetBValue(a) * percentA + GetBValue(b) * pb) / 100);
}

}  // namespace

Renderer::Renderer()
    : memDC_(0), bitmap_(0), oldBitmap_(0), bufferW_(0), bufferH_(0) {
  for (int i = 0; i < kMaxPlayers; ++i) {
    brushes_[i] = 0;
    brushColors_[i] = 0;
  }
  emptyBrush_ = CreateSolidBrush(kEmptyCellColor);
  gridBrush_ = CreateSolidBrush(kGridColor);
  outsideBrush_ = CreateSolidBrush(kOutsideColor);
}

Renderer::~Renderer() {
  if (memDC_) {
    SelectObject(memDC_, oldBitmap_);
    DeleteDC(memDC_);
  }
  if (bitmap_) DeleteObject(bitmap_);
  for (int i = 0; i < kMaxPlayers; ++i) {
    if (brushes_[i]) DeleteObject(brushes_[i]);
  }
  DeleteObject(emptyBrush_);
  DeleteObject(gridBrush_);
  DeleteObject(outsideBrush_);
}

bool Renderer::EnsureBuffer(HDC target, int width, int height) {
  if (!memDC_) {
    memDC_ = CreateCompatibleDC(target);
    if (!memDC_) return false;
  }
  if (width <= bufferW_ && height <= bufferH_) return true;
  if (width < bufferW_) width = bufferW_;
  if (height < bufferH_) height = bufferH_;
  HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
  if (!bitmap) return false;
  HGDIOBJ old = SelectObject(memDC_, bitmap);
  if (bitmap_) {
    DeleteObject(bitmap_);
  } else {
    oldBitmap_ = (HBITMAP)old;
  }
  bitmap_ = bitmap;
  bufferW_ = width;
  bufferH_ = height;
  return true;
}

HBRUSH Renderer::PlayerBrush(const GameState& game, int player) {
  COLORREF color = game.Player(player).color;
  if (!brushes_[player] || brushColors_[player] != color) {
    if (brushes_[player]) DeleteObject(brushes_[player]);
    brushes_[player] = CreateSolidBrush(color);
    brushColors_[player] = color;
  }
  return brushes_[player];
}

void Renderer::Paint(HDC target, int width, int height, const GameState& game,
                     int scrollX, int scrollY, const HoverCell& hover,
                     bool markLastMove) {
  if (width <= 0 || height <= 0) return;
  if (!EnsureBuffer(target, width, height)) return;
  HDC dc = memDC_;

  RECT all = {0, 0, width, height};
  FillRect(dc, &all, outsideBrush_);

  if (game.Width()) {
    // Visible part of the board in client coordinates.
    RECT board = {-scrollX, -scrollY, BoardPixelWidth(game) - scrollX,
                  BoardPixelHeight(game) - scrollY};
    RECT visible;
    if (IntersectRect(&visible, &board, &all)) {
      FillRect(dc, &visible, emptyBrush_);

      uint32_t x0 = (uint32_t)((visible.left + scrollX) / kCellSize);
      uint32_t y0 = (uint32_t)((visible.top + scrollY) / kCellSize);
      uint32_t x1 = (uint32_t)((visible.right - 1 + scrollX) / kCellSize);
      uint32_t y1 = (uint32_t)((visible.bottom - 1 + scrollY) / kCellSize);
      if (x1 >= game.Width()) x1 = game.Width() - 1;
      if (y1 >= game.Height()) y1 = game.Height() - 1;

      // Grid lines: one on the left/top of every cell plus the closing ones.
      HGDIOBJ oldBrush = SelectObject(dc, gridBrush_);
      int lineH = visible.bottom - visible.top;
      int lineW = visible.right - visible.left;
      for (uint32_t x = x0; x <= x1 + 1; ++x) {
        PatBlt(dc, (int)x * kCellSize - scrollX, visible.top, 1, lineH,
               PATCOPY);
      }
      for (uint32_t y = y0; y <= y1 + 1; ++y) {
        PatBlt(dc, visible.left, (int)y * kCellSize - scrollY, lineW, 1,
               PATCOPY);
      }
      SelectObject(dc, oldBrush);

      bool showHover = hover.valid && !game.IsGameOver() &&
                       hover.x >= x0 && hover.x <= x1 && hover.y >= y0 &&
                       hover.y <= y1 && game.Owner(hover.x, hover.y) < 0;

      for (uint32_t y = y0; y <= y1; ++y) {
        for (uint32_t x = x0; x <= x1; ++x) {
          int owner = game.Owner(x, y);
          if (owner < 0) continue;
          RECT cell = {(int)x * kCellSize - scrollX + 1,
                       (int)y * kCellSize - scrollY + 1,
                       (int)(x + 1) * kCellSize - scrollX,
                       (int)(y + 1) * kCellSize - scrollY};
          FillRect(dc, &cell, PlayerBrush(game, owner));
        }
      }

      if (showHover) {
        COLORREF color = game.Player(game.CurrentPlayer()).color;
        HBRUSH tint = CreateSolidBrush(Blend(color, kEmptyCellColor, 35));
        RECT cell = {(int)hover.x * kCellSize - scrollX + 1,
                     (int)hover.y * kCellSize - scrollY + 1,
                     (int)(hover.x + 1) * kCellSize - scrollX,
                     (int)(hover.y + 1) * kCellSize - scrollY};
        FillRect(dc, &cell, tint);
        DeleteObject(tint);
      }

      if (markLastMove && game.HasLastMove()) {
        uint32_t lx = game.LastMoveX(), ly = game.LastMoveY();
        // Visible (with a cell of margin, the frame reaches over the grid).
        if (lx + 1 >= x0 && lx <= x1 + 1 && ly + 1 >= y0 && ly <= y1 + 1) {
          int left = (int)lx * kCellSize - scrollX;
          int top = (int)ly * kCellSize - scrollY;
          // 2 px black ring over the grid lines, 1 px white ring inside it:
          // stands out on light and dark player colors alike.
          RECT ring = {left - 1, top - 1, left + kCellSize + 2,
                       top + kCellSize + 2};
          HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
          FrameRect(dc, &ring, black);
          InflateRect(&ring, -1, -1);
          FrameRect(dc, &ring, black);
          InflateRect(&ring, -1, -1);
          FrameRect(dc, &ring, (HBRUSH)GetStockObject(WHITE_BRUSH));
        }
      }
    }
  }

  BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
}
