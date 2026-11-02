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
#pragma once

#include "storage/direct_load/ob_direct_load_data_block.h"

namespace oceanbase
{
namespace storage
{

class ObDirectLoadSSTableDataBlock
{
public:
  static const int64_t DEFAULT_DATA_BLOCK_SIZE = 64 * 1024; // 64K
  struct Header : public ObDirectLoadDataBlock::Header
  {
    OB_UNIS_VERSION(1);
  public:
    Header();
    ~Header();
    void reset();
    TO_STRING_KV(K_(last_row_pos));
  public:
    int32_t last_row_pos_;
    int32_t reserved_;
  };
public:
};

struct ObDirectLoadSSTableDataBlockDesc
{
public:
  ObDirectLoadSSTableDataBlockDesc();
  ~ObDirectLoadSSTableDataBlockDesc();
  TO_STRING_KV(K_(fragment_idx), K_(offset), K_(size), K_(block_count), K_(is_left_border),
               K_(is_right_border));
public:
  int64_t fragment_idx_;
  int64_t offset_;
  int64_t size_;
  int64_t block_count_;
  bool is_left_border_;
  bool is_right_border_;
};

} // namespace storage
} // namespace oceanbase
