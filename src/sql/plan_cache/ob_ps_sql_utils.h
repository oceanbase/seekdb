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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_PS_SQL_UTILS_H_
#define OCEANBASE_SQL_PLAN_CACHE_OB_PS_SQL_UTILS_H_

#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "common/data_buffer.h"
#include "sql/parser/parse_node.h"
#include "lib/container/ob_iarray.h"
#include "lib/container/ob_bit_set.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObString;
}
namespace sql
{
class ObPsSqlUtils
{
public:
  template<typename T>
  static int alloc_new_var(common::ObIAllocator &allocator, const T &t, T *&new_t);

  static int deep_copy_str(common::ObIAllocator &allocator,
                           const common::ObString &src,
                           common::ObString &dst);
  // Get the memory occupied by a certain ObPsStmtItem/ObPsStmtInfo object
  template<typename T>
  static int get_var_mem_total(const T &t, int64_t &size);
};

template<typename T>
int ObPsSqlUtils::alloc_new_var(common::ObIAllocator &allocator, const T &t, T *&new_t)
{
  int ret = common::OB_SUCCESS;
  new_t = NULL;
  common::ObDataBuffer *data_buf = NULL;
  int64_t cv_size = 0;
  if (OB_FAIL(t.get_convert_size(cv_size))) {
    SQL_PC_LOG(WARN, "get_convert_size failed", K(ret));
  } else {
    const int64_t size = cv_size + sizeof(common::ObDataBuffer);
    char *buf = static_cast<char *>(allocator.alloc(size));
    if (OB_ISNULL(buf)) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      SQL_PC_LOG(WARN, "failed to alloc memory");
    } else {
      data_buf = new (buf + sizeof(T)) common::ObDataBuffer(buf + sizeof(T) + sizeof(common::ObDataBuffer),
                                                            size - sizeof(T) - sizeof(common::ObDataBuffer));
      new_t = new (buf) T(data_buf, &allocator);
      if (OB_FAIL(new_t->deep_copy(t))) {
        SQL_PC_LOG(WARN, "deep copy failed", K(ret), K(cv_size));
      }
    }
  }
  return ret;
}

template<typename T>
int ObPsSqlUtils::get_var_mem_total(const T &t, int64_t &size)
{
  int ret = common::OB_SUCCESS;
  size = 0;
  if (OB_FAIL(t.get_convert_size(size))) {
    SQL_PC_LOG(WARN, "get_convert_size failed", K(ret));
  } else {
    size += sizeof(common::ObDataBuffer);
  }
  return ret;
}

} //end of sql
} //end of oceanbase


#endif //OCEANBASE_SQL_PLAN_CACHE_OB_PS_SQL_UTILS_H_
