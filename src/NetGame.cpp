// WSAAsyncSelect, inet_addr and gethostbyname are used on purpose: they exist
// on Windows XP, unlike their modern replacements.
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>

#include "NetGame.h"

#include "SaveManager.h"
#include "UiCommon.h"

namespace {

const char kNetMagic[4] = {'F', 'E', 'O', 'N'};
const uint32_t kMaxMessageSize = 256u << 20;
const size_t kRecvBufferSize = 1 << 16;

// Message types.
enum {
  // Client -> host.
  kMsgHello = 1,       // magic[4], u16 version
  kMsgJoin = 2,        // i8 slot (0xFF = any), player info
  kMsgMove = 3,        // u32 x, u32 y
  kMsgResync = 4,      // ask for a full snapshot
  // Host -> client.
  kMsgLobby = 10,      // u8 started, u8 saved, u32 w, u32 h, u8 n, seats
  kMsgJoinOk = 11,     // u8 slot
  kMsgJoinFail = 12,   // u8 NetJoinError
  kMsgState = 13,      // u8 reason, serialized game (save file format)
  kMsgMoveApplied = 14,  // u8 player, u32 x, u32 y, u8 next, u64 filled,
                         // u64 cells per player
  kMsgMoveRejected = 15,
  kMsgBye = 16,        // host stops the game
  kMsgVersion = 17,    // u16 host protocol version, then the host closes
};

// STATE reasons.
enum { kStatePreview = 0, kStateStart = 1, kStateSync = 2 };

void BeginMessage(ByteBuffer* m, uint8_t type) {
  m->Clear();
  m->U32(0);
  m->U8(type);
}

void EndMessage(ByteBuffer* m) { m->PatchU32(0, (uint32_t)(m->Size() - 4)); }

const uintptr_t kNoSocket = (uintptr_t)INVALID_SOCKET;

}  // namespace

NetGame::NetGame()
    : window_(0), role_(kNetOff), game_(0), listen_(kNoSocket),
      recvBuffer_(0), winsockStarted_(false) {
  for (int i = 0; i < kMaxNetConnections; ++i) conns_[i].sock = kNoSocket;
  ResetSession();
}

NetGame::~NetGame() {
  Close();
  free(recvBuffer_);
  if (winsockStarted_) WSACleanup();
}

bool NetGame::StartWinsock() {
  if (!winsockStarted_) {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    winsockStarted_ = true;
  }
  if (!recvBuffer_) recvBuffer_ = (uint8_t*)malloc(kRecvBufferSize);
  return recvBuffer_ != 0;
}

void NetGame::ResetSession() {
  role_ = kNetOff;
  port_ = 0;
  connecting_ = false;
  started_ = false;
  savedGame_ = false;
  joinPrompted_ = false;
  moveSent_ = false;
  receivedState_ = false;
  localSlot_ = -1;
  width_ = height_ = 0;
  numSlots_ = 0;
  memset(slots_, 0, sizeof(slots_));
}

void NetGame::Post(NetEvent event, LPARAM detail) {
  if (window_) PostMessageW(window_, WM_NET_EVENT, (WPARAM)event, detail);
}

void NetGame::Close() {
  if (role_ == kNetHost) {
    ByteBuffer bye;
    BeginMessage(&bye, kMsgBye);
    EndMessage(&bye);
    Broadcast(bye, false);
  }
  for (int i = 0; i < kMaxNetConnections; ++i) CloseConn(&conns_[i]);
  if (listen_ != kNoSocket) {
    closesocket((SOCKET)listen_);
    listen_ = kNoSocket;
  }
  ResetSession();
}

void NetGame::CloseWithReason(NetCloseReason reason) {
  if (role_ == kNetOff) return;
  Close();
  Post(kNetEvClosed, reason);
}

// ---- Connections --------------------------------------------------------------

NetGame::Conn* NetGame::FindConn(uintptr_t sock) {
  for (int i = 0; i < kMaxNetConnections; ++i) {
    if (conns_[i].sock == sock && sock != kNoSocket) return &conns_[i];
  }
  return 0;
}

