/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_OB_MICRO_BLOCK_HASH_INDEX_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_OB_MICRO_BLOCK_HASH_INDEX_H_

#include "lib/oblog/ob_log_module.h"
#include "ob_data_buffer.h"
// #include "ob_data_store_desc.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObMergeSchema;
}
}
namespace blocksstable
{
struct ObDataStoreDesc;
struct ObMicroBlockData;
class ObMicroBufferWriter;
class ObMicroBlockHashIndex
{
public:  
  static const uint8_t NO_ENTRY = 255;
  static const uint8_t COLLISION = 254;
  static const uint8_t MAX_OFFSET_SUPPORTED = 253;
  static const uint8_t RESERVED_BYTE = 0;
  static constexpr double DEFAULT_UTIL_RATIO = 0.75;
  static constexpr double BUCKET_PER_KEY = 1 / DEFAULT_UTIL_RATIO;
  static const uint32_t MAX_BUCKET_NUMBER = static_cast<uint32_t>(BUCKET_PER_KEY * MAX_OFFSET_SUPPORTED) | 1;
  static constexpr double MAX_COLLISION_RATIO = 1.5;
  static const uint32_t MIN_ROWS_BUILD_HASH_INDEX = 16;
  static const uint32_t MIN_INT_COLUMNS_NEEDED = 3;
public:
  ObMicroBlockHashIndex()
    : is_inited_(false),
      num_buckets_(0),
      bucket_table_(nullptr)
  {
  }
  OB_INLINE uint8_t find(const uint64_t hash_value) const
  {
    const uint16_t idx = static_cast<uint16_t>(
                             static_cast<uint32_t>(hash_value) % num_buckets_);
    return bucket_table_[idx];
  }
  OB_INLINE bool is_inited()
  {
    return is_inited_;
  }
  OB_INLINE void reset()
  {
    is_inited_ = false;
  }
  OB_INLINE void reuse()
  {
    reset();
  }
  OB_INLINE static uint32_t get_serialize_size(uint32_t num_bucket) 
  {
    return sizeof(uint8_t) * num_bucket + get_fixed_header_size();
  }
  OB_INLINE static uint32_t get_fixed_header_size() 
  {
    // reserved byte(1 byte) + num_buckets(2 bytes).
    return sizeof(uint16_t) + sizeof(uint8_t);
  }
  OB_INLINE static uint32_t hash_index_size(const char *data)
  {
    uint32_t num_bucket = reinterpret_cast<const uint16_t *>(data + 1)[0];
    return get_serialize_size(num_bucket);
  }
  int init(const ObMicroBlockData &micro_block_data);
public:
  bool is_inited_;
  uint16_t num_buckets_;
  const uint8_t *bucket_table_;
};

class ObMicroBlockHashIndexBuilder
{
public:
  ObMicroBlockHashIndexBuilder()
    : count_(0),
      row_index_(0),
      last_key_with_L_flag_(false),
      data_store_desc_(nullptr),
      is_inited_(false)
  {
  }
  ~ObMicroBlockHashIndexBuilder() {}
  int init_if_needed(const ObDataStoreDesc *data_store_desc);
  OB_INLINE bool is_valid() const { return is_inited_; }
  OB_INLINE void reset()
  {
    row_index_ = 0;
    count_ = 0;
    last_key_with_L_flag_ = false;
    is_inited_ = false;
  }
  OB_INLINE bool is_empty() const { return 0 == count_; }
  OB_INLINE uint16_t caculate_bucket_number(uint32_t count) const
  {
    uint16_t estimated_num_buckets =
                 static_cast<uint16_t>(count * ObMicroBlockHashIndex::BUCKET_PER_KEY);
    estimated_num_buckets |= 1;
    return estimated_num_buckets;
  }
  OB_INLINE int64_t estimate_size(bool plus_one = false) const
  {
    int64_t size = 0;
    if (is_valid()) {
      const uint32_t count = plus_one ? (count_ + 1) : count_;
      if (count > ObMicroBlockHashIndex::MIN_ROWS_BUILD_HASH_INDEX) {
        uint16_t estimated_num_buckets = caculate_bucket_number(count);
        size = ObMicroBlockHashIndex::get_serialize_size(estimated_num_buckets);
      }
    }
    return size;
  }
  OB_INLINE void reuse()
  {
    row_index_ = 0;
    count_ = 0;
    last_key_with_L_flag_ = false;
    is_inited_ = true;
  }
  int add(const ObDatumRow &row);
  int build_block(ObMicroBufferWriter &buffer);
private:
  int check_need_build_hash_index(const ObDataStoreDesc &data_store_desc, bool &need_build);
  bool can_be_added_to_hash_index(const ObDatumRow &row);
  int internal_add(const uint64_t hash_value, const uint32_t row_index);
private:
  uint32_t count_;
  uint32_t row_index_;
  bool last_key_with_L_flag_;
  const ObDataStoreDesc *data_store_desc_;
  bool is_inited_; 
  uint8_t buckets_[ObMicroBlockHashIndex::MAX_BUCKET_NUMBER];
  uint8_t row_indexes_[ObMicroBlockHashIndex::MAX_OFFSET_SUPPORTED];
  uint32_t hash_values_[ObMicroBlockHashIndex::MAX_OFFSET_SUPPORTED];
};

} // end namespace blocksstable
} // end namespace oceanbase

#endif // OCEANBASE_STORAGE_BLOCKSSTABLE_OB_MICRO_BLOCK_HASH_INDEX_H_
