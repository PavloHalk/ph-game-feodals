#include "InputHandler.h"

#include "Renderer.h"

bool ClientPointToCell(const GameState& game, int mouseX, int mouseY,
                       int scrollX, int scrollY, uint32_t* cellX,
                       uint32_t* cellY) {
  int64_t px = (int64_t)mouseX + scrollX;
  int64_t py = (int64_t)mouseY + scrollY;
  if (px < 0 || py < 0) return false;
  int64_t cx = px / kCellSize, cy = py / kCellSize;
  if (cx >= game.Width() || cy >= game.Height()) return false;
  *cellX = (uint32_t)cx;
  *cellY = (uint32_t)cy;
  return true;
}

MoveResult HandleBoardClick(GameState& game, int mouseX, int mouseY,
                            int scrollX, int scrollY) {
  uint32_t x, y;
  if (!ClientPointToCell(game, mouseX, mouseY, scrollX, scrollY, &x, &y)) {
    MoveResult none;
    memset(&none, 0, sizeof(none));
    return none;
  }
  return game.TryClaimCell(x, y);
}