void NetGame::CloseConn(Conn* conn) {
  if (conn->sock == kNoSocket) return;
  FlushConn(conn);  // best effort for a final message
  WSAAsyncSelect((SOCKET)conn->sock, 0, 0, 0);
  closesocket((SOCKET)conn->sock);
  conn->sock = kNoSocket;
  conn->in.Free();
  conn->out.Free();
  conn->slot = -1;
  conn->hello = false;
  conn->closeAfterFlush = false;
}

void NetGame::DropConn(Conn* conn) {
  if (role_ == kNetClient) {
    CloseWithReason(kCloseConnectionLost);
    return;
  }
  int slot = conn->slot;
  conn->out.Clear();
  CloseConn(conn);
  if (slot < 0) return;
  if (!started_ && !savedGame_) {
    memset(&slots_[slot], 0, sizeof(slots_[slot]));
    slots_[slot].state = kSlotOpen;
  } else {
    slots_[slot].state = kSlotWaiting;
  }
  BroadcastLobby();
  Post(kNetEvLobbyChanged, 0);
}

void NetGame::FlushConn(Conn* conn) {
  while (conn->sock != kNoSocket && conn->out.Size()) {
    int chunk = conn->out.Size() > (1u << 20) ? (1 << 20)
                                              : (int)conn->out.Size();
    int sent = send((SOCKET)conn->sock, (const char*)conn->out.Data(), chunk, 0);
    if (sent == SOCKET_ERROR) {
      if (WSAGetLastError() == WSAEWOULDBLOCK) return;  // wait for FD_WRITE
      conn->out.Clear();
      return;  // FD_CLOSE follows
    }
    conn->out.Consume((size_t)sent);
  }
}

void NetGame::Send(Conn* conn, const ByteBuffer& message) {
  if (conn->sock == kNoSocket) return;
  if (!conn->out.Bytes(message.Data(), message.Size())) {
    DropConn(conn);  // out of memory for this peer
    return;
  }
  FlushConn(conn);
  if (conn->sock != kNoSocket && conn->closeAfterFlush && !conn->out.Size()) {
    CloseConn(conn);
  }
}

void NetGame::Broadcast(const ByteBuffer& message, bool joinedOnly) {
  for (int i = 0; i < kMaxNetConnections; ++i) {
    Conn* c = &conns_[i];
    if (c->sock == kNoSocket || !c->hello) continue;
    if (joinedOnly && c->slot < 0) continue;
    Send(c, message);
  }
}

void NetGame::ReadConn(Conn* conn) {
  for (;;) {
    int got = recv((SOCKET)conn->sock, (char*)recvBuffer_,
                   (int)kRecvBufferSize, 0);
    if (got == SOCKET_ERROR) {
      if (WSAGetLastError() != WSAEWOULDBLOCK) DropConn(conn);
      break;
    }
    if (got == 0) break;  // orderly shutdown, FD_CLOSE handles it
    if (!conn->in.Bytes(recvBuffer_, (size_t)got)) {
      DropConn(conn);
      return;
    }
    if (got < (int)kRecvBufferSize) break;
  }

  uintptr_t sock = conn->sock;
  while (conn->sock == sock && sock != kNoSocket && conn->in.Size() >= 4) {
    const uint8_t* p = conn->in.Data();
    uint32_t length = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
    if (length == 0 || length > kMaxMessageSize) {
      if (role_ == kNetClient) {
        CloseWithReason(kCloseProtocolError);
      } else {
        DropConn(conn);
      }
      return;
    }
    if (conn->in.Size() < 4 + (size_t)length) break;
    // Copy out: handlers may send, close or reset buffers.
    ByteBuffer message;
    if (!message.Bytes(p + 5, length - 1)) {
      DropConn(conn);
      return;
    }
    uint8_t type = p[4];
    conn->in.Consume(4 + (size_t)length);
    if (!HandleMessage(conn, type, message.Data(), message.Size())) {
      if (role_ == kNetClient) {
        CloseWithReason(kCloseProtocolError);
      } else if (conn->sock == sock) {
        DropConn(conn);
      }
      return;
    }
  }
}

