#include "SaveManager.h"

#include <commdlg.h>

#include "Lang.h"

namespace {

const char kMagic[4] = {'F', 'E', 'O', 'D'};
const uint64_t kCellRecordSize = 9;
const DWORD kMaxFileSize = 0x60000000;  // 1.5 GB, fits a 32-bit process

void MakeDefaultSaveName(wchar_t* buffer) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wsprintfW(buffer, L"feodals_%04u-%02u-%02u_%02u%02u.feo", t.wYear, t.wMonth,
            t.wDay, t.wHour, t.wMinute);
}

// "Feodals saves (*.feo)\0*.feo\0All files (*.*)\0*.*\0" in the current
// language (the list ends with an empty string).
void MakeFileFilter(wchar_t* out) {
  const wchar_t* parts[] = {Tr(kStrFilterSaves), L"*.feo", Tr(kStrFilterAll),
                            L"*.*"};
  for (int i = 0; i < 4; ++i) {
    lstrcpyW(out, parts[i]);
    out += lstrlenW(out) + 1;
  }
  *out = 0;
}

const int kFilterSize = 128;

}  // namespace

void WritePlayerInfo(ByteBuffer* out, const PlayerInfo& player) {
  char utf8[kMaxNameLen * 4];
  int len = WideCharToMultiByte(CP_UTF8, 0, player.name, -1, utf8,
                                sizeof(utf8), 0, 0);
  len = len > 0 ? len - 1 : 0;  // without the terminating zero
  if (len > 255) len = 255;
  out->U8(len);
  out->Bytes(utf8, len);
  out->U8(player.color & 0xFF);
  out->U8((player.color >> 8) & 0xFF);
  out->U8((player.color >> 16) & 0xFF);
}

bool ReadPlayerInfo(ByteReader* in, PlayerInfo* player) {
  memset(player, 0, sizeof(*player));
  char utf8[256];
  uint32_t len = in->U8();
  if (!in->Bytes(utf8, len)) return false;
  wchar_t wide[256];
  int wlen =
      len ? MultiByteToWideChar(CP_UTF8, 0, utf8, (int)len, wide, 255) : 0;
  if (len && wlen <= 0) return false;
  if (wlen > kMaxNameLen - 1) wlen = kMaxNameLen - 1;
  memcpy(player->name, wide, wlen * sizeof(wchar_t));
  uint32_t r = in->U8(), g = in->U8(), b = in->U8();
  player->color = r | (g << 8) | (b << 16);
  return in->Ok();
}

bool SerializeGame(const GameState& game, ByteBuffer* out) {
  const CellMap& cells = game.Cells();
  out->Bytes(kMagic, 4);
  out->U16(kSaveFormatVersion);
  out->U32(game.Width());
  out->U32(game.Height());
  out->U8(game.NumPlayers());
  for (int i = 0; i < game.NumPlayers(); ++i) {
    WritePlayerInfo(out, game.Player(i));
    out->U8(game.Player(i).botLevel);
  }
  out->U8(game.CurrentPlayer());
  out->U64(cells.Count());

  uint8_t* p = out->Grow(cells.Count() * (size_t)kCellRecordSize);
  if (!p) return false;
  for (size_t i = 0; i < cells.Capacity(); ++i) {
    if (!cells.SlotUsed(i)) continue;
    uint64_t key = cells.SlotKey(i);
    uint32_t x = CellKeyX(key), y = CellKeyY(key);
    p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8);
    p[2] = (uint8_t)(x >> 16); p[3] = (uint8_t)(x >> 24);
    p[4] = (uint8_t)y; p[5] = (uint8_t)(y >> 8);
    p[6] = (uint8_t)(y >> 16); p[7] = (uint8_t)(y >> 24);
    p[8] = cells.SlotValue(i);
    p += kCellRecordSize;
  }
  return out->Ok();
}

SaveResult DeserializeGame(const uint8_t* data, size_t size, GameState* game) {
  ByteReader in(data, size);
  char magic[4];
  if (!in.Bytes(magic, 4) || memcmp(magic, kMagic, 4) != 0) {
    return kSaveErrFormat;
  }
  uint32_t version = in.U16();
  if (!in.Ok()) return kSaveErrFormat;
  if (version < 1 || version > kSaveFormatVersion) return kSaveErrVersion;

  uint32_t width = in.U32();
  uint32_t height = in.U32();
  int numPlayers = (int)in.U8();
  if (!in.Ok() || numPlayers < kMinPlayers || numPlayers > kMaxPlayers) {
    return kSaveErrFormat;
  }
  PlayerInfo players[kMaxPlayers];
  memset(players, 0, sizeof(players));
  for (int i = 0; i < numPlayers; ++i) {
    if (!ReadPlayerInfo(&in, &players[i])) return kSaveErrFormat;
    if (version >= 2) {
      uint32_t level = in.U8();
      if (level >= (uint32_t)kBotLevelCount) return kSaveErrFormat;
      players[i].botLevel = (uint8_t)level;
    }
  }
  int current = (int)in.U8();
  uint64_t cellCount = in.U64();
  if (!in.Ok()) return kSaveErrFormat;

  GameState loaded;
  if (!loaded.Init(width, height, numPlayers, players) ||
      !loaded.SetCurrentPlayer(current) || cellCount > loaded.TotalCells() ||
      in.Remaining() != cellCount * kCellRecordSize) {
    return kSaveErrFormat;
  }
  if (!loaded.ReserveCells(cellCount)) return kSaveErrMemory;

  const uint8_t* p = in.Current();
  for (uint64_t i = 0; i < cellCount; ++i, p += kCellRecordSize) {
    uint32_t x = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
    uint32_t y = p[4] | (p[5] << 8) | (p[6] << 16) | ((uint32_t)p[7] << 24);
    if (!loaded.PlaceCell(x, y, p[8])) return kSaveErrFormat;
  }

  game->Swap(loaded);
  return kSaveOk;
}

