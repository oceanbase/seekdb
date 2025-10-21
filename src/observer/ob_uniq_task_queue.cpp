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

#include "ob_uniq_task_queue.h"
namespace oceanbase
{
namespace observer
{
void *ObHighPrioMemAllocator::alloc(const int64_t sz)
{
  void *mem = NULL;
  int ret = common::OB_SUCCESS;
  if (sz > 0) {
    mem = common::ob_malloc(sz, attr_);
    if (NULL == mem) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      SERVER_LOG(ERROR, "alloc memory failed", K(ret), K(sz), K_(attr_.label));
    }
  }
  return mem;
}

void ObHighPrioMemAllocator::free(void *p)
{
  if (NULL != p) {
    common::ob_free(p);
    p = NULL;
  }
}
}
}
