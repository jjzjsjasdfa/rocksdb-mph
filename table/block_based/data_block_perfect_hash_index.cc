#include "table/block_based/data_block_perfect_hash_index.h"

#include <string>
#include <vector>

#include "rocksdb/slice.h"
#include "util/coding.h"
#include "util/hash.h"
#include "perfect_hash.h"

namespace ROCKSDB_NAMESPACE {

void DataBlockPerfectHashIndexBuilder::Add(const Slice& key,
                                           const size_t restart_index) {
  assert(Valid());
  if (restart_index > kMaxRestartSupportedByHashIndex) {
    valid_ = false;
    return;
  }
  kv_pairs.emplace_back(key, static_cast<uint8_t>(restart_index));
}

void DataBlockPerfectHashIndexBuilder::Finish(std::string& buffer) {
  assert(Valid());

  ht.Serialize(buffer);

  assert(buffer.size() <= kMaxBlockSizeSupportedByHashIndex);
}

void DataBlockPerfectHashIndexBuilder::Reset() {
  valid_ = true;
  kv_pairs.clear();
}

void DataBlockHashIndex::Initialize(const char* data, uint16_t size,
                                    uint16_t* map_offset) {
  assert(size >= sizeof(uint64_t));  // At least MPH size

  uint64_t mph_size = DecodeFixed64(data + size - sizeof(uint64_t));
  assert(mph_size <= size - sizeof(uint64_t));


  *map_offset = static_cast<uint16_t>(size - sizeof(uint64_t) - mph_size);
  const char* mph_ptr = data + *map_offset;


  ht.Initialize(mph_ptr, static_cast<size_t>(mph_size));
}

uint8_t DataBlockHashIndex::Lookup(const char* data, uint32_t map_offset,
                                   const Slice& key) const {
  auto restart_opt = ht.get(key.ToString());
  if (!restart_opt.has_value()) {
    return kNoEntry;
  }

  int restart_index = restart_opt.value();
  return static_cast<uint8_t>(restart_index);
}

}  // namespace ROCKSDB_NAMESPACE
