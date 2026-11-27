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

#ifndef OB_MACRO_BLOCK_READER_H_
#define OB_MACRO_BLOCK_READER_H_

#include "lib/hash/ob_array_index_hash_set.h"
#include "lib/compress/ob_compress_util.h"
#include "storage/access/ob_table_param.h"
#include "ob_imicro_block_reader.h"
#include "storage/blocksstable/ob_simplified_sstable_macro_block_header.h"
namespace oceanbase
{
namespace share
{
namespace schema
{
struct ObColDesc;
}
}
namespace blocksstable
{
struct ObMicroBlockData;
struct ObMicroBlockDesc;
class ObMacroBlockReader
{
public:
  ObMacroBlockReader();
  virtual ~ObMacroBlockReader();
  int decompress_data(
      const common::ObCompressorType compressor_type,
      const char *buf,
      const int64_t size,
      const char *&uncomp_buf,
      int64_t &uncomp_size,
      bool &is_compressed);
  // both payload_buf and uncomp_buf don't contain micro block header
  int decompress_data_with_prealloc_buf(
      const common::ObCompressorType compressor_type,
      const char *buf,
      const int64_t size,
      char *uncomp_buf,
      const int64_t uncomp_buf_size);
  int decompress_data(
      const ObSSTableMacroBlockHeader &block_header,
      const char *buf,
      const int64_t size,
      const bool need_deep_copy,
      const char *&uncomp_buf,
      int64_t &uncomp_size,
      bool &is_compressed);
  int decompress_data(
      const ObSimplifiedSSTableMacroBlockHeader &block_header,
      const char *buf,
      const int64_t size,
      const bool need_deep_copy,
      const char *&uncomp_buf,
      int64_t &uncomp_size,
      bool &is_compressed);
  int decompress_data(
      const ObMicroBlockDesMeta &deserialize_meta,
      const char *input,
      const int64_t size,
      const char *&uncomp_buf,
      int64_t &uncomp_size,
      bool &is_compressed,
      const bool need_deep_copy = false,
      ObIAllocator *ext_allocator = nullptr);
  int do_decompress_data(
      const ObMicroBlockHeader &header,
      const ObMicroBlockDesMeta &deserialize_meta,
      const char *src_buf,
      const int64_t src_buf_size,
      const char *&uncomp_buf,
      int64_t &uncomp_size,
      bool &is_compressed,
      const bool need_deep_copy,
      ObIAllocator *ext_allocator);

private:
  int alloc_buf(const int64_t req_size, char *&buf, int64_t &buf_size);
  int alloc_buf(ObIAllocator &allocator, const int64_t buf_size, char *&buf);

private:
  char *uncomp_buf_;
  int64_t uncomp_buf_size_;
  common::ObArenaAllocator allocator_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMacroBlockReader);
};
} /* namespace blocksstable */
} /* namespace oceanbase */

#endif /* OB_MACRO_BLOCK_READER_H_ */
