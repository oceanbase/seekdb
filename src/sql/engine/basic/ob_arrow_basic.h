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

#ifndef OB_PARQUET_BASIC_H
#define OB_PARQUET_BASIC_H

#ifndef OB_BUILD_EMBED_MODE
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <parquet/exception.h>

#include "share/ob_device_manager.h"
#include "sql/engine/basic/ob_select_into_basic.h"

namespace oceanbase
{
namespace sql
{

class ObArrowMemPool : public ::arrow::MemoryPool
{
public:
  ObArrowMemPool() : total_alloc_size_(0), total_hold_size_(0), num_allocations_(0) {}
  void init();
  virtual arrow::Status Allocate(int64_t size, int64_t alignment, uint8_t** out) override;

  virtual arrow::Status Reallocate(int64_t old_size, int64_t new_size, int64_t alignment,
                                   uint8_t **ptr) override;

  virtual void Free(uint8_t* buffer, int64_t size, int64_t alignment) override;

  virtual void ReleaseUnused() override;
  /// The number of bytes that were allocated and not yet free'd through
  /// this allocator.
  virtual int64_t bytes_allocated() const override;
  virtual int64_t max_memory() const override { return -1; }

  virtual int64_t total_bytes_allocated() const override { return total_alloc_size_; }

  virtual int64_t num_allocations() const override { return 0; }

  virtual std::string backend_name() const override { return "Arrow"; }
private:
  common::ObArenaAllocator alloc_;
  common::ObMemAttr mem_attr_;
  int64_t total_alloc_size_;
  int64_t total_hold_size_;
  int64_t num_allocations_;
};

class ObParquetOutputStream : public arrow::io::OutputStream
{
public:
  ObParquetOutputStream (ObFileAppender *file_appender,
                         ObStorageAppender *storage_appender,
                         IntoFileLocation file_location,
                         const ObString &url)
    : file_appender_(file_appender),
      storage_appender_(storage_appender),
      file_location_(file_location),
      url_(url),
      position_(0)
    {}

  ~ObParquetOutputStream() override {}

  // Write methods
  // Virtual methods in `arrow::io::Writable`
  virtual arrow::Status Write(const void* data, int64_t nbytes) override;
  // virtual arrow::Status Write(const std::shared_ptr<arrow::Buffer>& data) override;
  // virtual arrow::Status Flush() override;

  // Virtual methods in `arrow::io::FileInterface`
  virtual arrow::Status Close() override;
  virtual bool closed() const override;
  virtual arrow::Result<int64_t> Tell() const override;

private:
  ObFileAppender *file_appender_;
  ObStorageAppender *storage_appender_;
  IntoFileLocation file_location_;
  const ObString &url_;
  int64_t position_;
};

} // end of sql namespace
} // end of oceanbase namespace

#endif // !OB_BUILD_EMBED_MODE
#endif // OB_PARQUET_BASIC_H
