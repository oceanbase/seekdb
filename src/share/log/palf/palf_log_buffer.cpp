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

#define USING_LOG_PREFIX PALF

#include "share/log/palf/palf_log_buffer.h"
#include "lib/allocator/ob_malloc.h"

namespace oceanbase
{
using namespace common;
namespace palf
{

PalfLogBuffer::PalfLogBuffer()
{}

PalfLogBuffer::~PalfLogBuffer()
{
  reset();
}

int PalfLogBuffer::init(const int64_t capacity, const int64_t prefix_size)
{
  int ret = OB_SUCCESS;
  const int64_t alloc_size = capacity + prefix_size;
  if (NULL != allocation_) {
    ret = OB_INIT_TWICE;
  } else if (capacity <= 0 || prefix_size < 0 || alloc_size <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(allocation_ = static_cast<char *>(
                 ob_malloc(alloc_size, "PalfLogBuffer")))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    data_ = allocation_ + prefix_size;
    size_ = 0;
    capacity_ = capacity;
    prefix_size_ = prefix_size;
    is_sealed_ = false;
  }
  return ret;
}

int PalfLogBuffer::copy_from(const char *buf,
                             const int64_t size,
                             const int64_t prefix_size)
{
  int ret = OB_SUCCESS;
  if (NULL == buf || size <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(init(size, prefix_size))) {
    PALF_LOG(WARN, "init owned log buffer failed", K(ret), K(size), K(prefix_size));
  } else {
    MEMCPY(data_, buf, size);
    size_ = size;
    is_sealed_ = true;
  }
  return ret;
}

int PalfLogBuffer::extend_and_copy(const int64_t new_capacity,
                                   const int64_t valid_size)
{
  int ret = OB_SUCCESS;
  char *new_allocation = NULL;
  if (NULL == allocation_ || is_sealed_ || new_capacity <= capacity_
      || valid_size < 0 || valid_size > capacity_) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(new_allocation = static_cast<char *>(
                 ob_malloc(new_capacity + prefix_size_, "PalfLogBuffer")))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    char *new_data = new_allocation + prefix_size_;
    if (valid_size > 0) {
      MEMCPY(new_data, data_, valid_size);
    }
    ob_free(allocation_);
    allocation_ = new_allocation;
    data_ = new_data;
    capacity_ = new_capacity;
    size_ = valid_size;
  }
  return ret;
}

int PalfLogBuffer::seal(const int64_t size)
{
  int ret = OB_SUCCESS;
  if (NULL == allocation_ || size <= 0 || size > capacity_) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    size_ = size;
    is_sealed_ = true;
  }
  return ret;
}

int PalfLogBuffer::reuse_for_write()
{
  int ret = OB_SUCCESS;
  if (NULL == allocation_) {
    ret = OB_NOT_INIT;
  } else {
    size_ = 0;
    is_sealed_ = false;
  }
  return ret;
}

int PalfLogBuffer::move_from(PalfLogBuffer &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    reset();
    allocation_ = other.allocation_;
    data_ = other.data_;
    size_ = other.size_;
    capacity_ = other.capacity_;
    prefix_size_ = other.prefix_size_;
    is_sealed_ = other.is_sealed_;
    other.allocation_ = NULL;
    other.data_ = NULL;
    other.size_ = 0;
    other.capacity_ = 0;
    other.prefix_size_ = 0;
    other.is_sealed_ = false;
  }
  return ret;
}

void PalfLogBuffer::reset()
{
  if (NULL != allocation_) {
    ob_free(allocation_);
  }
  allocation_ = NULL;
  data_ = NULL;
  size_ = 0;
  capacity_ = 0;
  prefix_size_ = 0;
  is_sealed_ = false;
}

bool PalfLogBuffer::is_valid() const
{
  return NULL != allocation_ && NULL != data_ && capacity_ > 0
      && size_ >= 0 && size_ <= capacity_;
}

char *PalfLogBuffer::get_prefix_buf(const int64_t size)
{
  return (NULL != data_ && size >= 0 && size <= prefix_size_) ? data_ - size : NULL;
}

const char *PalfLogBuffer::get_prefix_buf(const int64_t size) const
{
  return (NULL != data_ && size >= 0 && size <= prefix_size_) ? data_ - size : NULL;
}

} // namespace palf
} // namespace oceanbase
