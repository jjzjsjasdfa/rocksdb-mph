#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rocksdb/slice.h"
#include "table/block_based/perfect_hash.h"

namespace ROCKSDB_NAMESPACE {

class DataBlockPerfectHashIndexBuilder {
 public:
  DataBlockPerfectrHashIndexBuilder()
      : valid_(false) {}

  void Initialize() {
    valid_ = true;
    kv_pairs.clear();
  }

  inline bool Valid() const { return valid_; }
  void Add(const Slice& key, const size_t restart_index);
  void Finish(std::string& buffer);
  void Reset();
  inline size_t EstimateSize() const {
    return 2 * sizeof(uint64_t) + 3 * sizeof(uint32_t) + 3 * sizeof(uint8_t) +
           kv_pairs.size() / 4 * sizeof(uint32_t) + kv_pairs.size() * sizezof(uint8_t);
  }

 private:
  bool valid_;

  std::vector<std::pair<uint32_t, uint8_t>> kv_pairs;
  friend class DataBlockHashIndex_DataBlockHashTestSmall_Test;
};

class DataBlockPerfectHashIndex {
 public:
  void Initialize(const char* data, uint16_t size, uint16_t* map_offset);

  uint8_t Lookup(const char* data, uint32_t map_offset, const Slice& key) const;

  inline bool Valid() { return ht.size() != 0; }

 private:
  // To make the serialized hash index compact and to save the space overhead,
  // here all the data fields persisted in the block are in uint16 format.
  // We find that a uint16 is large enough to index every offset of a 64KiB
  // block.
  // So in other words, DataBlockHashIndex does not support block size equal
  // or greater then 64KiB.

  PerfectHashTable ht;
};

}  // namespace ROCKSDB_NAMESPACE