void NetGame::OnSocketMessage(WPARAM wParam, LPARAM lParam) {
  uintptr_t sock = (uintptr_t)wParam;
  int event = WSAGETSELECTEVENT(lParam);
  int error = WSAGETSELECTERROR(lParam);

  if (role_ == kNetHost && sock == listen_ && event == FD_ACCEPT) {
    for (;;) {
      SOCKET s = accept((SOCKET)listen_, 0, 0);
      if (s == INVALID_SOCKET) break;
      Conn* conn = 0;
      for (int i = 0; i < kMaxNetConnections && !conn; ++i) {
        if (conns_[i].sock == kNoSocket) conn = &conns_[i];
      }
      if (!conn) {
        closesocket(s);
        continue;
      }
      BOOL noDelay = TRUE;
      setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay,
                 sizeof(noDelay));
      conn->sock = (uintptr_t)s;
      conn->slot = -1;
      conn->hello = false;
      conn->closeAfterFlush = false;
      conn->in.Clear();
      conn->out.Clear();
      WSAAsyncSelect(s, window_, WM_NET_SOCKET, FD_READ | FD_WRITE | FD_CLOSE);
    }
    return;
  }

  Conn* conn = FindConn(sock);
  if (!conn) return;  // stale notification for a closed socket

  switch (event) {
    case FD_CONNECT: {
      if (error) {
        CloseWithReason(kCloseConnectFailed);
        return;
      }
      connecting_ = false;
      BOOL noDelay = TRUE;
      setsockopt((SOCKET)sock, IPPROTO_TCP, TCP_NODELAY,
                 (const char*)&noDelay, sizeof(noDelay));
      ByteBuffer hello;
      BeginMessage(&hello, kMsgHello);
      hello.Bytes(kNetMagic, 4);
      hello.U16(kNetProtocolVersion);
      EndMessage(&hello);
      conn->hello = true;
      Send(conn, hello);
      Post(kNetEvConnected, 0);
      break;
    }
    case FD_READ:
      ReadConn(conn);
      break;
    case FD_WRITE:
      FlushConn(conn);
      if (conn->sock != kNoSocket && conn->closeAfterFlush &&
          !conn->out.Size()) {
        CloseConn(conn);
      }
      break;
    case FD_CLOSE:
      ReadConn(conn);  // deliver what arrived before the close
      if (conn->sock != sock) return;
      if (role_ == kNetClient) {
        CloseWithReason(connecting_ ? kCloseConnectFailed
                                    : kCloseConnectionLost);
      } else {
        DropConn(conn);
      }
      break;
  }
}

bool NetGame::HandleMessage(Conn* conn, uint8_t type, const uint8_t* data,
                            size_t size) {
  ByteReader in(data, size);
  if (role_ == kNetHost) {
    if (!conn->hello && type != kMsgHello) return false;
    switch (type) {
      case kMsgHello: return HostHello(conn, &in);
      case kMsgJoin: return HostJoin(conn, &in);
      case kMsgMove: return HostMove(conn, &in);
      case kMsgResync: {
        if (conn->slot < 0 || !started_) return true;
        ByteBuffer state;
        if (BuildState(&state, kStateSync)) Send(conn, state);
        return true;
      }
    }
    return false;
  }

  switch (type) {
    case kMsgLobby:
      return ClientLobby(&in);
    case kMsgJoinOk: {
      int slot = (int)in.U8();
      if (!in.Ok() || slot >= numSlots_) return false;
      localSlot_ = slot;
      Post(kNetEvJoined, slot);
      Post(kNetEvLobbyChanged, 0);
      return true;
    }
    case kMsgJoinFail: {
      int reason = (int)in.U8();
      joinPrompted_ = false;
      Post(kNetEvJoinRejected, reason);
      return in.Ok();
    }
    case kMsgState:
      return ClientState(&in);
    case kMsgMoveApplied:
      return ClientMoveApplied(&in);
    case kMsgMoveRejected:
      moveSent_ = false;
      Post(kNetEvLobbyChanged, 0);
      return true;
    case kMsgBye:
      CloseWithReason(kCloseServerStopped);
      return true;
    case kMsgVersion:
      CloseWithReason(kCloseVersionMismatch);
      return true;
  }
  return false;
}

// ---- Host -------------------------------------------------------------------

