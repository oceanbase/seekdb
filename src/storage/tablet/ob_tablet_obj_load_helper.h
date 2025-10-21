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

#ifndef OCEANBASE_STORAGE_OB_TABLET_OBJ_LOAD_HELPER
#define OCEANBASE_STORAGE_OB_TABLET_OBJ_LOAD_HELPER

#include <stdint.h>
#include "lib/ob_errno.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/oblog/ob_log.h"

namespace oceanbase
{
namespace common
{
class ObArenaAllocator;
}

namespace storage
{
class ObMetaDiskAddr;

class ObTabletObjLoadHelper
{
public:
  template <typename T>
  static int alloc_and_new(common::ObIAllocator &allocator, T *&ptr);

  template <typename T>
  static void free(common::ObIAllocator &allocator, T *&ptr);

  static int read_from_addr(
    common::ObArenaAllocator &allocator,
    const ObMetaDiskAddr &meta_addr,
    char *&buf,
    int64_t &buf_len);
};

template <typename T>
int ObTabletObjLoadHelper::alloc_and_new(common::ObIAllocator &allocator, T *&ptr)
{
  int ret = common::OB_SUCCESS;
  void *buffer = allocator.alloc(sizeof(T));

  if (OB_ISNULL(buffer)) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "fail to allocate memory", K(ret), "size", sizeof(T));
  } else {
    ptr = new (buffer) T();
  }

  return ret;
}

template <typename T>
void ObTabletObjLoadHelper::free(common::ObIAllocator &allocator, T *&ptr)
{
  if (nullptr != ptr) {
    ptr->~T();
    allocator.free(ptr);
    ptr = nullptr;
  }
}
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_OBJ_LOAD_HELPER
