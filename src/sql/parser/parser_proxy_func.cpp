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

#define USING_LOG_PREFIX SQL_PARSER


#include "parser_proxy_func.h"
#include "lib/oblog/ob_log.h"
#include "lib/allocator/ob_allocator.h"


using namespace oceanbase::common;

void *parser_alloc_buffer(void *malloc_pool, const int64_t alloc_size)
{
  int ret = OB_SUCCESS;
  void *alloced_buf = NULL;
  if (OB_ISNULL(malloc_pool)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid malloc pool", K(ret));
  } else {
    ObIAllocator *allocator = static_cast<ObIAllocator *>(malloc_pool);
    if (OB_ISNULL(alloced_buf = allocator->alloc(alloc_size))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", K(ret), K(alloc_size));
    } else {
      // do nothing
    }
  }
  return alloced_buf;
}

void parser_free_buffer(void *malloc_pool, void *buffer)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(malloc_pool)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null malloc pool", K(ret));
  } else {
    ObIAllocator *allocator = static_cast<ObIAllocator *>(malloc_pool);
    allocator->free(buffer);
  }
}
