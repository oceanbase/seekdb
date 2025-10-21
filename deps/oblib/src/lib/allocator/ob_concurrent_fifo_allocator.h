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

#ifndef  OCEANBASE_COMMON_CONCURRENT_FIFO_ALLOCATOR_H_
#define  OCEANBASE_COMMON_CONCURRENT_FIFO_ALLOCATOR_H_
#include "lib/allocator/ob_lf_fifo_allocator.h"

namespace oceanbase
{
namespace common
{
class ObConcurrentFIFOAllocator : public common::ObIAllocator
{
public:
  ObConcurrentFIFOAllocator();
  virtual ~ObConcurrentFIFOAllocator();
public:
  int init(const int64_t total_limit,
           const int64_t hold_limit,
           const int64_t page_size);
  int init(const int64_t page_size,
           const lib::ObLabel &label,
           const uint64_t tenant_id,
           const int64_t total_limit);
  int init(const int64_t page_size,
           const lib::ObMemAttr &attr,
           const int64_t total_limit);
  void destroy();
  void purge();
public:
  void set_label(const lib::ObLabel &label);
  void set_attr(const lib::ObMemAttr &attr);
  void set_nway(int nway) { inner_allocator_.set_nway(nway); }
  void *alloc(const int64_t size);
  void *alloc(const int64_t size, const ObMemAttr &attr);
  void free(void *ptr);
  int64_t allocated() const;
  int64_t hold() const {return 0;}
  void set_total_limit(int64_t total_limit);
private:
  static const int64_t STORAGE_SIZE_TIMES = 2;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConcurrentFIFOAllocator);
private:
  ObLfFIFOAllocator inner_allocator_;
};
}
}

#endif //OCEANBASE_COMMON_FIFO_ALLOCATOR_H_
