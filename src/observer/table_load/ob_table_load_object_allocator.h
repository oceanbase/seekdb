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

#pragma once

#include "lib/allocator/ob_small_allocator.h"

namespace oceanbase
{
namespace observer
{

template<class T>
class ObTableLoadObjectAllocator
{
public:
  static const int64_t DEFAULT_MIN_OBJ_COUNT_ON_BLOCK = 1;
  ObTableLoadObjectAllocator() : size_(0) {}
  ~ObTableLoadObjectAllocator() {}

  template<typename... Args>
  T *alloc(Args&&... args);
  void free(T *t);

  int init(const lib::ObLabel &label = nullptr,
           const uint64_t tenant_id = common::OB_SERVER_TENANT_ID,
           const int64_t block_size = common::OB_MALLOC_NORMAL_BLOCK_SIZE,
           const int64_t min_obj_count_on_block = DEFAULT_MIN_OBJ_COUNT_ON_BLOCK,
           const int64_t limit_num = INT64_MAX) {
    return small_allocator_.init(sizeof(T), label, tenant_id, block_size, min_obj_count_on_block, limit_num);
  }

  int64_t size() const { return ATOMIC_LOAD(&size_); }

private:
  common::ObSmallAllocator small_allocator_;
  int64_t size_;
};

template<class T>
template<typename... Args>
T *ObTableLoadObjectAllocator<T>::alloc(Args&&... args)
{
  T *t = nullptr;
  void *buf = nullptr;
  if (OB_NOT_NULL(buf = small_allocator_.alloc())) {
    t = new (buf) T(args...);
    ATOMIC_AAF(&size_, 1);
  }
  return t;
}

template<class T>
void ObTableLoadObjectAllocator<T>::free(T *t)
{
  if (OB_NOT_NULL(t)) {
    t->~T();
    small_allocator_.free(t);
    ATOMIC_AAF(&size_, -1);
  }
}

}  // namespace observer
}  // namespace oceanbase
