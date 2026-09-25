# Feodals

A turn-based territory capture game for 2–8 players, either hot-seat on one computer or over a network.
Plain C++ and the Win32 API, no third-party libraries. The game UI is in Ukrainian and English, switchable at any time.
Specification (Ukrainian): [working_assets/feodals-spec.md](working_assets/feodals-spec.md).

## Building

| Toolchain | Command | Output |
|---|---|---|
| MinGW g++ (32-bit) | `build_mingw.bat` | `build\Feodals.exe` (~134 KB, Windows XP and later) |
| MSVC (32-bit) | `build_msvc.bat` | `build\Feodals_msvc.exe` (~175 KB) |
| Tests (MinGW) | `tests\build_tests.bat` | builds and runs `build\test_game.exe` (rules, saves, computer player) and `build\test_net.exe` (networking over 127.0.0.1) |

`build_msvc.bat` locates Visual Studio via `vswhere`. An MSVC build for Windows XP needs the `v141_xp` toolset and `/SUBSYSTEM:WINDOWS,5.01`; newer toolsets produce binaries for Vista/7 and later.

Build flags:
- **GCC:** `-Os -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -Wl,--gc-sections -static -s`, followed by `strip`.
- **MSVC:** `/O1 /GS- /GR- /EHs-c- /MT /Gy /Gw` with `/OPT:REF /OPT:ICF /DEBUG:NONE`.
- Both: `_WIN32_WINNT=WINVER=0x0501` (Windows XP), Unicode.

The executable depends only on system DLLs (`user32`, `gdi32`, `kernel32`, `comdlg32`, `comctl32`, `ws2_32`; the MinGW build also uses `msvcrt.dll`, which ships with every Windows).

## How to play

