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

#ifndef OCEANBASE_SQL_OB_BINARY_AGGREGATE
#define OCEANBASE_SQL_OB_BINARY_AGGREGATE

#include "lib/container/ob_array_iterator.h"
#include "common/xml/ob_multi_mode_interface.h"
#include "common/xml/ob_multi_mode_bin.h"
#include "common/json_type/ob_json_bin.h"

namespace oceanbase {
namespace common {

enum ObBinAggAllocFlag {
  AGG_ALLOC_A,
  AGG_ALLOC_B,
  AGG_ALLOC_MAX
};

struct ObAggBinKeyInfo {
  uint8_t type_;
  bool is_duplicate_;
  uint64_t value_offset_;
  uint64_t value_len_;
  uint64_t offset_;
  uint32_t key_len_;
  TO_STRING_KV(K(type_),
               K(is_duplicate_),
               K(value_offset_),
               K(offset_),
               K(key_len_));
};

typedef common::ObArray<ObAggBinKeyInfo*> ObAggBinKeyArray;

class ObJsonBinAggSerializer {
public:
  ObJsonBinAggSerializer(ObIAllocator *allocator,
                         uint8_t header_type,
                         ObIAllocator *back_allocator = nullptr,
                         ObIAllocator *arr_allocator = nullptr);

  int serialize();
  int append_key_and_value(ObString key, ObStringBuffer &value, ObJsonBin *json_val);
  void set_sort_and_unique() { sort_and_unique_ = true; }
  ObStringBuffer *get_buffer() { return &buff_; }
  int64_t get_key_info_count() { return key_info_.count(); }
  int64_t get_last_count() { return count_; }
  int64_t get_approximate_length() { return key_.length() + value_.length(); }

private:
  int construct_meta();
  int construct_key_and_value();
  int rewrite_total_size();
  int construct_header();
  void do_json_sort();
  int reserve_meta();
  int copy_and_reset(ObIAllocator *new_allocator,
                     ObIAllocator *old_allocator,
                     ObStringBuffer &add_value);
  bool has_unique_flag() { return sort_and_unique_; }
  bool is_json_array() { return header_type_ == static_cast<uint8_t>(ObJsonNodeType::J_ARRAY); }
  bool json_not_sort()
  {
    return header_type_ == static_cast<uint8_t>(ObJsonNodeType::J_OBJECT) && !sort_and_unique_;
  }
  bool first_alloc_flag() { return alloc_flag_ == ObBinAggAllocFlag::AGG_ALLOC_A; }
  void set_first_alloc() { alloc_flag_ = ObBinAggAllocFlag::AGG_ALLOC_A; }
  void set_second_alloc() { alloc_flag_ = ObBinAggAllocFlag::AGG_ALLOC_B; }
  bool check_three_allocator() { return back_allocator_ == nullptr || arr_allocator_ == nullptr; }
  ObIAllocator *get_array_allocator() { return arr_allocator_ == nullptr ? allocator_ : arr_allocator_; }
  void set_key_entry(int64_t entry_idx, int64_t key_offset, int64_t key_len);
  void set_value_entry(int64_t entry_idx, uint8_t type, int64_t value_offset);
  int set_key(int64_t key_offset, int64_t key_len);
  int set_value(int64_t value_offset, int64_t value_len);
  static int64_t estimate_total(int64_t base_length, int64_t count);
  static constexpr int REPLACE_MEMORY_SIZE_THRESHOLD = 8 << 20;

private:
  ObStringBuffer value_;
  ObStringBuffer key_;
  ObStringBuffer buff_;
  ObMulBinHeaderSerializer header_;
  bool sort_and_unique_;
  uint8_t header_type_;
  uint8_t alloc_flag_;
  int64_t key_len_;
  int64_t value_len_;
  int64_t count_;
  int64_t index_start_;
  int64_t key_entry_start_;
  int8_t key_entry_size_;
  int64_t value_entry_start_;
  int8_t value_entry_size_;
  int64_t key_start_;
  ObIAllocator *allocator_;
  ObIAllocator *back_allocator_;
  ObIAllocator *arr_allocator_;
  ModulePageAllocator page_allocator_;
  ObAggBinKeyArray key_info_;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SQL_OB_BINARY_AGGREGATE
