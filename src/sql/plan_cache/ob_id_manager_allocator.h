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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_ID_MANAGER_ALLOCATOR_
#define OCEANBASE_SQL_PLAN_CACHE_OB_ID_MANAGER_ALLOCATOR_

#include "lib/allocator/ob_malloc.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/ob_small_allocator.h"
#include "lib/alloc/alloc_struct.h"

namespace oceanbase
{
namespace sql
{

// ObIdManagerAllocator is used for small-size memory allocation.
// Use ObSmallAllocator for size less than object_size_, else use ObMalloc.
// ALLOC_MAGIC is used to mark and check the memory allocated.
class ObIdManagerAllocator : public common::ObIAllocator
{
public:
  ObIdManagerAllocator();
  ~ObIdManagerAllocator();


  void *alloc(const int64_t sz) override { return alloc_(sz); }
  void* alloc(const int64_t size, const ObMemAttr &attr) override
  {
    UNUSED(attr);
    return alloc(size);
  }
  void free(void *ptr) override { free_(ptr); }
  void reset();

private:
  void *alloc_(const int64_t sz);
  void free_(void *ptr);

private:
  const static int64_t ALLOC_MAGIC = 0x1A4420844B;
  const static int64_t SMALL_ALLOC_SYMBOL = 0x38;
  const static int64_t M_ALLOC_SYMBOL = 0x7;
  const static int EXTEND_SIZE = sizeof(int64_t) * 2;
  const static int64_t BLOCK_SIZE = 16 * 1024; //16K

  common::ObSmallAllocator small_alloc_;
  common::ObMalloc m_alloc_;
  lib::ObMemAttr mem_attr_;
  int64_t object_size_;
  bool inited_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObIdManagerAllocator);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_PLAN_CACHE_OB_ID_MANAGER_ALLOCATOR_
