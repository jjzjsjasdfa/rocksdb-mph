#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#define XXH_INLINE_ALL
#include "xxhash.h"
#include <iostream>
#include "util/coding.h"
#include "rocksdb/slice.h"
#include "util/hash.h"

class PerfectHashTable {
 private:
      static constexpr size_t STRIDE = 4;
      std::vector<uint64_t> bitmap;
      std::vector<uint32_t> bitmap_offsets;
      std::vector<uint8_t> bitmap_seeds;
      std::vector<uint32_t> rank_vec;
      std::vector<uint8_t> values;
      size_t n_bits = 0;

 public:
    using kv_pair = std::pair<uint32_t, uint8_t>;

    PerfectHashTable(const std::vector<kv_pair>& items) {
        build(items);
    }

    bool bit_check(size_t bit_pos) const {
        return (this->bitmap[bit_pos / 64] >> bit_pos % 64) & 1ULL;
    }

    std::optional<int> get(const std::string& key) const {
        for (size_t i = 0; i < bitmap_offsets.size(); i++) {
            uint64_t h = XXH64(key.data(), key.size(), bitmap_seeds[i]);
            size_t bm_size = i == bitmap_offsets.size() - 1 
                // last chunk
                ? this->n_bits - bitmap_offsets[i] 
                : bitmap_offsets[i + 1] - bitmap_offsets[i];
            size_t bit_pos = (h % bm_size) + bitmap_offsets[i];
            if (bit_check(bit_pos)) {
                size_t rank = rank_vec[bit_pos / STRIDE];
                for (size_t j = 0; j < bit_pos % STRIDE; j++) {
                    if (bit_check((bit_pos / STRIDE) * STRIDE + j)) {
                        rank++;
                    }
                }
                return values[rank];
            }
        }
        return std::nullopt;
    }

    void build(const std::vector<kv_pair>& items) {
        std::vector<kv_pair> remaining = items;
        std::vector<std::vector<std::vector<kv_pair>>> temp_bms;
        size_t offset = 0;
        size_t seed = 0;
        
        while (!remaining.empty()) {
            
            for (size_t i = 0; i < remaining.size(); ++i) {
                std::cout << "Remaining " << i << ":\n";
                std::cout << "  " << remaining[i].first << " -> " << remaining[i].second << "\n";
            }
            
            size_t m = remaining.size();
            std::vector<std::vector<kv_pair>> temp_bm;
            temp_bm.assign(m, {});

            for (const auto& kv : remaining) {
                uint64_t h = XXH64(kv.first, kv.first.size(), seed);
                size_t idx = h % m;
                temp_bm[idx].push_back(kv);
            }

            std::vector<kv_pair> next;
            
            bool success = false;
            for (size_t i = 0; i < m; i++) {
                if (temp_bm[i].size() == 1) {
                    // store the values in this order
                    this->values.push_back(temp_bm[i][0].second);

                    // We have successfully uniquely hash at least one key
                    success = true;
                }
                else {
                    for (const auto& kv : temp_bm[i]) {
                        next.push_back(kv);
                    }
                }
            }

            if (!success) {
                seed++;
                continue;
            }
            else {
                std::cout << "Seed is " << seed << std::endl;
                bitmap_seeds.push_back(seed);
                offset += m;
                remaining.swap(next);
                temp_bms.push_back(temp_bm);
            }
        }

        // build the actual bitmap
        this->n_bits = offset;
        size_t n_words = (this->n_bits + 63) / 64;
        uint64_t word = 0ULL;

        for (int i = 0; i < n_words; i++) {
            this->bitmap.push_back(word);
        }

        int rank = 0;
        int j = 0;
        for (const auto& bm : temp_bms) {
            this->bitmap_offsets.push_back(j);
            for (const auto& bucket : bm) {
                // construct rank_vec
                // the rank sum up util the previous element
                if (j % STRIDE == 0) {
                    this->rank_vec.push_back(rank);
                }

                // construct bitmap
                if (bucket.size() == 1) {
                    bitmap[j / 64] |= 1ULL << (j % 64);
                    rank++;
                }

                j++;
            }
        }
    }

