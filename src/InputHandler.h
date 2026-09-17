// Translates local mouse input into game moves. A network receiver can later
// call GameState::TryClaimCell the same way.
#pragma once

#include "GameState.h"

// Converts a point in board-window client coordinates into a cell.
// Returns false when the point is outside the board.
bool ClientPointToCell(const GameState& game, int mouseX, int mouseY,
                       int scrollX, int scrollY, uint32_t* cellX,
                       uint32_t* cellY);

// Left click on the board: claims the cell under the cursor for the current
// player (if the cell is free).
MoveResult HandleBoardClick(GameState& game, int mouseX, int mouseY,
                            int scrollX, int scrollY);