SaveResult SaveGame(const wchar_t* path, const GameState& game) {
  wchar_t tempPath[MAX_PATH + 8];
  if (lstrlenW(path) >= MAX_PATH) return kSaveErrOpen;
  lstrcpyW(tempPath, path);
  lstrcatW(tempPath, L".tmp");

  ByteBuffer data;
  if (!SerializeGame(game, &data)) return kSaveErrMemory;

  HANDLE file = CreateFileW(tempPath, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, 0);
  if (file == INVALID_HANDLE_VALUE) return kSaveErrOpen;
  bool ok = true;
  const uint8_t* p = data.Data();
  size_t left = data.Size();
  while (ok && left) {
    DWORD chunk = left > (1u << 24) ? (1u << 24) : (DWORD)left;
    DWORD written = 0;
    ok = WriteFile(file, p, chunk, &written, 0) && written == chunk;
    p += chunk;
    left -= chunk;
  }
  if (!CloseHandle(file)) ok = false;
  if (!ok || !MoveFileExW(tempPath, path, MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileW(tempPath);
    return kSaveErrWrite;
  }
  return kSaveOk;
}

SaveResult LoadGame(const wchar_t* path, GameState* game) {
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  if (file == INVALID_HANDLE_VALUE) return kSaveErrOpen;
  DWORD high = 0;
  DWORD size = GetFileSize(file, &high);
  if ((size == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) || high ||
      size > kMaxFileSize) {
    CloseHandle(file);
    return high || size > kMaxFileSize ? kSaveErrMemory : kSaveErrRead;
  }
  ByteBuffer data;
  uint8_t* p = data.Grow(size);
  if (!p) {
    CloseHandle(file);
    return kSaveErrMemory;
  }
  DWORD done = 0;
  while (done < size) {
    DWORD got = 0;
    if (!ReadFile(file, p + done, size - done, &got, 0) || !got) break;
    done += got;
  }
  CloseHandle(file);
  if (done != size) return kSaveErrRead;
  return DeserializeGame(data.Data(), data.Size(), game);
}

const wchar_t* SaveResultText(SaveResult result) {
  switch (result) {
    case kSaveOk: return Tr(kStrSaveOk);
    case kSaveErrOpen: return Tr(kStrSaveErrOpen);
    case kSaveErrWrite: return Tr(kStrSaveErrWrite);
    case kSaveErrRead: return Tr(kStrSaveErrRead);
    case kSaveErrFormat: return Tr(kStrSaveErrFormat);
    case kSaveErrVersion: return Tr(kStrSaveErrVersion);
    case kSaveErrMemory: return Tr(kStrSaveErrMemory);
  }
  return Tr(kStrSaveErrUnknown);
}

bool GetSavesDir(wchar_t* buffer, bool create) {
  DWORD len = GetModuleFileNameW(0, buffer, MAX_PATH);
  if (!len || len >= MAX_PATH) return false;
  while (len && buffer[len - 1] != L'\\' && buffer[len - 1] != L'/') --len;
  if (len + 6 >= MAX_PATH) return false;
  lstrcpyW(buffer + len, L"saves");
  if (create) CreateDirectoryW(buffer, 0);
  return true;
}

bool PromptSavePath(HWND owner, wchar_t* path) {
  wchar_t dir[MAX_PATH] = L"", filter[kFilterSize];
  GetSavesDir(dir, true);
  if (!path[0]) MakeDefaultSaveName(path);
  MakeFileFilter(filter);

  OPENFILENAMEW ofn;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrInitialDir = dir;
  ofn.lpstrDefExt = L"feo";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
              OFN_NOCHANGEDIR;
  return GetSaveFileNameW(&ofn) != 0;
}

bool PromptLoadPath(HWND owner, wchar_t* path) {
  wchar_t dir[MAX_PATH] = L"", filter[kFilterSize];
  GetSavesDir(dir, false);
  path[0] = 0;
  MakeFileFilter(filter);

  OPENFILENAMEW ofn;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrInitialDir = dir;
  ofn.lpstrDefExt = L"feo";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
              OFN_NOCHANGEDIR;
  return GetOpenFileNameW(&ofn) != 0;
}
