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

#include <cstdlib>

#include "storage/multi_data_source/runtime_utility/common_define.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{

void *DefaultAllocator::alloc(const int64_t size)
{
  void *ptr = std::malloc(size);
  ATOMIC_INC(&alloc_times_);
  MDS_LOG(DEBUG, "alloc test object", KP(ptr), K(size));
  return ptr;
}

void DefaultAllocator::free(void *ptr)
{
  ATOMIC_INC(&free_times_);
  std::free(ptr);
}

void *MdsAllocator::alloc(const int64_t size)
{
  void *ptr = std::malloc(size);
  ATOMIC_INC(&alloc_times_);
  MDS_LOG(DEBUG, "alloc test object", KP(ptr), K(size));
  return ptr;
}

void MdsAllocator::free(void *ptr)
{
  ATOMIC_INC(&free_times_);
  std::free(ptr);
}

} // namespace mds
} // namespace storage
} // namespace oceanbase