- The program starts with a 30×30 game for two players. Start another one or load a save from the «Гра» (Game) menu.
- **Language:** the «Мова» (Language) menu switches the whole interface between Ukrainian and English on the fly, even in the middle of a game. The choice is remembered in `feodals.ini` next to the executable; the first start is in Ukrainian. Player names that were never edited («Гравець 2», "Player 2") are shown in the current language, in saves and network games too, so every player sees them in their own language; a name someone typed stays as it is.
- **Playing against the computer:** in the new game dialog every player is either «Людина» (a person) or «Комп'ютер» (the computer) with a difficulty level: weak, medium, strong, very strong. All four levels are available. The very strong one thinks for up to several seconds per move; the window stays responsive meanwhile (the computer thinks on a separate thread). The computer moves by itself about 0.4 s after the previous move. Computer players exist only in local games, not in network ones.
- **Left click** an unclaimed cell to claim it. The turn then passes to the next player.
- **Encirclement:** when your cells close off an area (unclaimed and/or opponents' cells), the whole area becomes yours. As with dots on paper, cells touching only at corners also form a wall. Territory inside connects only through cell sides. The board edge is not a wall.
- The game ends when every cell is claimed.
- Pan the board by dragging with the **right or middle mouse button**. Scrolling: mouse wheel, Shift+wheel, arrow keys, PageUp/PageDown, Home/End.
- Shortcuts: Ctrl+N (new game), Ctrl+S (save), Ctrl+Shift+S (save as), Ctrl+O (load), F1 (rules).
- Saves go to the `saves\` folder next to the executable. The `.feo` format is described in [src/SaveManager.h](src/SaveManager.h).
- A new game on a board larger than the window starts scrolled to the middle of the board.
- The «Показувати останній хід» (Show the last move) checkbox in the side panel draws a frame around the cell of the last move. It is on when the program starts, only affects the current window (in a network game each player sets it for themselves) and is not saved with the game.

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
| [src/Lang.*](src/Lang.h), [src/Strings.inc](src/Strings.inc) | Interface languages. Every text lives in `Strings.inc` with its Ukrainian and English version side by side; a test checks that both exist and that format strings take the same arguments |
| [src/UiCommon.*](src/UiCommon.h) | Fonts, palette, number formatting and plurals for the current language, default player names |
| [src/main.cpp](src/main.cpp) | `WinMain`, main window, board, side panel, menu |

### Computer player

All levels start from the same candidates: free cells within two cells of claimed ones (a random sample of anchors on crowded boards, so a move stays fast even on a 1 000 000×1 000 000 board). They share a local positional heuristic that favours touching opponents and extending a wall next to them. Captures are found with `GameState::EvaluateClaim`, a dry run of the encirclement check.

- **Weak:** no look-ahead and no strategy. It adds random noise, overlooks a capture now and then, sometimes plays one of its top moves at random, and never defends its own territory. Tests: it takes an open capture in about 70% of cases and beats a random player in 20 of 20 games.
- **Medium:** first ranks candidates by what they capture now and by how much they block (cells where an opponent would capture). Then it plays out the best 16 on a copy of the board and subtracts the best capture the next player could answer with (one reply of look-ahead). A move that leaves it two capture threats (a fork) gets a bonus, but only half of the time. Cells won or lost outweigh position. Tests: the best defence in 28 of 30 cases and the best capture in 29 of 30, checked against a brute force over every cell; it beats the weak level in 20 of 20 games; its slowest move on a 20×20 board takes a few tens of milliseconds.

- **Strong:** alpha-beta search 4 plies deep (its move, the reply, its move, the reply). It tries the 12 best-ranked moves at the root and 6 at deeper nodes, with a budget of 4000 positions per move. Other players are assumed to play against it (paranoid minimax). The evaluation counts cells, the capture the player to move is about to make, the balance of capture threats (so it builds forks and avoids traps) and a view of the position that depends on the size of the board: the territory estimate on small boards, the breathing space of each group on large ones (both are described below). At the root, cells won at once and the capture left to the opponent count a little extra, so it does not put off a sure capture or leave a group hanging. Tests: the best defence and the best capture in 20 of 20 cases; it beats medium in 12 of 12 games and the first strong version (cells and threats only, no position) in 5 of 6 games on 20×20 by about 110 cells a game; against an opponent that hunts cells standing alone on 60×60 it keeps its cells together and stays ahead. A move takes about 0.3 s on 30×30. Above 5000 claimed cells it plays like medium to stay fast.
- **Very strong:** the same search with iterative deepening: 4, 6, then 8 plies, the best move of an iteration tried first in the next one. 16 moves at the root, 8 right below it narrowing to 4 deeper down, a budget of 12 000 positions per move, and at the leaves captures are followed up to 4 more plies (quiescence), so exchanges are not judged half-way. Against strong it won 6 of 6 games on 12×12 (by about 19 cells a game). On 30×30 a move takes about 3 s on average; after 5 s it plays the best move found so far.

**Territory estimate (strong and very strong, boards up to 50×50).** Rings are usually built long before they close, often with dots every other cell, which cannot be cut: whatever cell the opponent takes between two dots, the other connection remains. So the bots estimate, for every player, how deeply each cell is about to be enclosed. Walls may be built on free cells within 5 cells of a player's cells (moving in 8 directions, like real walls); such a cell costs more to cross the closer it is, the player's own cells cannot be crossed, other players' cells cost nothing. The enclosure depth of a cell is the cheapest way to it from the outside (Dial's shortest path). A single cell or a straight wall gives its surroundings a depth of at most 15; a ring gives more: behind dots every other cell 35, behind a gap of 5 cells 27, of 7 cells 20. Cells above 15 count as potential territory in proportion, other players' cells in it twice (they would be taken away). This makes a ring that is 70–80% built count long before the last move and gives value to moves that cut into it. The change a move brings to the estimate also ranks the candidates before the search.

**Breathing space of groups (strong and very strong, boards above 50×50).** Four cells around a cell standing on its own take it: four moves for five cells, a bargain for the attacker. So on a large board the evaluation looks at each player's groups (cells joined side by side) and counts the free cells beside them: the fewer are left, the more the group is worth to its enemies. Its own groups count against a player, everyone else's for it, which keeps the bot's cells together and makes it answer cells placed beside them, and take the same chances itself.

**Why the two are split by board size.** The territory estimate cannot be trusted on a large board: with open space in every direction the bands around scattered cells cover everything the players can reach and merge into a net of phantom walls, in which the middle of the picture looks enclosed by whoever has cells around it. The bot then sowed single cells over the board instead of holding its own together — exactly what a playtest on 100×100 showed. Against an opponent that hunts cells standing alone (10 games on 100×100, 120 moves each) it went from behind in all ten games by 605 cells in all, with 717 of its cells left standing alone, to ahead in all ten by 372 cells with 5 left alone. On 60×60 it went from 0 of 10 games and 333 cells left alone to 8 of 10 and 9 left alone. The group term is the opposite: on a small board the search sees a group being surrounded move by move without any hint, and the term only added noise (it lost 7 of 8 games on 25×25 to the same bot without it), while the territory estimate is worth a lot there — without it the bot loses every game on 30×30 by some 380 cells.

The computer thinks on a worker thread with its own copy of the game; starting a new game, loading one or closing the window cancels a move in progress.

Saves use format version 2, which stores each player's level; version 1 files still load (everyone human).

### Encirclement check

Implements section 9.1 of the specification, with a few refinements:
1. **Quick rejection.** Walls connect in 8 directions, territory in 4. First, look at the side neighbours of the new cell and check whether they are connected to each other through the ring of 8 neighbours. If they form fewer than two groups, the move cannot cut anything off and no flood fill runs. Most moves on large boards take this path.
2. **Parallel flood fill.** Groups are expanded in turns (round-robin). A group that is exhausted without reaching the edge is captured. When only one unresolved group is left and none has been found open yet, that group must be the outer one, so the search stops. Closing a small pocket in the middle of a huge board therefore costs in proportion to the pocket size.
3. **Player bounding box.** An area that extends beyond the rectangle containing the player's cells is definitely open.
4. **Safety limit** `kFloodFillLimit = 100 000` cells applies to each area separately. A larger area is treated as open.

Test measurements: regular moves and closing a pocket on a 100 000×100 000 board take under 1 ms. An artificial worst case (a player spread across the whole board closing a gap in a long wall) takes about 100 ms.
