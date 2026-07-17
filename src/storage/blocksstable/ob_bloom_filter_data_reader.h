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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_OB_BLOOM_FILTER_DATA_READER_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_OB_BLOOM_FILTER_DATA_READER_H_

#include "lib/ob_define.h"
#include "lib/container/ob_array.h"
#include "ob_block_manager.h"
#include "ob_macro_block_reader.h"
#include "ob_macro_block.h"

namespace oceanbase
{
namespace blocksstable
{

class ObBloomFilterCacheValue;

class ObBloomFilterMacroBlockReader
{
public:
  ObBloomFilterMacroBlockReader(const bool is_sys_read = false);
  virtual ~ObBloomFilterMacroBlockReader();
  void reset();
  int read_macro_block(
      const MacroBlockId &macro_id,
      const char *&bf_buf,
      int64_t &bf_size);
private:
  int decompress_micro_block(const char *&block_buf, int64_t &block_size);
  int read_micro_block(const char *buf, const int64_t buf_size, const char *&bf_buf, int64_t &bf_size);
  int read_macro_block(const MacroBlockId &macro_id);
private:
  ObMacroBlockReader macro_reader_;
  ObStorageObjectHandle macro_handle_;
  ObMacroBlockCommonHeader common_header_;
  const ObBloomFilterMacroBlockHeader *bf_macro_header_;
  bool is_sys_read_;
  common::ObArenaAllocator io_allocator_;
  char *io_buf_;
};

class ObBloomFilterDataReader
{
public:
  ObBloomFilterDataReader(const bool is_sys_read = false);
  virtual ~ObBloomFilterDataReader();
  void reset();
  void reuse();
private:
  ObBloomFilterMacroBlockReader bf_macro_reader_;
};

}  // end namespace blocksstable
}  // end namespace oceanbase

#endif  // OCEANBASE_STORAGE_BLOCKSSTABLE_OB_BLOOM_FILTER_DATA_READER_H_
