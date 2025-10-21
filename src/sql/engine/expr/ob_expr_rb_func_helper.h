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

#ifndef OCEANBASE_SQL_OB_EXPR_RB_FUNC_HELPER_
#define OCEANBASE_SQL_OB_EXPR_RB_FUNC_HELPER_

#include "sql/engine/expr/ob_expr_util.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/expr/ob_expr_result_type_util.h"
#include "sql/engine/ob_exec_context.h"
#include "share/object/ob_obj_cast.h"
#include "objit/common/ob_item_type.h"
#include "sql/session/ob_sql_session_info.h"
#include "lib/roaringbitmap/ob_roaringbitmap.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
class ObRbExprHelper final
{
public:

  static int get_input_roaringbitmap_bin(ObEvalCtx &ctx, ObIAllocator &allocator, ObExpr *rb_arg, ObString &rb_bin, bool &is_rb_null);
  static int get_input_roaringbitmap(ObEvalCtx &ctx, ObIAllocator &allocator, ObExpr *rb_arg, ObRoaringBitmap *&rb, bool &is_rb_null);
  static int get_input_roaringbitmap(ObEvalCtx &ctx, ObIAllocator &allocator, ObExpr *rb_arg, ObIVector *&rb_vec, ObRoaringBitmap *&rb, bool &is_rb_null, int64_t idx);
  static int pack_rb_res(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res, const ObString &str);
  static uint64_t get_tenant_id(ObSQLSessionInfo *session);

private:
  // const static uint32_t RESERVE_MIN_BUFF_SIZE = 32;
  DISALLOW_COPY_AND_ASSIGN(ObRbExprHelper);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_RB_FUNC_HELPER_
