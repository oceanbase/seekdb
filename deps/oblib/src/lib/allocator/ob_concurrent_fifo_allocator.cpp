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

#include "lib/allocator/ob_concurrent_fifo_allocator.h"
#include "lib/ob_running_mode.h"

namespace oceanbase
{
namespace common
{
ObConcurrentFIFOAllocator::ObConcurrentFIFOAllocator()
  : inner_allocator_()
{
}

ObConcurrentFIFOAllocator::~ObConcurrentFIFOAllocator()
{
  destroy();
}

int ObConcurrentFIFOAllocator::init(const int64_t total_limit,
                                    const int64_t hold_limit,
                                    const int64_t page_size)
{
  UNUSED(hold_limit);
  int ret = OB_SUCCESS;
  const int64_t cache_page_count = lib::is_mini_mode() ? 0 : get_cpu_count() * STORAGE_SIZE_TIMES;
  if (OB_FAIL(inner_allocator_.init(
          page_size,
          "ConFifoAlloc",
          OB_SERVER_TENANT_ID,
          cache_page_count,
          total_limit))) {
    LIB_LOG(WARN, "fail to init inner allocator", K(ret));
  }
  return ret;
}

int ObConcurrentFIFOAllocator::init(const int64_t page_size,
                                    const lib::ObMemAttr &attr,
                                    const int64_t total_limit)
{
  int ret = OB_SUCCESS;
  const int64_t cache_page_count = lib::is_mini_mode() ? 0 : get_cpu_count() * STORAGE_SIZE_TIMES;
  if (OB_FAIL(inner_allocator_.init(page_size,
                                    attr,
                                    cache_page_count,
                                    total_limit))) {
    LIB_LOG(WARN, "failed to init inner allocator", K(ret));
  }
  return ret;
}

int ObConcurrentFIFOAllocator::init(const int64_t page_size,
                                    const lib::ObLabel &label,
                                    const uint64_t tenant_id,
                                    const int64_t total_limit)
{
  return init(page_size, ObMemAttr(tenant_id, label), total_limit);
}


void ObConcurrentFIFOAllocator::set_total_limit(int64_t total_limit)
{
  inner_allocator_.set_total_limit(total_limit);
}

void ObConcurrentFIFOAllocator::destroy()
{
  inner_allocator_.destroy();
}

void ObConcurrentFIFOAllocator::purge()
{
  inner_allocator_.purge();
}

void ObConcurrentFIFOAllocator::set_label(const lib::ObLabel &label)
{
  inner_allocator_.set_label(label);
}

void ObConcurrentFIFOAllocator::set_attr(const lib::ObMemAttr &attr)
{
  inner_allocator_.set_attr(attr);
}


int64_t ObConcurrentFIFOAllocator::allocated() const
{
  return inner_allocator_.allocated();
}

void *ObConcurrentFIFOAllocator::alloc(const int64_t size, const ObMemAttr &attr)
{
  return inner_allocator_.alloc(size, attr);
}

void *ObConcurrentFIFOAllocator::alloc(const int64_t size)
{
  return inner_allocator_.alloc(size);
}

void ObConcurrentFIFOAllocator::free(void *ptr)
{
  inner_allocator_.free(ptr);
  ptr = NULL;
}

}
}