int NetGame::HostNewGame(GameState* game, uint16_t port, uint32_t width,
                         uint32_t height, int numPlayers,
                         const PlayerInfo& host) {
  Close();
  if (!StartWinsock()) return WSASYSNOTREADY;

  PlayerInfo placeholder[kMaxPlayers];
  for (int i = 0; i < kMaxPlayers; ++i) {
    DefaultPlayerName(i, placeholder[i].name);
    placeholder[i].color = kDefaultPalette[i];
  }
  placeholder[0] = host;
  if (!game->Init(width, height, numPlayers, placeholder)) {
    return WSAEINVAL;
  }

  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return WSAGetLastError();
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR ||
      listen(s, SOMAXCONN) == SOCKET_ERROR ||
      WSAAsyncSelect(s, window_, WM_NET_SOCKET, FD_ACCEPT) == SOCKET_ERROR) {
    int error = WSAGetLastError();
    closesocket(s);
    return error;
  }

  listen_ = (uintptr_t)s;
  role_ = kNetHost;
  game_ = game;
  port_ = port;
  width_ = width;
  height_ = height;
  numSlots_ = numPlayers;
  for (int i = 0; i < numPlayers; ++i) slots_[i].state = kSlotOpen;
  slots_[0].state = kSlotConnected;
  slots_[0].info = host;
  localSlot_ = 0;
  return 0;
}

int NetGame::HostSavedGame(GameState* game, uint16_t port, int hostSlot,
                           const wchar_t* hostName) {
  Close();
  if (!StartWinsock()) return WSASYSNOTREADY;
  if (hostSlot < 0 || hostSlot >= game->NumPlayers()) return WSAEINVAL;

  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return WSAGetLastError();
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR ||
      listen(s, SOMAXCONN) == SOCKET_ERROR ||
      WSAAsyncSelect(s, window_, WM_NET_SOCKET, FD_ACCEPT) == SOCKET_ERROR) {
    int error = WSAGetLastError();
    closesocket(s);
    return error;
  }

  if (hostName && hostName[0]) {
    PlayerInfo info = game->Player(hostSlot);
    lstrcpynW(info.name, hostName, kMaxNameLen);
    game->SetPlayerInfo(hostSlot, info);
  }

  listen_ = (uintptr_t)s;
  role_ = kNetHost;
  game_ = game;
  port_ = port;
  savedGame_ = true;
  width_ = game->Width();
  height_ = game->Height();
  numSlots_ = game->NumPlayers();
  for (int i = 0; i < numSlots_; ++i) {
    slots_[i].state = kSlotWaiting;
    slots_[i].info = game->Player(i);
  }
  slots_[hostSlot].state = kSlotConnected;
  localSlot_ = hostSlot;
  return 0;
}

bool NetGame::HostHello(Conn* conn, ByteReader* in) {
  char magic[4];
  in->Bytes(magic, 4);
  uint32_t version = in->U16();
  if (!in->Ok() || memcmp(magic, kNetMagic, 4) != 0) return false;
  if (version != kNetProtocolVersion) {
    ByteBuffer m;
    BeginMessage(&m, kMsgVersion);
    m.U16(kNetProtocolVersion);
    EndMessage(&m);
    conn->closeAfterFlush = true;
    Send(conn, m);
    return true;
  }
  conn->hello = true;
  ByteBuffer lobby;
  BuildLobby(&lobby);
  Send(conn, lobby);
  if (savedGame_ || started_) {
    ByteBuffer state;
    if (BuildState(&state, kStatePreview)) Send(conn, state);
  }
  return true;
}

