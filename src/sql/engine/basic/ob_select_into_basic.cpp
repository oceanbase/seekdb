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
#include "rust/compression.h"
#include <algorithm>
#include <limits>

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
    ret = OB_NOT_INIT;
  } else if ((length > 0 && OB_ISNULL(src))
             || length > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (compress_stream_finished_) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    size_t offset = 0;
    bool finished = false;
    while (OB_SUCC(ret) && (offset < length || (is_file_end && !finished))) {
      if (curr_pos_ == buf_len_) {
        if (OB_FAIL(flush_to_storage(buf_, curr_pos_))) {
        } else {
          curr_pos_ = 0;
        }
      }
      if (OB_SUCC(ret)) {
        StreamCompressionProgress progress;
        const int64_t remaining = static_cast<int64_t>(length - offset);
        const char *input = remaining > 0 ? src + offset : nullptr;
        if (OB_FAIL(compressor_->compress(input, remaining, buf_ + curr_pos_,
                                         buf_len_ - curr_pos_, is_file_end, progress))) {
        } else if (progress.consumed < 0 || progress.consumed > remaining
                   || progress.produced < 0 || progress.produced > buf_len_ - curr_pos_
                   || (progress.finished && (!is_file_end || progress.consumed != remaining))
                   || (progress.consumed == 0 && progress.produced == 0 && !progress.finished)) {
          ret = OB_ERR_COMPRESS_DECOMPRESS_DATA;
        } else if (progress.produced > std::numeric_limits<int64_t>::max() - total_compressed_bytes_) {
          ret = OB_SIZE_OVERFLOW;
        } else {
          offset += progress.consumed;
          curr_pos_ += progress.produced;
          total_compressed_bytes_ += progress.produced;
          finished = progress.finished;
        }
      }
    }
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

int ObCompressStreamWriter::reuse()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(compressor_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(compressor_->reuse())) {
  } else {
    curr_pos_ = 0;
    total_compressed_bytes_ = 0;
    compress_stream_finished_ = false;
  }
  return ret;
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
  if (OB_ISNULL(file_appender_) || compress_type_ == CsvCompressType::NONE) {
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

  if (OB_SUCC(ret) && OB_ISNULL(compressor)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_SUCC(ret)) {
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

int ObOutfileZstdStreamCompressor::init()
{
  int ret = OB_SUCCESS;
  if (encoder_.has_value()) {
    ret = OB_INIT_TWICE;
  } else {
    encoder_.emplace(compression::new_zstd_encoder());
  }
  return ret;
}

void ObOutfileZstdStreamCompressor::reset()
{
  encoder_.reset();
}

int ObOutfileZstdStreamCompressor::reuse()
{
  return encoder_.has_value()
      ? compression_error_code(compression::zstd_encoder_reset(**encoder_)) : OB_NOT_INIT;
}

int ObOutfileZstdStreamCompressor::compress(const char *src,
                                         int64_t src_size,
                                         char *dest,
                                         int64_t dest_capacity,
                                         bool is_file_end,
                                         StreamCompressionProgress &progress)
{
  int ret = OB_SUCCESS;
  progress = {};
  if (!encoder_.has_value()) {
    ret = OB_NOT_INIT;
  } else if (src_size < 0 || (src_size > 0 && OB_ISNULL(src))
             || OB_ISNULL(dest) || dest_capacity <= 0
             || static_cast<uint64_t>(src_size) > std::numeric_limits<ptrdiff_t>::max()
             || static_cast<uint64_t>(dest_capacity) > std::numeric_limits<ptrdiff_t>::max()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const auto *input = reinterpret_cast<const uint8_t *>(src_size > 0 ? src : "");
    const auto result = compression::zstd_encode(**encoder_,
        rust::Slice<const uint8_t>(input, static_cast<size_t>(src_size)),
        reinterpret_cast<uint8_t *>(dest), static_cast<size_t>(dest_capacity),
        is_file_end);
    if (OB_SUCC(ret = compression_error_code(result.code))) {
      progress.consumed = static_cast<int64_t>(result.consumed);
      progress.produced = static_cast<int64_t>(result.produced);
      progress.finished = result.finished;
    }
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

int ObOutfileGzipStreamCompressor::reuse()
{
  int ret = OB_SUCCESS;
  if (encoder_.has_value()) {
    compression::gzip_reset(*encoder_.value());
  } else {
    ret = OB_NOT_INIT;
  }
  return ret;
}

int ObOutfileGzipStreamCompressor::compress(const char *src,
                                         int64_t src_size,
                                         char *dest,
                                         int64_t dest_capacity,
                                         bool is_file_end,
                                         StreamCompressionProgress &progress)
{
  int ret = OB_SUCCESS;
  progress = {};
  if (!encoder_.has_value()) {
    ret = OB_NOT_INIT;
  } else if (src_size < 0 || (src_size > 0 && OB_ISNULL(src))
             || OB_ISNULL(dest) || dest_capacity <= 0
             || static_cast<uint64_t>(src_size) > std::numeric_limits<ptrdiff_t>::max()
             || static_cast<uint64_t>(dest_capacity) > std::numeric_limits<ptrdiff_t>::max()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const auto *input = reinterpret_cast<const uint8_t *>(src_size > 0 ? src : "");
    const auto result = compression::gzip_process(
        *encoder_.value(),
        rust::Slice<const uint8_t>(input, static_cast<size_t>(src_size)),
        rust::Slice<uint8_t>(reinterpret_cast<uint8_t *>(dest), dest_capacity),
        is_file_end);
    if (compression::Code::Ok != result.code) {
      ret = compression::Code::OutOfMemory == result.code
          ? OB_ALLOCATE_MEMORY_FAILED : OB_ERR_COMPRESS_DECOMPRESS_DATA;
      LOG_WARN("failed to deflate", KP(src), K(src_size), K(dest_capacity));
    } else {
      progress.consumed = static_cast<int64_t>(result.consumed);
      progress.produced = static_cast<int64_t>(result.produced);
      progress.finished = result.stream_end;
    }
  }
  return ret;
}

}
}
