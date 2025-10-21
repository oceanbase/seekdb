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

#include "lib/compress/ob_compressor_pool.h"

namespace oceanbase
{
namespace common
{
ObCompressorPool::ObCompressorPool()
    :allocator_(ObMemAttr(OB_SERVER_TENANT_ID, "Compressor"), OB_MALLOC_BIG_BLOCK_SIZE),
     none_compressor(),
     lz4_compressor(),
     lz4_compressor_1_9_1(),
     snappy_compressor(),
     zlib_compressor(),
     zstd_compressor(allocator_),
     zstd_compressor_1_3_8(allocator_),
     zlib_lite_compressor(),
     lz4_stream_compressor(),
     zstd_stream_compressor(allocator_),
     zstd_stream_compressor_1_3_8(allocator_)
{
  allocator_.set_nway(32);
}
ObCompressorPool &ObCompressorPool::get_instance()
{
  static ObCompressorPool instance_;
  return instance_;
}

int ObCompressorPool::get_compressor(const char *compressor_name,
                                     ObCompressor *&compressor)
{
  int ret = OB_SUCCESS;
  ObCompressorType compressor_type = INVALID_COMPRESSOR;

  if (NULL == compressor_name) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid compressor name argument, ", K(ret), KP(compressor_name));
  } else if (OB_FAIL(get_compressor_type(compressor_name, compressor_type))) {
    LIB_LOG(WARN, "fail to get compressor type, ", K(ret), KCSTRING(compressor_name));
  } else if (OB_FAIL(get_compressor(compressor_type, compressor))) {
    LIB_LOG(WARN, "fail to get compressor", K(ret), K(compressor_type));
  }
  return ret;
}

int ObCompressorPool::get_compressor(const ObCompressorType &compressor_type,
                                     ObCompressor *&compressor)
{
  int ret = OB_SUCCESS;
  switch(compressor_type) {
    case NONE_COMPRESSOR:
      compressor = &none_compressor;
      break;
    case LZ4_191_COMPRESSOR:
      compressor = &lz4_compressor_1_9_1;
      break;
    case LZ4_COMPRESSOR:
      compressor = &lz4_compressor;
      break;
    case SNAPPY_COMPRESSOR:
      compressor = &snappy_compressor;
      break;
    case ZLIB_COMPRESSOR:
      compressor = &zlib_compressor;
      break;
    case ZSTD_COMPRESSOR:
      compressor = &zstd_compressor;
      break;
    case ZSTD_1_3_8_COMPRESSOR:
      compressor = &zstd_compressor_1_3_8;
      break;
    case ZLIB_LITE_COMPRESSOR:
      compressor = &zlib_lite_compressor;
      break;
    default:
      compressor = NULL;
      ret = OB_NOT_SUPPORTED;
      LIB_LOG(WARN, "not support compress type, ", K(ret), K(compressor_type));
  }
  return ret;
}

int ObCompressorPool::get_compressor_type(const char *compressor_name,
                                          ObCompressorType &compressor_type) const
{
  int ret = OB_SUCCESS;
  if (NULL == compressor_name) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid compressor name argument, ", K(ret), KP(compressor_name));
  } else if (!STRCASECMP(compressor_name, "none")) {
    compressor_type = NONE_COMPRESSOR;
  } else if (!STRCASECMP(compressor_name, "lz4_1.0")) {
    compressor_type = LZ4_COMPRESSOR;
  } else if (!STRCASECMP(compressor_name, "snappy_1.0")) {
    compressor_type = SNAPPY_COMPRESSOR;
  } else if (!STRCASECMP(compressor_name, "zlib_1.0")) {
    compressor_type = ZLIB_COMPRESSOR;
  } else if (!STRCASECMP(compressor_name, "zstd_1.0")) {
    compressor_type = ZSTD_COMPRESSOR;
  } else if (!STRCASECMP(compressor_name, "zstd_1.3.8")) {
    compressor_type = ZSTD_1_3_8_COMPRESSOR;
  } else if (!STRCASECMP(compressor_name, "lz4_1.9.1")) {
    compressor_type = LZ4_191_COMPRESSOR;
  } else if (!STRCASECMP(compressor_name, "stream_lz4_1.0")) {
    compressor_type = STREAM_LZ4_COMPRESSOR;
  } else if (!STRCASECMP(compressor_name, "stream_zstd_1.0")) {
    compressor_type = STREAM_ZSTD_COMPRESSOR;
  } else if (!STRCASECMP(compressor_name, "stream_zstd_1.3.8")) {
    compressor_type = STREAM_ZSTD_1_3_8_COMPRESSOR;
  } else if (!strcmp(compressor_name, "zlib_lite_1.0")) {
    compressor_type = ZLIB_LITE_COMPRESSOR;
  }
  else {
    ret = OB_NOT_SUPPORTED;
    LIB_LOG(WARN, "no support compressor type, ", K(ret), KCSTRING(compressor_name));
  }
  return ret;
}

int ObCompressorPool::get_compressor_type(const ObString &compressor_name,
                                          ObCompressorType &compressor_type) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(compressor_name.ptr()) || compressor_name.length() == 0) {
    compressor_type = ObCompressorType::NONE_COMPRESSOR;
  } else {
    int64_t len = compressor_name.length() + 1;
    char comp_name[len];
    MEMCPY(comp_name, compressor_name.ptr(), len - 1);
    comp_name[len - 1] = '\0';
    if (OB_FAIL(get_compressor_type(comp_name, compressor_type))) {
      LIB_LOG(ERROR, "no support compressor name", K(ret), K(compressor_name));
    }
  }
  return ret;
}

int ObCompressorPool::get_stream_compressor(const char *compressor_name,
                                            ObStreamCompressor *&stream_compressor)
{
  int ret = OB_SUCCESS;
  ObCompressorType compressor_type = INVALID_COMPRESSOR;

  if (OB_ISNULL(compressor_name)) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid compressor name argument, ", K(ret), KP(compressor_name));
  } else if (OB_FAIL(get_compressor_type(compressor_name, compressor_type))) {
    LIB_LOG(WARN, "fail to get compressor type, ", K(ret), KCSTRING(compressor_name));
  } else if (OB_FAIL(get_stream_compressor(compressor_type, stream_compressor))) {
    LIB_LOG(WARN, "fail to get stream compressor", K(ret), K(compressor_type));
  } else {/*do nothing*/}
  return ret;
}

int ObCompressorPool::get_stream_compressor(const ObCompressorType &compressor_type,
                                            ObStreamCompressor *&stream_compressor)
{
  int ret = OB_SUCCESS;
  switch(compressor_type) {
    case STREAM_LZ4_COMPRESSOR:
      stream_compressor = &lz4_stream_compressor;
      break;
    case STREAM_ZSTD_COMPRESSOR:
      stream_compressor = &zstd_stream_compressor;
      break;
    case STREAM_ZSTD_1_3_8_COMPRESSOR:
      stream_compressor = &zstd_stream_compressor_1_3_8;
      break;
    default:
      stream_compressor = NULL;
      ret = OB_NOT_SUPPORTED;
      LIB_LOG(WARN, "not support compress type for stream compress", K(ret), K(compressor_type));
  }
  return ret;
}

} /* namespace common */
} /* namespace oceanbase */
