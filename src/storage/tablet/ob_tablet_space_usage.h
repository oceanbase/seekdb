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

#ifndef OCEANBASE_STORAGE_TABLET_OB_TABLET_SPACE_USAGE_H_
#define OCEANBASE_STORAGE_TABLET_OB_TABLET_SPACE_USAGE_H_

#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace storage
{
struct ObTabletSpaceUsage final
{
public:
  ObTabletSpaceUsage()
    : all_sstable_data_occupy_size_(0), 
      all_sstable_data_required_size_(0), 
      all_sstable_meta_size_(0), 
      tablet_clustered_meta_size_(0)
  {
  }
  void reset()
  {
    all_sstable_data_occupy_size_ = 0;
    all_sstable_data_required_size_ = 0;
    all_sstable_meta_size_ = 0;
    tablet_clustered_meta_size_ = 0;
  }
  TO_STRING_KV(K_(all_sstable_data_occupy_size), K_(all_sstable_data_required_size), K_(all_sstable_meta_size), K_(tablet_clustered_meta_size));
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t data_len, int64_t &pos);
  int32_t get_serialize_size() const;
  bool is_valid() const 
  {
    return (OB_INVALID_SIZE != all_sstable_data_occupy_size_) 
        && (OB_INVALID_SIZE != all_sstable_data_required_size_)
        && (OB_INVALID_SIZE != all_sstable_meta_size_)
        && (OB_INVALID_SIZE != tablet_clustered_meta_size_);
  }
public:
  static const int32_t TABLET_SPACE_USAGE_INFO_VERSION = 1;
public:
  // All SSTable data block bytes currently occupied.
  int64_t all_sstable_data_occupy_size_;
  // All SSTable data required size: data_block_count * macro block size.
  int64_t all_sstable_data_required_size_;
  // sstable_meta_block_count * 2MB
  int64_t all_sstable_meta_size_;
  int64_t tablet_clustered_meta_size_;
};
}
}

#endif
