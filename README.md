# Feodals

A turn-based territory capture game for 2–8 players, either hot-seat on one computer or over a network.
Plain C++ and the Win32 API, no third-party libraries. The game UI is in Ukrainian.
Specification (Ukrainian): [working_assets/feodals-spec.md](working_assets/feodals-spec.md).

## Building

| Toolchain | Command | Output |
|---|---|---|
| MinGW g++ (32-bit) | `build_mingw.bat` | `build\Feodals.exe` (~117 KB, Windows XP and later) |
| MSVC (32-bit) | `build_msvc.bat` | `build\Feodals_msvc.exe` (~158 KB) |
| Tests (MinGW) | `tests\build_tests.bat` | builds and runs `build\test_game.exe` (rules, saves, computer player) and `build\test_net.exe` (networking over 127.0.0.1) |

`build_msvc.bat` locates Visual Studio via `vswhere`. An MSVC build for Windows XP needs the `v141_xp` toolset and `/SUBSYSTEM:WINDOWS,5.01`; newer toolsets produce binaries for Vista/7 and later.

Build flags:
- **GCC:** `-Os -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -Wl,--gc-sections -static -s`, followed by `strip`.
- **MSVC:** `/O1 /GS- /GR- /EHs-c- /MT /Gy /Gw` with `/OPT:REF /OPT:ICF /DEBUG:NONE`.
- Both: `_WIN32_WINNT=WINVER=0x0501` (Windows XP), Unicode.

The executable depends only on system DLLs (`user32`, `gdi32`, `kernel32`, `comdlg32`, `comctl32`, `ws2_32`; the MinGW build also uses `msvcrt.dll`, which ships with every Windows).

## How to play

