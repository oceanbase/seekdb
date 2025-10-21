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

#define USING_LOG_PREFIX SQL_PC
#include "ob_ps_sql_utils.h"
namespace oceanbase
{
using namespace common;
namespace sql
{

int ObPsSqlUtils::deep_copy_str(common::ObIAllocator &allocator,
                                const common::ObString &src,
                                common::ObString &dst)
{
  int ret = common::OB_SUCCESS;
  int32_t size = src.length() + 1;
  char* buf = static_cast<char *>(allocator.alloc(size));
  if (OB_ISNULL(buf)) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc memory failed", K(ret), K(size), K(src));
  } else {
    MEMCPY(buf, src.ptr(), src.length());
    buf[size-1] = '\0';
    dst.assign_ptr(buf, src.length());
  }
  return ret;
}

} //end of sql
} //end of oceanbase
