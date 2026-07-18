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

#ifndef OCEANBASE_SQL_ENGINE_DECODE_TRACE_ID_
#define OCEANBASE_SQL_ENGINE_DECODE_TRACE_ID_
#include "sql/engine/expr/ob_expr_operator.h"
#define MAX_DECODE_TRACE_ID_RES_LEN 128 // max string buffer length
namespace oceanbase {
namespace sql {
class ObExprDecodeTraceId : public ObFuncExprOperator {
public:
  explicit ObExprDecodeTraceId(common::ObIAllocator &alloc);
  virtual ~ObExprDecodeTraceId();
  virtual int calc_result_type1(ObExprResType &type, ObExprResType &trace_id,
                                common::ObExprTypeCtx &type_ctx) const;
  static int calc_decode_trace_id_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                       ObDatum &res_datum);
  static int calc_decode_trace_id_expr_batch(const ObExpr &expr, ObEvalCtx &ctx,
                                             const ObBitVector &skip,
                                             const int64_t batch_size);
                                             
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  template <typename T>
  static int calc_one_row(const ObExpr &expr, ObEvalCtx &ctx, const T &param, T &res)
  {
    int ret = OB_SUCCESS;
    ObCurTraceId::TraceId trace_id;
    int32_t len = 0;
    char *buf = expr.get_str_res_mem(ctx, MAX_DECODE_TRACE_ID_RES_LEN);
    ObString trace_id_str = param.get_string();
    if (OB_ISNULL(buf)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("allocate string res buf failed", K(ret));
    } else if (trace_id_str.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid trace id string", K(ret));
    } else if (OB_FAIL(trace_id.parse_from_buf(trace_id_str.ptr()))) {
      LOG_WARN("parse trace_id failed", K(ret));
    } else if (OB_FAIL(trace_id.get_addr().addr_to_buffer(buf, MAX_DECODE_TRACE_ID_RES_LEN, len))) {
      SQL_ENG_LOG(WARN, "fail to databuff_printf", K(ret));
    } else {
      res.set_string(buf, len);
    }
    return ret;
  }
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprDecodeTraceId);
};
} // namespace sql
} // namespace oceanbase
#endif /* OCEANBASE_SQL_ENGINE_DECODE_TRACE_ID_ */