bool NetGame::HostJoin(Conn* conn, ByteReader* in) {
  int requested = (int)in->U8();
  PlayerInfo info;
  if (!ReadPlayerInfo(in, &info) || conn->slot >= 0) return false;

  ByteBuffer reply;
  int slot = -1;
  NetJoinError error = kJoinOk;
  if (!started_ && !savedGame_) {
    info.color &= 0xFFFFFF;
    for (int i = 0; i < numSlots_ && slot < 0; ++i) {
      if (slots_[i].state == kSlotOpen) slot = i;
    }
    if (slot < 0) error = kJoinGameFull;
    for (int i = 0; i < numSlots_; ++i) {
      if (slots_[i].state != kSlotOpen && slots_[i].info.color == info.color) {
        error = kJoinColorTaken;
      }
    }
    if (info.color == 0xFFFFFF) error = kJoinBadRequest;
  } else {
    bool anyWaiting = false;
    for (int i = 0; i < numSlots_; ++i) {
      if (slots_[i].state == kSlotWaiting) anyWaiting = true;
    }
    if (!anyWaiting) {
      error = kJoinGameFull;
    } else if (requested >= numSlots_ ||
               slots_[requested].state != kSlotWaiting) {
      error = kJoinSlotTaken;
    } else {
      slot = requested;
      info.color = slots_[slot].info.color;  // colors of a saved game stay
    }
  }

  if (error != kJoinOk) {
    BeginMessage(&reply, kMsgJoinFail);
    reply.U8(error);
    EndMessage(&reply);
    Send(conn, reply);
    return true;
  }

  if (!info.name[0]) {
    if (started_ || savedGame_) {
      lstrcpyW(info.name, slots_[slot].info.name);  // keep the saved name
    } else {
      DefaultPlayerName(slot, info.name);
    }
  }
  conn->slot = slot;
  slots_[slot].state = kSlotConnected;
  slots_[slot].info = info;
  if (started_ || savedGame_) game_->SetPlayerInfo(slot, info);

  BeginMessage(&reply, kMsgJoinOk);
  reply.U8(slot);
  EndMessage(&reply);
  Send(conn, reply);

  if (started_) {
    ByteBuffer state;
    if (BuildState(&state, kStateSync)) Send(conn, state);
    BroadcastLobby();
    // Names may have changed: refresh everybody's player list.
    ByteBuffer sync;
    if (BuildState(&sync, kStateSync)) {
      for (int i = 0; i < kMaxNetConnections; ++i) {
        Conn* c = &conns_[i];
        if (c != conn && c->sock != kNoSocket && c->slot >= 0) Send(c, sync);
      }
    }
  } else {
    BroadcastLobby();
    StartGameIfReady();
  }
  Post(kNetEvLobbyChanged, 0);
  return true;
}

void NetGame::StartGameIfReady() {
  if (started_ || ConnectedSlots() < numSlots_) return;
  if (!savedGame_) {
    PlayerInfo players[kMaxPlayers];
    for (int i = 0; i < numSlots_; ++i) players[i] = slots_[i].info;
    game_->Init(width_, height_, numSlots_, players);
  } else {
    for (int i = 0; i < numSlots_; ++i) {
      game_->SetPlayerInfo(i, slots_[i].info);
    }
  }
  started_ = true;
  BroadcastLobby();
  ByteBuffer state;
  if (BuildState(&state, kStateStart)) Broadcast(state, true);
  Post(kNetEvGameStarted, 0);
}

bool NetGame::HostMove(Conn* conn, ByteReader* in) {
  uint32_t x = in->U32();
  uint32_t y = in->U32();
  if (!in->Ok()) return false;
  bool allowed = started_ && !IsPaused() && conn->slot >= 0 &&
                 conn->slot == game_->CurrentPlayer();
  MoveResult r;
  memset(&r, 0, sizeof(r));
  if (allowed) r = game_->TryClaimCell(x, y);
  ByteBuffer m;
  if (!r.accepted) {
    BeginMessage(&m, kMsgMoveRejected);
    EndMessage(&m);
    Send(conn, m);
    return true;
  }
  BuildMoveApplied(&m, conn->slot, x, y);
  Broadcast(m, true);
  Post(kNetEvMoveApplied, r.gameOver ? 1 : 0);
  return true;
}

void NetGame::BuildLobby(ByteBuffer* m) {
  BeginMessage(m, kMsgLobby);
  m->U8(started_ ? 1 : 0);
  m->U8(savedGame_ ? 1 : 0);
  m->U32(width_);
  m->U32(height_);
  m->U8(numSlots_);
  for (int i = 0; i < numSlots_; ++i) {
    m->U8(slots_[i].state);
    WritePlayerInfo(m, slots_[i].info);
  }
  EndMessage(m);
}

