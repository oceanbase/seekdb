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
#include "ob_file_prefetch_buffer.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace share::schema;
using namespace common;
using namespace share;
namespace sql {

void ObFilePrefetchBuffer::destroy()
{
  if (nullptr != buffer_) {
    alloc_.free(buffer_);
    buffer_ = nullptr;
  }
  offset_ = 0;
  length_ = 0;
  buffer_size_ = 0;
}

void ObFilePrefetchBuffer::clear()
{
  offset_ = 0;
  length_ = 0;
}

int ObFilePrefetchBuffer::prefetch(const int64_t file_offset, const int64_t size)
{
  int ret = OB_SUCCESS;
  int64_t max_prebuffer_size = GCONF._parquet_row_group_prebuffer_size;
  offset_ = 0;
  length_ = 0;
  if (size > max_prebuffer_size) {
    // do nothing
    LOG_TRACE("exceeding the maximum prefetch size", K(size), K(max_prebuffer_size));
  } else {
    void *buffer = nullptr;
    int64_t read_size = 0;
    if (size > buffer_size_) {
      alloc_.free(buffer_);
      buffer_ = nullptr;
      buffer_size_ = 0;
    }
    if (nullptr == buffer_) {
      if (OB_ISNULL(buffer = alloc_.alloc(size))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc prefetch buffer", K(ret), K(size));
      } else {
        buffer_ = buffer;
        buffer_size_ = size;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(file_reader_.pread(buffer_, size, file_offset, read_size))) {
      LOG_WARN("fail to read file", K(ret), K(size));
    } else if (size != read_size) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected read size", K(size), K(read_size));
    } else {
      offset_ = file_offset;
      length_ = size;
    }
    LOG_INFO("success prefetch", K(ret), K(file_offset), K(size));
  }
  return ret;
}

bool ObFilePrefetchBuffer::in_prebuffer_range(const int64_t position, const int64_t nbytes)
{
  bool in_range = true;
  if (OB_UNLIKELY(nullptr == buffer_) || position < offset_
      || position + nbytes > offset_ + length_) {
    in_range = false;
    LOG_TRACE("out of prebuffer range", K(position), K(nbytes), K(offset_), K(length_));
  }
  return in_range;
}


void ObFilePrefetchBuffer::fetch(const int64_t position, const int64_t nbytes, void *out)
{
  MEMCPY(out, (char *)buffer_ + (position - offset_), nbytes);
}

}
}
