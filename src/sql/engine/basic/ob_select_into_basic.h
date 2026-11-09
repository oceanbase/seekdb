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

#ifndef SRC_SQL_ENGINE_BASIC_OB_SELECT_INTO_BASIC_H_
#define SRC_SQL_ENGINE_BASIC_OB_SELECT_INTO_BASIC_H_


#include "sql/engine/cmd/ob_load_data_parser.h"
#include "lib/file/ob_file.h"
#include "rust/compression.h"
#include <optional>

namespace oceanbase
{
namespace sql
{
using CsvCompressType = ObCSVGeneralFormat::ObCSVCompression;

struct StreamCompressionProgress
{
  int64_t consumed = 0;
  int64_t produced = 0;
  bool finished = false;
};

class ObOutfileStreamCompressor
{
public:
  ObOutfileStreamCompressor(ObIAllocator &allocator)
      : allocator_(allocator) {}
  virtual ~ObOutfileStreamCompressor() { }
  virtual int compress(const char *src, int64_t src_size,
                       char *dest, int64_t dest_capacity,
                       bool is_file_end, StreamCompressionProgress &progress) = 0;

  virtual int init() = 0;
  virtual void reset() {};
  virtual int reuse() = 0;
  static int create(CsvCompressType format, ObIAllocator &allocator, ObOutfileStreamCompressor *&compressor);
  static void destroy(ObOutfileStreamCompressor *compressor);
protected:
  ObIAllocator &allocator_;
};

class ObOutfileZstdStreamCompressor : public ObOutfileStreamCompressor
{
public:
  ObOutfileZstdStreamCompressor(ObIAllocator &allocator)
      : ObOutfileStreamCompressor(allocator) {}
  ~ObOutfileZstdStreamCompressor() { reset(); }
  int compress(const char *src, int64_t src_size,
               char *dest, int64_t dest_capacity,
               bool is_file_end, StreamCompressionProgress &progress) override;
  int init() override;
  void reset() override;
  int reuse() override;
private:
  std::optional<rust::Box<compression::ZstdEncoder>> encoder_;
};

class ObOutfileGzipStreamCompressor : public ObOutfileStreamCompressor
{
public:
  ObOutfileGzipStreamCompressor(ObIAllocator &allocator)
      : ObOutfileStreamCompressor(allocator) {}
  ~ObOutfileGzipStreamCompressor() { reset(); }
  int compress(const char *src, int64_t src_size,
               char *dest, int64_t dest_capacity,
               bool is_file_end, StreamCompressionProgress &progress) override;
  int init() override;
  void reset() override;
  int reuse() override;
private:
  std::optional<rust::Box<compression::GzipEncoder>> encoder_;
};

class ObCompressStreamWriter
{
public:
  ObCompressStreamWriter()
  : file_appender_(NULL),
    compress_type_(CsvCompressType::NONE),
    buf_(NULL),
    buf_len_(0),
    curr_pos_(0),
    total_compressed_bytes_(0),
    compressor_(NULL),
    allocator_(NULL),
    compress_stream_finished_(true)
  {}

  int init(ObFileAppender *file_appender,
           CsvCompressType compress_type,
           ObIAllocator &allocator,
           int64_t buffer_size);

  int reuse();

  void reset();

  ~ObCompressStreamWriter() { reset(); }

  int write(const char *src, size_t length, bool is_file_end = false);

  int finish_file_compress();

  int64_t get_write_bytes() {return total_compressed_bytes_; }
private:

  int flush_to_storage(const char *src, size_t length);

  int finish_compress_stream();

private:
  ObFileAppender *file_appender_;
  CsvCompressType compress_type_;
  char *buf_;
  int64_t buf_len_;
  // compressed position in buf_
  int64_t curr_pos_;
  int64_t total_compressed_bytes_;

  ObOutfileStreamCompressor *compressor_;
  ObIAllocator *allocator_;
  bool compress_stream_finished_;
  static const int64_t DEFAULT_COMPRESSED_BUFFER_SIZE;
  static const int64_t MIN_COMPRESSED_BUFFER_SIZE;
};

}
}
#endif /* SRC_SQL_ENGINE_BASIC_OB_SELECT_INTO_BASIC_H_ */
