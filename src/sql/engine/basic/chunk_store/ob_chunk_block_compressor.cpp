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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/chunk_store/ob_chunk_block_compressor.h"

namespace oceanbase
{
using namespace common;

namespace sql
{

ObChunkBlockCompressor::ObChunkBlockCompressor()
  : compressor_type_(NONE_COMPRESSOR)
{
}

ObChunkBlockCompressor::~ObChunkBlockCompressor()
{
  reset();
}

void ObChunkBlockCompressor::reset()
{
  compressor_type_ = NONE_COMPRESSOR;
}

int ObChunkBlockCompressor::init(const ObCompressorType comptype)
{
  int ret = OB_SUCCESS;
  reset();

  if (comptype == NONE_COMPRESSOR) {
  } else if (comptype <= INVALID_COMPRESSOR || comptype >= MAX_COMPRESSOR) {
    ret = OB_NOT_SUPPORTED;
  } else {
    compressor_type_ = comptype;
  }
  return ret;
}

int ObChunkBlockCompressor::calc_need_size(int64_t in_size, int64_t &need_size)
{
  int ret = OB_SUCCESS;
  need_size = 0;
  if (compressor_type_ == NONE_COMPRESSOR) {
    need_size = in_size;
  } else {
    const auto bound = compression::compress_bound(
        static_cast<uint8_t>(compressor_type_), in_size);
    if (OB_FAIL(compression_error_code(bound.code))) {
    } else {
      need_size = static_cast<int64_t>(bound.size);
    }
  }

  return ret;
}

int ObChunkBlockCompressor::compress(const char *in, const int64_t in_size, const int64_t max_comp_size,
                                     char *out, int64_t &out_size)
{
  int ret = OB_SUCCESS;
  if (compressor_type_ == NONE_COMPRESSOR) {
    MEMCPY(out, in, in_size);
    out_size = in_size;
  } else {
    const auto result = compression::compress(
        static_cast<uint8_t>(compressor_type_),
        reinterpret_cast<const uint8_t *>(in), in_size,
        reinterpret_cast<uint8_t *>(out), max_comp_size);
    const int64_t comp_size = static_cast<int64_t>(result.size);
    if (OB_FAIL(compression_error_code(result.code))) {
    } else if (comp_size >= in_size) {
      MEMCPY(out, in, in_size);
      out_size = in_size;
    } else {
      out_size = comp_size;
    }
  }
  return ret;
}

int ObChunkBlockCompressor::decompress(const char *in, const int64_t in_size,
                                       const int64_t uncomp_size,
                                       char *out, int64_t &out_size)
{
  int ret = OB_SUCCESS;
  int64_t decomp_size = 0;
  if ((compressor_type_ == NONE_COMPRESSOR) || in_size == uncomp_size) {
    MEMCPY(out, in, in_size);
    out_size = in_size;
  } else {
    const auto result = compression::decompress(
        static_cast<uint8_t>(compressor_type_),
        reinterpret_cast<const uint8_t *>(in), in_size,
        reinterpret_cast<uint8_t *>(out), uncomp_size);
    if (OB_FAIL(compression_error_code(result.code))) {
    } else {
      decomp_size = static_cast<int64_t>(result.size);
    }
  }
  if (OB_SUCC(ret) && !((compressor_type_ == NONE_COMPRESSOR) || in_size == uncomp_size)) {
    out_size = decomp_size;
  }
  return ret;
}

}
}
