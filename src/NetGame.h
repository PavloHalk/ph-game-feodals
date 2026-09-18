// Network game over TCP (IPv4, Winsock 2, event driven through
// WSAAsyncSelect: no threads, works on Windows XP and later).
//
// The host is authoritative: it owns the lobby, validates every move and
// broadcasts accepted moves. Clients replay them through the same GameState
// logic and compare cell counts; on any mismatch they request a snapshot.
//
// Wire format: every message is uint32 length (of what follows), uint8 type,
// payload. Integers are little endian, names are UTF-8 (see SaveManager.h).
#pragma once

#include <windows.h>

#include "ByteBuffer.h"
#include "GameState.h"

// Messages posted to the window given to NetGame::SetWindow.
const UINT WM_NET_SOCKET = WM_APP + 20;  // forward to OnSocketMessage
const UINT WM_NET_EVENT = WM_APP + 21;   // wParam: NetEvent, lParam: detail

const uint16_t kDefaultNetPort = 5757;
const uint16_t kNetProtocolVersion = 2;  // 2: snapshots carry bot levels
const int kMaxNetConnections = 16;

enum NetRole { kNetOff, kNetHost, kNetClient };

enum NetSlotState {
  kSlotOpen = 0,       // new game: nobody has taken this seat yet
  kSlotWaiting = 1,    // seat with a fixed color waiting for its player
  kSlotConnected = 2,  // player is present
};

enum NetEvent {
  kNetEvLobbyChanged = 1,  // seats or status changed: repaint
  kNetEvConnected,         // client: connection to the host established
  kNetEvJoinPrompt,        // client: lobby known, ask for name and color
  kNetEvJoinRejected,      // client: lParam is NetJoinError, ask again
  kNetEvJoined,            // client: seat assigned
  kNetEvGameStarted,       // game state replaced at game start
  kNetEvStateReplaced,     // snapshot received; lParam 1 = center the view
  kNetEvMoveApplied,       // a move was applied; lParam 1 = game over
  kNetEvClosed,            // session ended; lParam is NetCloseReason
};

enum NetJoinError {
  kJoinOk = 0,
  kJoinColorTaken,
  kJoinSlotTaken,
  kJoinGameFull,
  kJoinBadRequest,
};

enum NetCloseReason {
  kCloseByUser = 0,
  kCloseConnectFailed,
  kCloseConnectionLost,
  kCloseServerStopped,
  kCloseProtocolError,
  kCloseVersionMismatch,
};

struct NetSlot {
  uint8_t state;  // NetSlotState
  PlayerInfo info;
};

class NetGame {
 public:
  NetGame();
  ~NetGame();

  void SetWindow(HWND window) { window_ = window; }

  // Host a new game; the host takes seat 0. Initializes `game` with an empty
  // board. Returns 0 or a Winsock error code (e.g. port already in use).
  int HostNewGame(GameState* game, uint16_t port, uint32_t width,
                  uint32_t height, int numPlayers, const PlayerInfo& host);

  // Host an already loaded game; the host plays `hostSlot` (its color is
  // fixed, the name may change). Other seats wait for players.
  int HostSavedGame(GameState* game, uint16_t port, int hostSlot,
                    const wchar_t* hostName);

  // Starts connecting to a host. `address` is an IPv4 address or host name.
  int Connect(GameState* game, const wchar_t* address, uint16_t port);

  // Client: asks for a seat. New game: `slot` is ignored and the color is
  // chosen freely. Saved or running game: `slot` picks a waiting seat.
  bool Join(int slot, const PlayerInfo& info);

  // Ends the session without posting kNetEvClosed.
  void Close();

  void OnSocketMessage(WPARAM wParam, LPARAM lParam);

  // Whether the local player may click a cell right now.
  bool CanLocalPlayerMove() const;

  // Local click. Host: applies and broadcasts the move (result filled in).
  // Client: sends the request; the move arrives later as kNetEvMoveApplied.
  bool LocalMove(uint32_t x, uint32_t y, MoveResult* result);

  NetRole Role() const { return role_; }
  bool IsActive() const { return role_ != kNetOff; }
  bool IsConnecting() const { return role_ == kNetClient && connecting_; }
  bool IsJoined() const { return localSlot_ >= 0; }
  bool IsStarted() const { return started_; }
  bool IsSavedGame() const { return savedGame_; }
  bool IsPaused() const;
  int LocalSlot() const { return localSlot_; }
  int NumSlots() const { return numSlots_; }
  const NetSlot& Slot(int i) const { return slots_[i]; }
  int ConnectedSlots() const;
  uint16_t Port() const { return port_; }
  uint32_t LobbyWidth() const { return width_; }
  uint32_t LobbyHeight() const { return height_; }

  // IPv4 addresses of this computer, comma separated (for the host panel).
  static void LocalAddresses(wchar_t* out, int size);

 private:
  struct Conn {
    uintptr_t sock;
    ByteBuffer in, out;
    int slot;          // seat of this connection or -1
    bool hello;        // protocol handshake done
    bool closeAfterFlush;
  };

  NetGame(const NetGame&);
  void operator=(const NetGame&);

  bool StartWinsock();
  void ResetSession();
  void Post(NetEvent event, LPARAM detail);
  void CloseWithReason(NetCloseReason reason);

  Conn* FindConn(uintptr_t sock);
  void CloseConn(Conn* conn);
  void DropConn(Conn* conn);  // host: a client went away
  void ReadConn(Conn* conn);
  void FlushConn(Conn* conn);
  void Send(Conn* conn, const ByteBuffer& message);
  void Broadcast(const ByteBuffer& message, bool joinedOnly);
  bool HandleMessage(Conn* conn, uint8_t type, const uint8_t* data,
                     size_t size);

  // Host side.
  bool HostHello(Conn* conn, ByteReader* in);
  bool HostJoin(Conn* conn, ByteReader* in);
  bool HostMove(Conn* conn, ByteReader* in);
  void BuildLobby(ByteBuffer* message);
  bool BuildState(ByteBuffer* message, uint8_t reason);
  void BuildMoveApplied(ByteBuffer* message, int player, uint32_t x,
                        uint32_t y);
  void BroadcastLobby();
  void StartGameIfReady();

  // Client side.
  bool ClientLobby(ByteReader* in);
  bool ClientState(ByteReader* in);
  bool ClientMoveApplied(ByteReader* in);

  HWND window_;
  NetRole role_;
  GameState* game_;
  uintptr_t listen_;
  Conn conns_[kMaxNetConnections];
  uint8_t* recvBuffer_;
  bool winsockStarted_;

  uint16_t port_;
  bool connecting_;
  bool started_;
  bool savedGame_;
  bool joinPrompted_;
  bool moveSent_;
  bool receivedState_;
  int localSlot_;
  uint32_t width_, height_;
  int numSlots_;
  NetSlot slots_[kMaxPlayers];
};
