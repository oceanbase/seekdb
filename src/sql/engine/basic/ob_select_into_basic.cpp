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

#include "ob_select_into_basic.h"
#include "lib/compress/ob_compress_util.h"
#include "lib/compress/zstd_1_3_8/ob_zstd_wrapper.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

const int64_t ObCompressStreamWriter::DEFAULT_COMPRESSED_BUFFER_SIZE = 1 * 1024 * 1024; // 1MB
const int64_t ObCompressStreamWriter::MIN_COMPRESSED_BUFFER_SIZE = 4 * 1024; // 4KB

int ObCompressStreamWriter::write(const char *src, size_t length, bool is_file_end)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(compressor_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    bool compress_ended = false;
    size_t consumed_size = 0;
    do {
      int64_t compressed_size = 0;
      if (OB_FAIL(compressor_->compress(src, length, consumed_size, buf_ + curr_pos_, buf_len_ - curr_pos_,
                                        compressed_size, is_file_end, compress_ended))) {
      } else {
        curr_pos_ += compressed_size;
        total_compressed_bytes_ += compressed_size;
        if (curr_pos_ >= buf_len_) {
          if (OB_FAIL(flush_to_storage(buf_, curr_pos_))) {
          } else {
            curr_pos_ = 0;
          }
        }
      }
    } while (OB_SUCC(ret) && !compress_ended);
  }
  return ret;
}

int ObCompressStreamWriter::flush_to_storage(const char *data, size_t length)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(file_appender_->append(data, length, false))) {
  }
  return ret;
}

int ObCompressStreamWriter::finish_file_compress()
{
  int ret = OB_SUCCESS;
  if (compress_stream_finished_) {
  } else if (OB_FAIL(finish_compress_stream())) {
  } else {
    if (curr_pos_ > 0) {
      if (OB_FAIL(flush_to_storage(buf_, curr_pos_))) {
      }
    }
    if (OB_SUCC(ret)) {
      compress_stream_finished_ = true;
    }
  }
  return ret;
}

int ObCompressStreamWriter::finish_compress_stream()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(compressor_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(write(NULL, 0, true))) {
  }
  return ret;
}

