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

#ifndef OCEABASE_STORAGE_LOB_OB_LOB_DIFF_STRUCT_H_
#define OCEABASE_STORAGE_LOB_OB_LOB_DIFF_STRUCT_H_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace storage
{

struct ObLobDiffFlags
{
  ObLobDiffFlags() : can_do_append_(0), reserve_(0)
  {}
  TO_STRING_KV(K_(can_do_append), K_(reserve));
  uint64_t can_do_append_ : 1; // can do append in write situation
  uint64_t reserve_ : 63;
};

struct ObLobDiff
{
  enum DiffType
  {
    INVALID = 0,
    APPEND = 1,
    WRITE = 2,
    ERASE = 3,
    ERASE_FILL_ZERO = 4,
    WRITE_DIFF = 5,
  };
  ObLobDiff()
    : type_(DiffType::INVALID), ori_offset_(0), ori_len_(0), offset_(0), byte_len_(0), dst_offset_(0), dst_len_(0),
      flags_()
  {}
  TO_STRING_KV(K_(type), K_(ori_offset), K_(ori_len), K_(offset), K_(byte_len), K_(dst_offset), K_(dst_len),
               K_(flags));
  DiffType type_;
  uint64_t ori_offset_;
  uint64_t ori_len_; // for diff, char_len
  uint64_t offset_;
  uint64_t byte_len_; // byte len
  uint64_t dst_offset_;
  uint64_t dst_len_; // for diff, char_len
  ObLobDiffFlags flags_;
};

struct ObLobDiffHeader
{
  ObLobDiffHeader()
    : diff_cnt_(0), persist_loc_size_(0)
  {}
  ObLobCommon* get_persist_lob()
  {
    return reinterpret_cast<ObLobCommon*>(data_);
  }
  char* get_inline_data_ptr()
  {
    return data_ + persist_loc_size_ + sizeof(ObLobDiff) * diff_cnt_;
  }
  ObLobDiff *get_diff_ptr()
  {
    return reinterpret_cast<ObLobDiff*>(data_ + persist_loc_size_);
  }

  bool is_mutli_diff() { return diff_cnt_ > 0; }
  TO_STRING_KV(K_(diff_cnt), K_(persist_loc_size));
  uint32_t diff_cnt_;
  uint32_t persist_loc_size_;
  char data_[0];
};

}  // namespace storage
}  // namespace oceanbase

#endif
