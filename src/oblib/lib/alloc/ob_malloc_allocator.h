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

#ifndef _OB_MALLOC_ALLOCATOR_H_
#define _OB_MALLOC_ALLOCATOR_H_

#include "lib/ob_abort.h"
#include "lib/alloc/ob_iallocator.h"

namespace oceanbase
{
namespace lib
{

// Process allocator façade.  The concrete allocator is selected at build time:
// bundled jemalloc for supported production builds and the platform allocator
// for sanitizer/Windows/Android builds.
class ObMallocAllocator
    : public common::ObIAllocator
{
public:
  ObMallocAllocator();
  virtual ~ObMallocAllocator();

  void *alloc(const int64_t size);
  void *alloc(const int64_t size, const ObMemAttr &attr);
  void *realloc(const void *ptr, const int64_t size, const ObMemAttr &attr);
  void free(void *ptr);

  static ObMallocAllocator *get_instance();

private:
  DISALLOW_COPY_AND_ASSIGN(ObMallocAllocator);
}; // end of class ObMallocAllocator

} // end of namespace lib
} // end of namespace oceanbase

#endif /* _OB_MALLOC_ALLOCATOR_H_ */
