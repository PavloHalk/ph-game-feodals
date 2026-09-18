// Minimal POD containers without STL: keeps the executable small and works
// with -fno-exceptions / /EHs-c-. No Win32 dependency.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Packed cell coordinate: x in the high 32 bits, y in the low 32 bits.
inline uint64_t MakeCellKey(uint32_t x, uint32_t y) { return ((uint64_t)x << 32) | y; }
inline uint32_t CellKeyX(uint64_t key) { return (uint32_t)(key >> 32); }
inline uint32_t CellKeyY(uint64_t key) { return (uint32_t)key; }

// Growable array of trivially copyable values.
template <typename T>
class PodVec {
 public:
  PodVec() : data_(0), size_(0), cap_(0) {}
  ~PodVec() { free(data_); }

  bool Push(const T& value) {
    if (size_ == cap_) {
      size_t newCap = cap_ ? cap_ * 2 : 64;
      if (newCap > ((size_t)-1) / sizeof(T) / 2) return false;
      T* newData = (T*)realloc(data_, newCap * sizeof(T));
      if (!newData) return false;
      data_ = newData;
      cap_ = newCap;
    }
    data_[size_++] = value;
    return true;
  }

  size_t Size() const { return size_; }
  const T& operator[](size_t i) const { return data_[i]; }
  T& operator[](size_t i) { return data_[i]; }

 private:
  PodVec(const PodVec&);
  void operator=(const PodVec&);

  T* data_;
  size_t size_;
  size_t cap_;
};

// Open-addressing (linear probing) hash map: packed cell key -> 8-bit value.
// Absent key == unclaimed cell. Entries are never erased, which keeps probing
// simple (no tombstones).
class CellMap {
 public:
  CellMap() : keys_(0), vals_(0), cap_(0), count_(0) {}
  ~CellMap() { Free(); }

  void Free() {
    free(keys_);
    free(vals_);
    keys_ = 0;
    vals_ = 0;
    cap_ = 0;
    count_ = 0;
  }

  size_t Count() const { return count_; }
  size_t Capacity() const { return cap_; }
  bool SlotUsed(size_t i) const { return keys_[i] != EmptyKey(); }
  uint64_t SlotKey(size_t i) const { return keys_[i]; }
  uint8_t SlotValue(size_t i) const { return vals_[i]; }

  // Returns the stored value, or -1 if the key is absent.
  int Get(uint64_t key) const {
    if (!cap_) return -1;
    size_t mask = cap_ - 1;
    size_t i = Hash(key) & mask;
    for (;;) {
      uint64_t k = keys_[i];
      if (k == key) return vals_[i];
      if (k == EmptyKey()) return -1;
      i = (i + 1) & mask;
    }
  }

  // Makes room for `n` entries in total without further allocation.
  bool Reserve(size_t n) {
    size_t need = 16;
    while (need / 10 * 7 < n) {
      if (need > ((size_t)-1) / 32) return false;
      need <<= 1;
    }
    return need <= cap_ || Rehash(need);
  }

  // Inserts or overwrites. Returns the previous value, -1 if the key was new,
  // or -2 if memory could not be allocated (map left unchanged).
  int Set(uint64_t key, uint8_t value) {
    if (!Reserve(count_ + 1)) return -2;
    size_t mask = cap_ - 1;
    size_t i = Hash(key) & mask;
    for (;;) {
      uint64_t k = keys_[i];
      if (k == key) {
        int old = vals_[i];
        vals_[i] = value;
        return old;
      }
      if (k == EmptyKey()) {
        keys_[i] = key;
        vals_[i] = value;
        ++count_;
        return -1;
      }
      i = (i + 1) & mask;
    }
  }

  // Makes this map an exact copy of `other`. Returns false on out of memory
  // (this map is left empty then).
  bool CopyFrom(const CellMap& other) {
    if (this == &other) return true;
    Free();
    if (!other.cap_) return true;
    keys_ = (uint64_t*)malloc(other.cap_ * sizeof(uint64_t));
    vals_ = (uint8_t*)malloc(other.cap_);
    if (!keys_ || !vals_) {
      Free();
      return false;
    }
    memcpy(keys_, other.keys_, other.cap_ * sizeof(uint64_t));
    memcpy(vals_, other.vals_, other.cap_);
    cap_ = other.cap_;
    count_ = other.count_;
    return true;
  }

  void Swap(CellMap& other) {
    uint64_t* k = keys_; keys_ = other.keys_; other.keys_ = k;
    uint8_t* v = vals_; vals_ = other.vals_; other.vals_ = v;
    size_t c = cap_; cap_ = other.cap_; other.cap_ = c;
    size_t n = count_; count_ = other.count_; other.count_ = n;
  }

 private:
  CellMap(const CellMap&);
  void operator=(const CellMap&);

  static uint64_t EmptyKey() { return ~(uint64_t)0; }

  static size_t Hash(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    return (size_t)k;
  }

  bool Rehash(size_t newCap) {
    uint64_t* newKeys = (uint64_t*)malloc(newCap * sizeof(uint64_t));
    uint8_t* newVals = (uint8_t*)malloc(newCap);
    if (!newKeys || !newVals) {
      free(newKeys);
      free(newVals);
      return false;
    }
    memset(newKeys, 0xFF, newCap * sizeof(uint64_t));
    size_t mask = newCap - 1;
    for (size_t i = 0; i < cap_; ++i) {
      if (keys_[i] == EmptyKey()) continue;
      size_t j = Hash(keys_[i]) & mask;
      while (newKeys[j] != EmptyKey()) j = (j + 1) & mask;
      newKeys[j] = keys_[i];
      newVals[j] = vals_[i];
    }
    free(keys_);
    free(vals_);
    keys_ = newKeys;
    vals_ = newVals;
    cap_ = newCap;
    return true;
  }

  uint64_t* keys_;
  uint8_t* vals_;
  size_t cap_;
  size_t count_;
};