- The program starts with a 30×30 game for two players. Start another one or load a save from the «Гра» (Game) menu.
- **Playing against the computer:** in the new game dialog every player is either «Людина» (a person) or «Комп'ютер» (the computer) with a difficulty level: weak, medium, strong, very strong. **Weak**, **medium** and **strong** are available; very strong is planned. The computer moves by itself about 0.4 s after the previous move. Computer players exist only in local games, not in network ones.
- **Left click** an unclaimed cell to claim it. The turn then passes to the next player.
- **Encirclement:** when your cells close off an area (unclaimed and/or opponents' cells), the whole area becomes yours. As with dots on paper, cells touching only at corners also form a wall. Territory inside connects only through cell sides. The board edge is not a wall.
- The game ends when every cell is claimed.
- Pan the board by dragging with the **right or middle mouse button**. Scrolling: mouse wheel, Shift+wheel, arrow keys, PageUp/PageDown, Home/End.
- Shortcuts: Ctrl+N (new game), Ctrl+S (save), Ctrl+Shift+S (save as), Ctrl+O (load), F1 (rules).
- Saves go to the `saves\` folder next to the executable. The `.feo` format is described in [src/SaveManager.h](src/SaveManager.h).
- A new game on a board larger than the window starts scrolled to the middle of the board.
- The «Показувати останній хід» (show last move) checkbox in the side panel draws a frame around the cell of the last move. It only affects the current window (in a network game each player sets it for themselves) and is not saved with the game.

## Network game

Each player plays on their own computer. The main target is a local network (addresses like `192.168.x.y`). Playing over the internet also works if the host forwards the port on their router.

- **Host a game** («Мережа → Створити мережеву гру…», Network → Host a network game): board size, number of players, port (5757 by default), your name and color. The hosting computer becomes the server. Its IP addresses are shown in the side panel; share them with the other players.
- **Join** («Мережа → Приєднатися до гри…», Network → Join a game): the server's IP address and port, then your name and color. Colors already taken by others cannot be chosen.
- The game starts automatically once all players have joined. Everyone moves only on their own turn, and the panel shows «Ваш хід!» (your turn). An inactive window flashes in the taskbar.
- Only the server can **save** a network game, using the regular Save / Save As.
- Anyone can **continue a saved game over the network** («Мережа → Створити мережеву гру зі збереження…», Network → Host a network game from a save). The server picks which player to play. Joining players choose among the remaining saved players. Colors cannot be changed, names can.
- If a player disconnects, the game pauses. They can reconnect and take their seat again. When the server stops the game, clients keep a view of the last state.
- The first time a server starts, Windows may ask for a firewall permission for `Feodals.exe`. Without it other computers cannot connect.

How it works: TCP (IPv4, Winsock 2), events via `WSAAsyncSelect` in the regular message loop, no threads, works on Windows XP. The server is authoritative: it validates every move and broadcasts accepted ones. Clients replay each move through the same `GameState` and compare cell counters. On a mismatch a client requests a full state snapshot, which is sent in the save file format. The protocol is described at the top of [src/NetGame.cpp](src/NetGame.cpp).

## Architecture

| File | Role |
|---|---|
| [src/GameState.*](src/GameState.h) | Board, players, turn order, moves, encirclement check. No Win32 dependency |
| [src/Containers.h](src/Containers.h) | Sparse hash map of cells (`uint64` key → owner) and a simple vector, no STL |
| [src/Renderer.*](src/Renderer.h) | GDI rendering of the visible part of the board only, double-buffered |
| [src/InputHandler.*](src/InputHandler.h) | Translates a mouse click into `GameState::TryClaimCell` |
| [src/SaveManager.*](src/SaveManager.h) | Binary format (to file and to memory for networking), open/save dialogs, the `saves\` folder |
| [src/ByteBuffer.h](src/ByteBuffer.h) | Little-endian buffers for the save format and the protocol |
| [src/Bot.*](src/Bot.h) | Computer players (pure logic, no Win32) |
| [src/NetGame.*](src/NetGame.h) | Network session: server/client, lobby, move exchange, pause and reconnection |
| [src/NetDialogs.*](src/NetDialogs.h), [src/DialogKit.*](src/DialogKit.h) | Connect and player-selection dialogs, shared dialog building blocks |
| [src/NewGameDialog.*](src/NewGameDialog.h) | "New game" dialog. The template is built in memory, so no `.rc` is needed for it |
| [src/main.cpp](src/main.cpp) | `WinMain`, main window, board, side panel, menu |

### Computer player

Both levels start from the same candidates: free cells within two cells of claimed ones (a random sample of anchors on crowded boards, so a move stays fast even on a 1 000 000×1 000 000 board). They share a local positional heuristic that favours touching opponents and extending a wall next to them. Captures are found with `GameState::EvaluateClaim`, a dry run of the encirclement check.

- **Weak:** no look-ahead and no strategy. It adds random noise, overlooks a capture now and then, sometimes plays one of its top moves at random, and never defends its own territory. Tests: it takes an open capture in about 70% of cases and beats a random player in 20 of 20 games.
- **Medium:** first ranks candidates by what they capture now and by how much they block (cells where an opponent would capture). Then it plays out the best 16 on a copy of the board and subtracts the best capture the next player could answer with (one reply of look-ahead). A move that leaves it two capture threats (a fork) gets a bonus, but only half of the time. Cells won or lost outweigh position. Tests: the best defence in 28 of 30 cases and the best capture in 29 of 30, checked against a brute force over every cell; it beats the weak level in 20 of 20 games; its slowest move on a 20×20 board takes a few tens of milliseconds.

- **Strong:** alpha-beta search 4 plies deep (its move, the reply, its move, the reply). It tries the 12 best-ranked moves at the root and 6 at deeper nodes, with a budget of 4000 positions per move. Other players are assumed to play against it (paranoid minimax). The evaluation counts cells, the capture the player to move is about to make, the balance of capture threats (so it builds forks and avoids traps), and a light territory estimate: empty cells around the action that are closer to it than to anyone else (multi-source BFS), 8 such cells being worth one tenth of a real cell. That weight was chosen by 40-game matches against medium: without it strong won 31, with it 36. Tests: the best defence and the best capture in 30 of 30 cases; it beats medium in about 90% of games; the slowest move takes about 0.2 s on 20×20 and 0.6 s on 40×40. Above 5000 claimed cells it plays like medium to stay fast.

Saves use format version 2, which stores each player's level; version 1 files still load (everyone human).

### Encirclement check

Implements section 9.1 of the specification, with a few refinements:
1. **Quick rejection.** Walls connect in 8 directions, territory in 4. First, look at the side neighbours of the new cell and check whether they are connected to each other through the ring of 8 neighbours. If they form fewer than two groups, the move cannot cut anything off and no flood fill runs. Most moves on large boards take this path.
2. **Parallel flood fill.** Groups are expanded in turns (round-robin). A group that is exhausted without reaching the edge is captured. When only one unresolved group is left and none has been found open yet, that group must be the outer one, so the search stops. Closing a small pocket in the middle of a huge board therefore costs in proportion to the pocket size.
3. **Player bounding box.** An area that extends beyond the rectangle containing the player's cells is definitely open.
4. **Safety limit** `kFloodFillLimit = 100 000` cells applies to each area separately. A larger area is treated as open.

Test measurements: regular moves and closing a pocket on a 100 000×100 000 board take under 1 ms. An artificial worst case (a player spread across the whole board closing a gap in a long wall) takes about 100 ms.
