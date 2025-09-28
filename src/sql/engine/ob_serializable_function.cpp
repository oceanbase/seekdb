/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
