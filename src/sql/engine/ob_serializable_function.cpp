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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_serializable_function.h"
#include "sql/engine/expr/ob_expr.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

#define BOOL_EXTERN_DECLARE(id) CONCAT(extern bool g_reg_ser_func_, id)
#define REG_UNUSED_SER_FUNC_ARRAY(id) CONCAT(bool g_reg_ser_func_, id)
#define LIST_REGISTERED_FUNC_ARRAY(id) CONCAT(g_reg_ser_func_, id)

LST_DO_CODE(BOOL_EXTERN_DECLARE, SER_FUNC_ARRAY_ID_ENUM);
LST_DO_CODE(REG_UNUSED_SER_FUNC_ARRAY, UNUSED_SER_FUNC_ARRAY_ID_ENUM);

#undef LIST_REGISTERED_FUNC_ARRAY
#undef REG_UNUSED_SER_FUNC_ARRAY
#undef BOOL_EXTERN_DECLARE

bool ObFuncSerialization::reg_func_array(
    const ObSerFuncArrayID id, void **array, const int64_t size)
{
  bool succ = true;
  return succ;
}

// All serializable functions should register here.
void *g_all_misc_serializable_functions[] = {
  NULL
  // append only, only mark delete allowed.
};

REG_SER_FUNC_ARRAY(OB_SFA_ALL_MISC, g_all_misc_serializable_functions,
                   ARRAYSIZEOF(g_all_misc_serializable_functions));

// define item[X][Y] offset in source array
#define SRC_ITEM_OFF(X, Y) ((X) * n * row_size + (Y) * row_size)
#define COPY_FUNCS
bool ObFuncSerialization::convert_NxN_array(
    void **dst, void **src, const int64_t n,
    const int64_t row_size, // = 1
    const int64_t copy_row_idx, // = 0
    const int64_t copy_row_cnt) // = 1
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(dst) || OB_ISNULL(src) || n < 0 || row_size < 0
      || copy_row_idx < 0 || copy_row_idx >= row_size
      || copy_row_cnt < 1 || copy_row_cnt > row_size) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", K(ret), KP(dst), KP(src), K(n),
              K(row_size), K(copy_row_idx), K(copy_row_cnt));
  } else {
    const int64_t mem_copy_size = copy_row_cnt * sizeof(void *);
    int64_t idx = 0;
    for (int64_t i = 0; i < n; i++) {
      for (int64_t j = 0; j < i; j++) {
        memcpy(&dst[idx], &src[SRC_ITEM_OFF(i, j) + copy_row_idx], mem_copy_size);
        idx += copy_row_cnt;
        memcpy(&dst[idx], &src[SRC_ITEM_OFF(j, i) + copy_row_idx], mem_copy_size);
        idx += copy_row_cnt;
      }
      memcpy(&dst[idx], &src[SRC_ITEM_OFF(i, i) + copy_row_idx], mem_copy_size);
      idx += copy_row_cnt;
    }
  }
  return OB_SUCCESS == ret;
}

} // end namespace sql
} // end namespace oceanbase
