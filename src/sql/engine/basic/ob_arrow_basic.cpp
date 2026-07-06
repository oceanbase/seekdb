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
#ifndef OB_BUILD_EMBED_MODE
#define USING_LOG_PREFIX SQL_ENG

#include "ob_arrow_basic.h"
#include <parquet/api/reader.h>


namespace oceanbase
{
using namespace share::schema;
using namespace common;
using namespace share;
namespace sql {

/* ObArrowMemPool */
void ObArrowMemPool::init()
{
  mem_attr_ = ObMemAttr("ArrowMemPool");
}

arrow::Status ObArrowMemPool::Allocate(int64_t size, int64_t alignment, uint8_t** out)
{
  int ret = OB_SUCCESS;
  arrow::Status status_ret = arrow::Status::OK();
  if (0 == size) {
    *out = NULL;
  } else {
    void *buf = ob_malloc_align(alignment, size, mem_attr_);
    if (OB_ISNULL(buf)) {
      status_ret = arrow::Status::Invalid("allocate memory failed");
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate memory", K(size), K(lbt()));
    } else {
      *out = static_cast<uint8_t*>(buf);
      ++num_allocations_;
      total_alloc_size_ += size;
      total_hold_size_ += size;
    }
  }
  LOG_DEBUG("ObArrowMemPool::Allocate", K(size), "stack", lbt());
  return status_ret;
}

arrow::Status ObArrowMemPool::Reallocate(int64_t old_size, int64_t new_size, int64_t alignment,
  uint8_t **ptr)
{
  int ret = OB_SUCCESS;
  uint8_t* old = *ptr;
  arrow::Status status_ret = Allocate(new_size, alignment, ptr);
  if (arrow::Status::OK() == status_ret) {
    MEMCPY(*ptr, old, std::min(old_size, new_size));
    Free(old, old_size, alignment);
  }
  LOG_DEBUG("ObArrowMemPool::Reallocate", K(old_size), K(new_size), "stack", lbt());
  return status_ret;
}

void ObArrowMemPool::Free(uint8_t* buffer, int64_t size, int64_t alignment) {
  UNUSED(alignment);
  int ret = OB_SUCCESS;
  UNUSED(alignment);
  ob_free_align(buffer);
  total_hold_size_ -= size;
  LOG_DEBUG("ObArrowMemPool::Free", K(size), "stack", lbt());
}

void ObArrowMemPool::ReleaseUnused() {
  LOG_DEBUG("ObArrowMemPool::ReleaseUnused", "stack", lbt());
}

int64_t ObArrowMemPool::bytes_allocated() const {
  LOG_DEBUG("ObArrowMemPool::bytes_allocated", "stack", lbt());
  return total_hold_size_;
}

/*ObParquetOutputStream*/

arrow::Status ObParquetOutputStream::Write(const void* data, int64_t nbytes)
{
  arrow::Status status = arrow::Status::OK();
  int ret = OB_SUCCESS;
  int64_t write_size = 0;
  if (IntoFileLocation::SERVER_DISK == file_location_) {
    if (OB_FAIL(file_appender_->append(data, nbytes, false))) {
      LOG_WARN("failed to append file", K(ret), K(nbytes), K(url_));
      status = arrow::Status(arrow::StatusCode::IOError, "write file failed");
    }
  } else if (OB_FAIL(storage_appender_->append(static_cast<const char*>(data), nbytes, write_size))) {
    LOG_WARN("fail to append data", K(ret), KP(data), K(nbytes), K(url_));
    status = arrow::Status(arrow::StatusCode::IOError, "write file failed");
  }
  if (OB_SUCC(ret)) {
    position_ += nbytes;
  }
  return status;
}

// Virtual methods in `arrow::io::FileInterface`
arrow::Status ObParquetOutputStream::Close()
{
  arrow::Status status = arrow::Status::OK();
  int ret = OB_SUCCESS;
  if (IntoFileLocation::SERVER_DISK == file_location_) {
    file_appender_->close();
  } else if (OB_FAIL(storage_appender_->close())) {
    LOG_WARN("failed to close storage appender", K(ret));
    status = arrow::Status(arrow::StatusCode::IOError, "close file failed");
  }
  return status;
}

bool ObParquetOutputStream::closed() const
{
  bool ret = false;
  if (IntoFileLocation::SERVER_DISK == file_location_) {
    ret = !file_appender_->is_opened();
  } else {
    ret = !storage_appender_->is_opened_;
  }
  return ret;
}

arrow::Result<int64_t> ObParquetOutputStream::Tell() const
{
  return position_;
}

} // end of oceanbase namespace
} // end of oceanbase namespace
#endif // !OB_BUILD_EMBED_MODE