bool NetGame::BuildState(ByteBuffer* m, uint8_t reason) {
  BeginMessage(m, kMsgState);
  m->U8(reason);
  if (!SerializeGame(*game_, m)) return false;
  EndMessage(m);
  return m->Ok();
}

void NetGame::BuildMoveApplied(ByteBuffer* m, int player, uint32_t x,
                               uint32_t y) {
  BeginMessage(m, kMsgMoveApplied);
  m->U8(player);
  m->U32(x);
  m->U32(y);
  m->U8(game_->CurrentPlayer());
  m->U64(game_->FilledCells());
  for (int i = 0; i < game_->NumPlayers(); ++i) m->U64(game_->CellCount(i));
  EndMessage(m);
}

void NetGame::BroadcastLobby() {
  ByteBuffer lobby;
  BuildLobby(&lobby);
  Broadcast(lobby, false);
}

// ---- Client -----------------------------------------------------------------

int NetGame::Connect(GameState* game, const wchar_t* address, uint16_t port) {
  Close();
  if (!StartWinsock()) return WSASYSNOTREADY;

  char host[256];
  if (!WideCharToMultiByte(CP_ACP, 0, address, -1, host, sizeof(host), 0, 0)) {
    return WSAEINVAL;
  }
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(host);
  if (addr.sin_addr.s_addr == INADDR_NONE) {
    hostent* entry = gethostbyname(host);
    if (!entry || entry->h_addrtype != AF_INET || !entry->h_addr_list[0]) {
      return WSAHOST_NOT_FOUND;
    }
    memcpy(&addr.sin_addr, entry->h_addr_list[0], 4);
  }

  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return WSAGetLastError();
  if (WSAAsyncSelect(s, window_, WM_NET_SOCKET,
                     FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE) ==
      SOCKET_ERROR) {
    int error = WSAGetLastError();
    closesocket(s);
    return error;
  }
  if (connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR &&
      WSAGetLastError() != WSAEWOULDBLOCK) {
    int error = WSAGetLastError();
    closesocket(s);
    return error;
  }

  role_ = kNetClient;
  game_ = game;
  port_ = port;
  connecting_ = true;
  Conn* c = &conns_[0];
  c->sock = (uintptr_t)s;
  c->slot = -1;
  c->hello = false;
  c->closeAfterFlush = false;
  c->in.Clear();
  c->out.Clear();
  return 0;
}

bool NetGame::Join(int slot, const PlayerInfo& info) {
  if (role_ != kNetClient || conns_[0].sock == kNoSocket || IsJoined()) {
    return false;
  }
  ByteBuffer m;
  BeginMessage(&m, kMsgJoin);
  m.U8(slot < 0 ? 0xFF : slot);
  WritePlayerInfo(&m, info);
  EndMessage(&m);
  Send(&conns_[0], m);
  return true;
}

bool NetGame::ClientLobby(ByteReader* in) {
  bool started = in->U8() != 0;
  bool saved = in->U8() != 0;
  uint32_t width = in->U32();
  uint32_t height = in->U32();
  int n = (int)in->U8();
  if (!in->Ok() || n < kMinPlayers || n > kMaxPlayers ||
      width < kMinBoardSize || height < kMinBoardSize ||
      width > kMaxBoardSize || height > kMaxBoardSize) {
    return false;
  }
  NetSlot slots[kMaxPlayers];
  memset(slots, 0, sizeof(slots));
  for (int i = 0; i < n; ++i) {
    slots[i].state = (uint8_t)in->U8();
    if (slots[i].state > kSlotConnected) return false;
    if (!ReadPlayerInfo(in, &slots[i].info)) return false;
  }
  started_ = started;
  savedGame_ = saved;
  width_ = width;
  height_ = height;
  numSlots_ = n;
  memcpy(slots_, slots, sizeof(slots_));

  // Show the empty board of a new game while waiting in the lobby.
  if (!started_ && !savedGame_ && !receivedState_ &&
      (game_->Width() != width || game_->Height() != height ||
       game_->NumPlayers() != n || game_->FilledCells())) {
    PlayerInfo players[kMaxPlayers];
    for (int i = 0; i < n; ++i) players[i] = slots_[i].info;
    game_->Init(width, height, n, players);
    Post(kNetEvStateReplaced, 1);
  }

  if (!IsJoined() && !joinPrompted_) {
    joinPrompted_ = true;
    Post(kNetEvJoinPrompt, 0);
  }
  Post(kNetEvLobbyChanged, 0);
  return true;
}

