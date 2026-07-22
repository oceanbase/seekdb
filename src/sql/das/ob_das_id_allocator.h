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

#ifndef OCEANBASE_SQL_DAS_OB_DAS_ID_ALLOCATOR_H_
#define OCEANBASE_SQL_DAS_OB_DAS_ID_ALLOCATOR_H_

#include "lib/atomic/ob_atomic.h"

namespace oceanbase
{
namespace sql
{

class ObDASIDAllocator
{
public:
  ObDASIDAllocator() : next_id_(1) {}
  ~ObDASIDAllocator() = default;

  int get_next_id(int64_t &id);

private:
  int64_t next_id_ CACHE_ALIGNED;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_DAS_OB_DAS_ID_ALLOCATOR_H_
