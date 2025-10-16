#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rocksdb/slice.h"
#include "perfect_hash.h"

namespace ROCKSDB_NAMESPACE {
// This is an experimental feature aiming to reduce the CPU utilization of
// point-lookup within a data-block. It is only used in data blocks, and not
// in meta-data blocks or per-table index blocks.
//
// It only used to support BlockBasedTable::Get().
//
// A serialized hash index is appended to the data-block. The new block data
// format is as follows:
//
// DATA_BLOCK: [RI RI RI ... RI RI_IDX HASH_IDX FOOTER]
//
// RI:       Restart Interval (the same as the default data-block format)
// RI_IDX:   Restart Interval index (the same as the default data-block format)
// HASH_IDX: The new data-block hash index feature.
// FOOTER:   A 32bit block footer, which is the NUM_RESTARTS with the MSB as
//           the flag indicating if this hash index is in use. Note that
//           given a data block < 32KB, the MSB is never used. So we can
//           borrow the MSB as the hash index flag. Therefore, this format is
//           compatible with the legacy data-blocks with num_restarts < 32768,
//           as the MSB is 0.
//
// The format of the data-block hash index is as follows:
//
// HASH_IDX: [B B B ... B NUM_BUCK]
//
// B:         bucket, an array of restart index. Each buckets is uint8_t.
// NUM_BUCK:  Number of buckets, which is the length of the bucket array.
//
// We reserve two special flag:
//    kNoEntry=255,
//    kCollision=254.
//
// Therefore, the max number of restarts this hash index can supoport is 253.
//
// Buckets are initialized to be kNoEntry.
//
// When storing a key in the hash index, the key is first hashed to a bucket.
// If there the bucket is empty (kNoEntry), the restart index is stored in
// the bucket. If there is already a restart index there, we will update the
// existing restart index to a collision marker (kCollision). If the
// the bucket is already marked as collision, we do not store the restart
// index either.
//
// During query process, a key is first hashed to a bucket. Then we examine if
// the buckets store nothing (kNoEntry) or the bucket had a collision
// (kCollision). If either of those happens, we get the restart index of
// the key and will directly go to the restart interval to search the key.
//
// Note that we only support blocks with #restart_interval < 254. If a block
// has more restart interval than that, hash index will not be create for it.

// Because we use uint16_t address, we only support block no more than 64KB
const size_t kMaxBlockSizeSupportedByHashIndex = 1u << 16;

class DataBlockPerfectHashIndexBuilder {
 public:
  DataBlockPerfectrHashIndexBuilder()
      : valid_(false) {}

  void Initialize() {
    valid_ = true;
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
