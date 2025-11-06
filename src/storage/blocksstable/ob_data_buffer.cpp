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

#include "ob_data_buffer.h"
#include "share/rc/ob_tenant_base.h"
using namespace oceanbase;
using namespace common;
using namespace share;

namespace oceanbase
{
namespace blocksstable
{
ObSelfBufferWriter::ObSelfBufferWriter(
    const char *label, const int64_t size, const bool need_align, const bool use_fixed_blk)
    : ObBufferWriter(NULL, 0, 0), label_(label), is_aligned_(need_align), use_fixed_blk_(use_fixed_blk)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ensure_space(size))) {
    STORAGE_LOG(WARN, "cannot allocate memory for data buffer.", K(size), K(ret));
  }
}

ObSelfBufferWriter::~ObSelfBufferWriter()
{
  free();
  is_aligned_ = false;
  pos_ = 0;
  capacity_ = 0;
  use_fixed_blk_ = false;
}

char *ObSelfBufferWriter::alloc(const int64_t size)
{

  char *data = NULL;
  if (is_aligned_) {
    data = (char *)mtl_malloc_align(BUFFER_ALIGN_SIZE, size, label_);
  } else {
    data = (char *)mtl_malloc(size, label_);
  }
  return data;
}

int ObSelfBufferWriter::clean()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(data_) || capacity_ <= 0) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "do not clean ", KR(ret), KP(data_), K(capacity_));
  } else {
    MEMSET(data_, 0, capacity_);
  }
  return ret;
}

int ObSelfBufferWriter::ensure_space(int64_t size)
{
  int ret = OB_SUCCESS;
  //size = upper_align(size, common::ObLogConstants::LOG_FILE_ALIGN_SIZE);
  if (size <= 0) {
    // do nothing.
  } else if (is_aligned_ && size % BUFFER_ALIGN_SIZE != 0) {
    STORAGE_LOG(WARN, "not aligned buffer size", K(is_aligned_), K(size));
    ret = OB_INVALID_ARGUMENT;
  } else if (NULL == data_) {
    if (NULL == (data_ = alloc(size))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "allocate buffer memory error.", K(ret), K(size));
    } else {
      pos_ = 0;
      capacity_ = size;
    }
  } else if (capacity_ < size) {
    // resize;
    char *new_data = NULL;
    if (NULL == (new_data = alloc(size))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "allocate buffer memory error.", K(ret), K(size));
    } else {
      MEMCPY(new_data, data_, pos_);
      free();
      capacity_ = size;
      data_ = new_data;
    }
  }

  return ret;
}

void ObSelfBufferWriter::free()
{
  if (NULL != data_) {
    if (is_aligned_) {
      mtl_free_align(data_);
    } else {
      mtl_free(data_);
    }
    data_ = NULL;
  }
}
}
}