bool NetGame::ClientState(ByteReader* in) {
  int reason = (int)in->U8();
  if (!in->Ok()) return false;
  if (DeserializeGame(in->Current(), in->Remaining(), game_) != kSaveOk) {
    return false;
  }
  bool first = !receivedState_;
  receivedState_ = true;
  moveSent_ = false;
  if (reason == kStateStart) {
    started_ = true;
    Post(kNetEvGameStarted, 0);
  } else {
    Post(kNetEvStateReplaced, first ? 1 : 0);
  }
  return true;
}

bool NetGame::ClientMoveApplied(ByteReader* in) {
  int player = (int)in->U8();
  uint32_t x = in->U32();
  uint32_t y = in->U32();
  int next = (int)in->U8();
  uint64_t filled = in->U64();
  uint64_t counts[kMaxPlayers] = {0};
  for (int i = 0; i < game_->NumPlayers(); ++i) counts[i] = in->U64();
  if (!in->Ok()) return false;

  moveSent_ = false;
  bool inSync = game_->SetCurrentPlayer(player);
  MoveResult r;
  memset(&r, 0, sizeof(r));
  if (inSync) r = game_->TryClaimCell(x, y);
  inSync = inSync && r.accepted && game_->FilledCells() == filled;
  for (int i = 0; i < game_->NumPlayers() && inSync; ++i) {
    inSync = game_->CellCount(i) == counts[i];
  }
  if (!r.gameOver) game_->SetCurrentPlayer(next);
  if (!inSync) {
    ByteBuffer m;
    BeginMessage(&m, kMsgResync);
    EndMessage(&m);
    Send(&conns_[0], m);
  }
  Post(kNetEvMoveApplied, r.gameOver ? 1 : 0);
  return true;
}

// ---- Moves and status ---------------------------------------------------------

bool NetGame::IsPaused() const {
  return started_ && ConnectedSlots() < numSlots_;
}

int NetGame::ConnectedSlots() const {
  int n = 0;
  for (int i = 0; i < numSlots_; ++i) n += slots_[i].state == kSlotConnected;
  return n;
}

bool NetGame::CanLocalPlayerMove() const {
  return role_ != kNetOff && started_ && !IsPaused() && localSlot_ >= 0 &&
         !game_->IsGameOver() && game_->CurrentPlayer() == localSlot_ &&
         !(role_ == kNetClient && moveSent_);
}

bool NetGame::LocalMove(uint32_t x, uint32_t y, MoveResult* result) {
  memset(result, 0, sizeof(*result));
  if (!CanLocalPlayerMove() || game_->Owner(x, y) >= 0) return false;
  if (role_ == kNetHost) {
    *result = game_->TryClaimCell(x, y);
    if (!result->accepted) return false;
    ByteBuffer m;
    BuildMoveApplied(&m, localSlot_, x, y);
    Broadcast(m, true);
    return true;
  }
  ByteBuffer m;
  BeginMessage(&m, kMsgMove);
  m.U32(x);
  m.U32(y);
  EndMessage(&m);
  moveSent_ = true;
  Send(&conns_[0], m);
  return true;
}

void NetGame::LocalAddresses(wchar_t* out, int size) {
  out[0] = 0;
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return;
  char name[256];
  if (gethostname(name, sizeof(name)) == 0) {
    hostent* entry = gethostbyname(name);
    for (int i = 0; entry && entry->h_addrtype == AF_INET &&
                    entry->h_addr_list[i];
         ++i) {
      in_addr addr;
      memcpy(&addr, entry->h_addr_list[i], 4);
      if (addr.s_net == 127) continue;
      const char* text = inet_ntoa(addr);
      wchar_t wide[32];
      MultiByteToWideChar(CP_ACP, 0, text, -1, wide, 32);
      if (lstrlenW(out) + lstrlenW(wide) + 3 >= size) break;
      if (out[0]) lstrcatW(out, L", ");
      lstrcatW(out, wide);
    }
  }
  WSACleanup();
}
