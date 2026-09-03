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

#define USING_LOG_PREFIX LIB

#include "ob_malloc_allocator.h"
#include "lib/allocator/ob_jemalloc.h"

#include <cstdlib>

#if defined(__linux__) && !defined(__ANDROID__) && !defined(OB_USE_ASAN) \
    && !defined(OB_HAVE_BUNDLED_JEMALLOC)
#error "Linux builds must use the bundled jemalloc allocator"
#endif

using namespace oceanbase::lib;
using namespace oceanbase::common;

namespace oceanbase
{
namespace lib
{

ObMallocAllocator::ObMallocAllocator() = default;

ObMallocAllocator::~ObMallocAllocator() = default;

void *ObMallocAllocator::alloc(const int64_t size)
{
  ObMemAttr attr;
  return alloc(size, attr);
}

void *ObMallocAllocator::alloc(const int64_t size,
                               const oceanbase::lib::ObMemAttr &attr)
{
  return size > 0 ? realloc(NULL, size, attr) : NULL;
}

void *ObMallocAllocator::realloc(
    const void *ptr, const int64_t size,
    const oceanbase::lib::ObMemAttr &attr)
{
  UNUSED(attr);
  void *nptr = NULL;
  if (OB_LIKELY(size >= 0)) {
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
    nptr = oceanbase::common::jemalloc_realloc(
        const_cast<void *>(ptr), static_cast<size_t>(size));
#else
    nptr = ::realloc(const_cast<void *>(ptr), static_cast<size_t>(size));
#endif
  }
  return nptr;
}

void ObMallocAllocator::free(void *ptr)
{
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
  oceanbase::common::jemalloc_free(ptr);
#else
  ::free(ptr);
#endif
}

ObMallocAllocator *ObMallocAllocator::get_instance()
{
  static ObMallocAllocator instance;
  return &instance;
}

} // end of namespace lib
} // end of namespace oceanbase