void ObCompressStreamWriter::reuse()
{
  if (OB_NOT_NULL(compressor_)) {
    compressor_->reuse();
  }
  curr_pos_ = 0;
  total_compressed_bytes_ = 0;
  compress_stream_finished_ = false;
}
int ObCompressStreamWriter::init(ObFileAppender *file_appender,
                                 CsvCompressType compress_type,
                                 ObIAllocator &allocator,
                                 int64_t buffer_size)
{
  int ret = OB_SUCCESS;
  file_appender_ = file_appender;
  compress_type_ = compress_type;
  allocator_ = &allocator;
  compress_stream_finished_ = false;
  // buffer_size used for stream compression is limited to between MIN_COMPRESSED_BUFFER_SIZE
  // and DEFAULT_COMPRESSED_BUFFER_SIZE, which ensures streaming compression works properly
  // and avoiding the memory usage of too many partitions when export outfile by partition.
  if (buffer_size > DEFAULT_COMPRESSED_BUFFER_SIZE) {
    buf_len_ = DEFAULT_COMPRESSED_BUFFER_SIZE;
  } else if (buffer_size < MIN_COMPRESSED_BUFFER_SIZE) {
    buf_len_ = MIN_COMPRESSED_BUFFER_SIZE;
  } else {
    buf_len_ = buffer_size;
  }
  if (compress_type_ == CsvCompressType::NONE) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (OB_FAIL(ObOutfileStreamCompressor::create(compress_type_, *allocator_, compressor_))) {
    } else if (OB_ISNULL(buf_ = (char *)allocator_->alloc(buf_len_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate buffer.", K(buf_len_));
    }
  }
  return ret;
}

void ObCompressStreamWriter::reset()
{
  if (compressor_ != NULL) {
    compressor_->reset();
    ObOutfileStreamCompressor::destroy(compressor_);
    compressor_ = NULL;
  }
  if (buf_ != NULL) {
    allocator_->free(buf_);
    buf_ = NULL;
  }
  compress_stream_finished_ = false;
}

int ObOutfileStreamCompressor::create(CsvCompressType format, ObIAllocator &allocator, ObOutfileStreamCompressor *&compressor)
{
  int ret = OB_SUCCESS;
  compressor = NULL;

  switch (format) {
    case CsvCompressType::NONE: {
      ret = OB_INVALID_ARGUMENT;
    } break;

    case CsvCompressType::GZIP:
    case CsvCompressType::DEFLATE: {
      compressor = OB_NEW(ObOutfileGzipStreamCompressor, ObMemAttr("ExportWriter"), allocator);
    } break;

    case CsvCompressType::ZSTD: {
      compressor = OB_NEW(ObOutfileZstdStreamCompressor, ObMemAttr("ExportWriter"), allocator);
    } break;

    default: {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unsupported compression format", K(format));
    } break;
  }

  if (OB_SUCC(ret) && OB_NOT_NULL(compressor)) {
    if (OB_FAIL(compressor->init())) {
      ObOutfileStreamCompressor::destroy(compressor);
      compressor = NULL;
    }
  }

  return ret;
}

void ObOutfileStreamCompressor::destroy(ObOutfileStreamCompressor *compressor)
{
  if (OB_NOT_NULL(compressor)) {
    compressor->reset();
    OB_DELETE(ObOutfileStreamCompressor, ObMemAttr("ExportWriter"), compressor);
  }
}

using OB_ZSTD_customMem = oceanbase::common::zstd_1_3_8::OB_ZSTD_customMem;
using ObZstdWrapper = oceanbase::common::zstd_1_3_8::ObZstdWrapper;
int ObOutfileZstdStreamCompressor::init()
{
  int ret = OB_SUCCESS;

  if (OB_NOT_NULL(zstd_cctx_)) {
    ret = OB_INIT_TWICE;
  } else {
    OB_ZSTD_customMem allocator;
    allocator.customAlloc = ob_zstd_malloc;
    allocator.customFree  = ob_zstd_free;
    allocator.opaque      = &allocator_;

    ret = ObZstdWrapper::create_cctx(allocator, zstd_cctx_);
    if (OB_FAIL(ret)) {
    }
  }

  return ret;
}

void ObOutfileZstdStreamCompressor::reset()
{
  if (OB_NOT_NULL(zstd_cctx_)) {
    ObZstdWrapper::free_cctx(zstd_cctx_);
    zstd_cctx_ = NULL;
  }
}

void ObOutfileZstdStreamCompressor::reuse()
{
  ObZstdWrapper::reset_cctx(zstd_cctx_, 1/*ZSTD_reset_session_only*/);
}

int ObOutfileZstdStreamCompressor::compress(const char *src,
                                            int64_t src_size,
                                            size_t &consumed_size,
                                            char *dest,
                                            int64_t dest_capacity,
                                            int64_t &compressed_size,
                                            bool is_file_end,
                                            bool &compress_ended)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(zstd_cctx_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(dest)
             || dest_capacity <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    size_t tmp_compressed_size = 0;
    ret = ObZstdWrapper::compress_stream(zstd_cctx_,
                                         src, src_size, consumed_size,
                                         dest, dest_capacity, tmp_compressed_size, is_file_end, compress_ended);
    compressed_size = static_cast<int64_t>(tmp_compressed_size);
  }
  return ret;
}

int ObOutfileGzipStreamCompressor::init()
{
  int ret = OB_SUCCESS;
  if (encoder_.has_value()) {
    ret = OB_INIT_TWICE;
  } else {
    encoder_.emplace(compression::new_gzip_encoder());
  }
  return ret;
}

void ObOutfileGzipStreamCompressor::reset()
{
  encoder_.reset();
}

void ObOutfileGzipStreamCompressor::reuse()
{
  if (encoder_.has_value()) {
    compression::gzip_reset(*encoder_.value());
  }
}

int ObOutfileGzipStreamCompressor::compress(const char *src,
                                            int64_t src_size,
                                            size_t &consumed_size,
                                            char *dest,
                                            int64_t dest_capacity,
                                            int64_t &compressed_size,
                                            bool is_file_end,
                                            bool &compress_ended)
{
  int ret = OB_SUCCESS;
  if (NULL == dest
      || 0 >= dest_capacity
      || !encoder_.has_value()
      || src_size < 0
      || consumed_size > static_cast<size_t>(src_size)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    static const uint8_t EMPTY_INPUT = 0;
    const uint8_t *input = src_size == consumed_size
        ? &EMPTY_INPUT
        : reinterpret_cast<const uint8_t *>(src) + consumed_size;
    const auto progress = compression::gzip_process(
        *encoder_.value(),
        rust::Slice<const uint8_t>(input, src_size - consumed_size),
        rust::Slice<uint8_t>(reinterpret_cast<uint8_t *>(dest), dest_capacity),
        is_file_end);
    if (compression::Code::Ok != progress.code) {
      ret = OB_ERR_COMPRESS_DECOMPRESS_DATA;
      LOG_WARN("failed to deflate", KP(src), K(src_size), K(dest_capacity));
    } else {
      compressed_size = progress.produced;
      consumed_size += progress.consumed;
      compress_ended = is_file_end ? progress.stream_end : progress.drained;
    }
  }
  return ret;
}

}
}
