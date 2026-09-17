// Little-endian byte buffers for the save format and the network protocol.
// No Win32 dependency.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Growable byte array with a consumed prefix, so it also works as a FIFO for
// socket output without moving data on every partial send.
class ByteBuffer {
 public:
  ByteBuffer() : data_(0), start_(0), size_(0), cap_(0), ok_(true) {}
  ~ByteBuffer() { free(data_); }

  const uint8_t* Data() const { return data_ + start_; }
  uint8_t* Data() { return data_ + start_; }
  size_t Size() const { return size_ - start_; }
  bool Ok() const { return ok_; }  // false after a failed allocation

  void Clear() {
    start_ = size_ = 0;
    ok_ = true;
  }

  void Free() {
    free(data_);
    data_ = 0;
    start_ = size_ = cap_ = 0;
    ok_ = true;
  }

  // Drops `n` bytes from the front.
  void Consume(size_t n) {
    start_ += n;
    if (start_ >= size_) start_ = size_ = 0;
  }

  bool Bytes(const void* bytes, size_t n) {
    if (!n) return ok_;
    if (!Reserve(Size() + n)) return false;
    memcpy(data_ + size_, bytes, n);
    size_ += n;
    return true;
  }

  // Appends `n` uninitialized bytes and returns a pointer to them.
  uint8_t* Grow(size_t n) {
    if (!Reserve(Size() + n)) return 0;
    uint8_t* p = data_ + size_;
    size_ += n;
    return p;
  }

  void U8(uint32_t v) {
    uint8_t b = (uint8_t)v;
    Bytes(&b, 1);
  }
  void U16(uint32_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    Bytes(b, 2);
  }
  void U32(uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16),
                    (uint8_t)(v >> 24)};
    Bytes(b, 4);
  }
  void U64(uint64_t v) {
    U32((uint32_t)v);
    U32((uint32_t)(v >> 32));
  }

  // Overwrites 4 bytes at `offset` (relative to Data()).
  void PatchU32(size_t offset, uint32_t v) {
    uint8_t* p = Data() + offset;
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
  }

 private:
  ByteBuffer(const ByteBuffer&);
  void operator=(const ByteBuffer&);

  bool Reserve(size_t needed) {
    if (!ok_) return false;
    if (start_ && start_ + needed > cap_) {  // compact first
      memmove(data_, data_ + start_, size_ - start_);
      size_ -= start_;
      start_ = 0;
    }
    if (needed <= cap_) return true;
    size_t newCap = cap_ ? cap_ : 256;
    while (newCap < needed) {
      if (newCap > ((size_t)-1) / 2) {
        ok_ = false;
        return false;
      }
      newCap *= 2;
    }
    uint8_t* p = (uint8_t*)realloc(data_, newCap);
    if (!p) {
      ok_ = false;
      return false;
    }
    data_ = p;
    cap_ = newCap;
    return true;
  }

  uint8_t* data_;
  size_t start_, size_, cap_;
  bool ok_;
};

// Bounds-checked sequential reader. After any overrun Ok() stays false and
// every read returns zeros.
class ByteReader {
 public:
  ByteReader(const uint8_t* data, size_t size)
      : data_(data), size_(size), pos_(0), ok_(true) {}

  bool Ok() const { return ok_; }
  size_t Remaining() const { return size_ - pos_; }
  const uint8_t* Current() const { return data_ + pos_; }

  bool Bytes(void* out, size_t n) {
    if (!ok_ || n > size_ - pos_) {
      ok_ = false;
      memset(out, 0, n);
      return false;
    }
    memcpy(out, data_ + pos_, n);
    pos_ += n;
    return true;
  }

  bool Skip(size_t n) {
    if (!ok_ || n > size_ - pos_) return ok_ = false;
    pos_ += n;
    return true;
  }

  uint32_t U8() {
    uint8_t b[1];
    Bytes(b, 1);
    return b[0];
  }
  uint32_t U16() {
    uint8_t b[2];
    Bytes(b, 2);
    return b[0] | (b[1] << 8);
  }
  uint32_t U32() {
    uint8_t b[4];
    Bytes(b, 4);
    return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
  }
  uint64_t U64() {
    uint64_t low = U32();
    return low | ((uint64_t)U32() << 32);
  }

 private:
  const uint8_t* data_;
  size_t size_, pos_;
  bool ok_;
};