    void Serialize(std::string& buffer) const {
      std::string tmp;

      // 1. bitmap
      rocksdb::PutFixed32(&tmp, static_cast<uint32_t>(bitmap.size()));
      for (uint64_t word : bitmap)
        tmp.append(reinterpret_cast<const char*>(&word), sizeof(word));

      // 2. bitmap_offsets
      rocksdb::PutFixed32(&tmp, static_cast<uint32_t>(bitmap_offsets.size()));
      for (uint32_t offset : bitmap_offsets) rocksdb::PutFixed32(&tmp, offset);

      // 3. bitmap_seeds
      rocksdb::PutFixed32(&tmp, static_cast<uint32_t>(bitmap_seeds.size()));
      tmp.append(reinterpret_cast<const char*>(bitmap_seeds.data()),
                 bitmap_seeds.size());

      // 4. rank_vec
      rocksdb::PutFixed32(&tmp, static_cast<uint32_t>(rank_vec.size()));
      for (uint32_t r : rank_vec) rocksdb::PutFixed32(&tmp, r);

      // 5. values
      rocksdb::PutFixed32(&tmp, static_cast<uint32_t>(values.size()));
      tmp.append(reinterpret_cast<const char*>(values.data()), values.size());

      // 6. n_bits
      rocksdb::PutFixed64(&tmp, n_bits);

      buffer.append(tmp);
      rocksdb::PutFixed64(&buffer, tmp.size());
    }

    void Initialize(const char* data, size_t size) {
      const char* ptr = data;
      const char* end = data + size;

      // 1. bitmap
      assert(ptr + sizeof(uint32_t) <= end);
      uint32_t bitmap_size = rocksdb::DecodeFixed32(ptr);
      ptr += sizeof(uint32_t);
      bitmap.resize(bitmap_size);
      for (uint32_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = rocksdb::DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
      }

      // 2. bitmap_offsets
      assert(ptr + sizeof(uint32_t) <= end);
      uint32_t offsets_size = rocksdb::DecodeFixed32(ptr);
      ptr += sizeof(uint32_t);
      bitmap_offsets.resize(offsets_size);
      for (uint32_t i = 0; i < offsets_size; i++) {
        assert(ptr + sizeof(uint32_t) <= end);
        bitmap_offsets[i] = rocksdb::DecodeFixed32(ptr);
        ptr += sizeof(uint32_t);
      }

      // 3. bitmap_seeds
      assert(ptr + sizeof(uint32_t) <= end);
      uint32_t seeds_size = rocksdb::DecodeFixed32(ptr);
      ptr += sizeof(uint32_t);
      bitmap_seeds.resize(seeds_size);
      std::memcpy(bitmap_seeds.data(), ptr, seeds_size);
      ptr += seeds_size;

      // 4. rank_vec
      assert(ptr + sizeof(uint32_t) <= end);
      uint32_t rank_size = rocksdb::DecodeFixed32(ptr);
      ptr += sizeof(uint32_t);
      rank_vec.resize(rank_size);
      for (uint32_t i = 0; i < rank_size; i++) {
        assert(ptr + sizeof(uint32_t) <= end);
        rank_vec[i] = rocksdb::DecodeFixed32(ptr);
        ptr += sizeof(uint32_t);
      }

      // 5. values
      assert(ptr + sizeof(uint32_t) <= end);
      uint32_t values_size = rocksdb::DecodeFixed32(ptr);
      ptr += sizeof(uint32_t);
      values.resize(values_size);
      assert(ptr + values_size <= end);
      std::memcpy(values.data(), ptr, values_size);
      ptr += values_size;

      // 6. n_bits
      assert(ptr + sizeof(uint64_t) <= end);
      n_bits = rocksdb::DecodeFixed64(ptr);
      ptr += sizeof(uint64_t);

      assert(static_cast<size_t>(ptr - data) <= size);
    }
};