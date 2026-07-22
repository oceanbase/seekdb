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

#define USING_LOG_PREFIX SQL_DAS
#include "sql/das/ob_das_id_allocator.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace sql
{

int ObDASIDAllocator::get_next_id(int64_t &id)
{
  int ret = OB_SUCCESS;
  const int64_t next_id = ATOMIC_FAA(&next_id_, 1);
  if (OB_UNLIKELY(next_id <= 0)) {
    ret = OB_SIZE_OVERFLOW;
    SQL_DAS_LOG(ERROR, "local DAS task id exhausted", KR(ret), K(next_id));
  } else {
    id = next_id;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
