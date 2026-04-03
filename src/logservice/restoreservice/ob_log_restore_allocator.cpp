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
#include "ob_log_restore_allocator.h"

namespace oceanbase
{
namespace logservice
{
ObLogRestoreAllocator::ObLogRestoreAllocator() :
  inited_(false),
  tenant_id_(OB_INVALID_TENANT_ID),
  iterator_buf_allocator_()
{}

ObLogRestoreAllocator::~ObLogRestoreAllocator()
{
  destroy();
}

int ObLogRestoreAllocator::init(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  const int64_t max_restore_data_size = 1024 * 1024 * 1024L;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(WARN, "ObLogRestoreAllocator has been inited", K(ret), K(tenant_id));
  } else if (OB_FAIL(iterator_buf_allocator_.init("IterBuf", max_restore_data_size))) {
    CLOG_LOG(WARN, "iterator_buf_allocator_ init failed", K(ret));
  } else {
    tenant_id_ = tenant_id;
    inited_ = true;
  }
  return ret;
}

void ObLogRestoreAllocator::destroy()
{
  if (inited_) {
    tenant_id_ = OB_INVALID_TENANT_ID;
    (void)iterator_buf_allocator_.destroy();
    inited_ = false;
  }
}

char *ObLogRestoreAllocator::alloc_iterator_buffer(const int64_t size)
{
  char *data = NULL;

  if (OB_UNLIKELY(! inited_)) {
    ARCHIVE_LOG_RET(WARN, OB_NOT_INIT, "ObArchiveAllocator not init");
  } else if (OB_ISNULL(data = static_cast<char *>(iterator_buf_allocator_.acquire(size)))) {
    // alloc fail
  } else {
  }
  return data;
}

void ObLogRestoreAllocator::free_iterator_buffer(void *buf)
{
  if (NULL != buf) {
    iterator_buf_allocator_.reclaim(buf);
  }
}

void ObLogRestoreAllocator::weed_out_iterator_buffer()
{
  iterator_buf_allocator_.weed_out();
}

} // namespace logservice
} // namespace oceanbase
