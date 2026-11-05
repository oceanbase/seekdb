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

class ObDirectLoadSSTableIndexBlock
{
public:
  static const int64_t DEFAULT_INDEX_BLOCK_SIZE = 4 * 1024; // 4K
  struct Header : public ObDirectLoadDataBlock::Header
  {
    OB_UNIS_VERSION(1);
  public:
    Header();
    ~Header();
    void reset();
    TO_STRING_KV(K_(offset), K_(count), K_(last_entry_pos));
  public:
    int64_t offset_; // start offset of index block data
    int32_t count_;
    int32_t last_entry_pos_;
  };
  struct Entry
  {
    OB_UNIS_VERSION(1);
  public:
    Entry();
    ~Entry();
    void reuse();
    void reset();
    TO_STRING_KV(K_(offset));
  public:
    int64_t offset_;
  };
public:
  static int64_t get_header_size();
  static int64_t get_entry_size();
  static int64_t get_entries_per_block(int64_t block_size);
};

struct ObDirectLoadSSTableIndexEntry
{
public:
  ObDirectLoadSSTableIndexEntry();
  ~ObDirectLoadSSTableIndexEntry();
  TO_STRING_KV(K_(offset), K_(size));
public:
  int64_t offset_;
  int64_t size_;
};

} // namespace storage
} // namespace oceanbase
