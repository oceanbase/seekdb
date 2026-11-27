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

#define USING_LOG_PREFIX STORAGE
#include "ob_macro_block_reader.h"
#include "data_plane/scheduler/ob_dag_scheduler.h"

namespace oceanbase
{
using namespace common;
using namespace storage;
using namespace share;
namespace blocksstable
{

ObMacroBlockReader::ObMacroBlockReader()
    :uncomp_buf_(NULL),
     uncomp_buf_size_(0),
     allocator_(ObModIds::OB_CS_SSTABLE_READER, OB_MALLOC_NORMAL_BLOCK_SIZE)
{
  if (share::is_reserve_mode()) {
    allocator_.set_ctx_id(ObCtxIds::MERGE_RESERVE_CTX_ID);
  }
}

ObMacroBlockReader::~ObMacroBlockReader()
{
}

int ObMacroBlockReader::decompress_data(
    const common::ObCompressorType compressor_type,
    const char *buf,
    const int64_t size,
    const char *&uncomp_buf,
    int64_t &uncomp_size,
    bool &is_compressed)
{
  const ObMicroBlockDesMeta meta(compressor_type, common::MAX_ROW_STORE);
  return decompress_data(meta, buf, size, uncomp_buf, uncomp_size, is_compressed);
}

int ObMacroBlockReader::decompress_data(
    const ObSSTableMacroBlockHeader &block_header,
    const char *buf,
    const int64_t size,
    const bool need_deep_copy,
    const char *&uncomp_buf,
    int64_t &uncomp_size,
    bool &is_compressed)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(!block_header.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid arguments to decompress data",
        K(ret), KP(buf), K(size), K(block_header));
  } else {
    ObMicroBlockDesMeta deserialize_meta(
        block_header.get_compressor_type(),
        static_cast<common::ObRowStoreType>(block_header.fixed_header_.row_store_type_));
    if (OB_FAIL(decompress_data(deserialize_meta, buf, size, uncomp_buf, uncomp_size,
        is_compressed, need_deep_copy, nullptr/*ext_allocator*/))) {
    }
  }
  return ret;
}

int ObMacroBlockReader::decompress_data(
    const ObSimplifiedSSTableMacroBlockHeader &block_header,
    const char *buf,
    const int64_t size,
    const bool need_deep_copy,
    const char *&uncomp_buf,
    int64_t &uncomp_size,
    bool &is_compressed)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(!block_header.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid arguments to decompress data",
        K(ret), KP(buf), K(size), K(block_header));
  } else {
    ObMicroBlockDesMeta deserialize_meta(
        block_header.compressor_type_,
        static_cast<common::ObRowStoreType>(block_header.row_store_type_));
    if (OB_FAIL(decompress_data(deserialize_meta, buf, size, uncomp_buf, uncomp_size,
        is_compressed, need_deep_copy, nullptr/*ext_allocator*/))) {
    }
  }
  return ret;
}

int ObMacroBlockReader::decompress_data_with_prealloc_buf(
    const common::ObCompressorType compressor_type,
    const char *buf,
    const int64_t size,
    char *uncomp_buf,
    const int64_t uncomp_buf_size)
{
  int ret = OB_SUCCESS;
  int64_t uncomp_size = 0;
  if (OB_ISNULL(buf) || OB_UNLIKELY(size <= 0) || OB_ISNULL(uncomp_buf)
      || OB_UNLIKELY(uncomp_buf_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (size == uncomp_buf_size) {
    MEMCPY(uncomp_buf, buf, size);
  } else {
    const auto result = compression::decompress(
        static_cast<uint8_t>(compressor_type),
        reinterpret_cast<const uint8_t *>(buf), size,
        reinterpret_cast<uint8_t *>(uncomp_buf), uncomp_buf_size);
    if (OB_FAIL(compression_error_code(result.code))) {
    } else {
      uncomp_size = static_cast<int64_t>(result.size);
      if (OB_UNLIKELY(uncomp_size != uncomp_buf_size)) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}


int ObMacroBlockReader::alloc_buf(const int64_t req_size, char *&buf, int64_t &buf_size)
{
  int ret = OB_SUCCESS;
  if (NULL == buf || buf_size < req_size) {
    if (nullptr != buf) {
      allocator_.reuse();
      buf = nullptr;
    }
    if (NULL == (buf = static_cast<char*>(allocator_.alloc(req_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "Fail to allocate memory for buf, ", K(req_size), K(ret));
    } else {
      buf_size = req_size;
    }
  }
  return ret;
}

int ObMacroBlockReader::alloc_buf(ObIAllocator &allocator, const int64_t buf_size, char *&buf)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(buf_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  }
  return ret;
}

int ObMacroBlockReader::decompress_data(
    const ObMicroBlockDesMeta &deserialize_meta,
    const char *input,
    const int64_t size,
    const char *&uncomp_buf,
    int64_t &uncomp_size,
    bool &is_compressed,
    const bool need_deep_copy,
    ObIAllocator *ext_allocator)
{
  int ret = OB_SUCCESS;
  ObMicroBlockHeader header;
  if (OB_FAIL(header.deserialize_and_check_record(input, size))) {
  } else if (OB_FAIL(do_decompress_data(header, deserialize_meta, input, size,
      uncomp_buf, uncomp_size, is_compressed, need_deep_copy, ext_allocator))) {
  }

  return ret;
}

int ObMacroBlockReader::do_decompress_data(
    const ObMicroBlockHeader &header,
    const ObMicroBlockDesMeta &deserialize_meta,
    const char *src_buf,
    const int64_t src_buf_size,
    const char *&uncomp_buf,
    int64_t &uncomp_size,
    bool &is_compressed,
    const bool need_deep_copy,
    ObIAllocator *ext_allocator)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(src_buf) || OB_UNLIKELY(!header.is_valid()
      || src_buf_size != header.get_stored_size())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    is_compressed = header.is_compressed_data();
    if (!is_compressed && !need_deep_copy) {
      uncomp_buf = src_buf;
      uncomp_size = src_buf_size;
    } else {
      const int64_t output_size = static_cast<int64_t>(header.header_size_) + header.data_length_;
      char *out = nullptr;
      if (nullptr != ext_allocator) {
        ret = alloc_buf(*ext_allocator, output_size, out);
      } else if (OB_SUCC(alloc_buf(output_size, uncomp_buf_, uncomp_buf_size_))) {
        out = uncomp_buf_;
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(decompress_data_with_prealloc_buf(deserialize_meta.compressor_type_,
          src_buf + header.header_size_, header.data_zlength_,
          out + header.header_size_, header.data_length_))) {
      } else {
        MEMCPY(out, src_buf, header.header_size_);
        uncomp_buf = out;
        uncomp_size = output_size;
      }
      if (OB_FAIL(ret) && nullptr != ext_allocator && nullptr != out) {
        ext_allocator->free(out);
      }
    }
  }
  return ret;
}


} /* namespace blocksstable */
} /* namespace oceanbase */
