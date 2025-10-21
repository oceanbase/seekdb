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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_ESTIMATE_NDV_H_
#define OCEANBASE_SQL_ENGINE_EXPR_ESTIMATE_NDV_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase {
namespace sql {
class ObExprEstimateNdv : public ObFuncExprOperator {
public:
  explicit ObExprEstimateNdv(common::ObIAllocator &alloc);
  virtual ~ObExprEstimateNdv();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
  static void llc_estimate_ndv(int64_t &result, const common::ObString &bitmap_str);
  static int llc_estimate_ndv(double &estimate_ndv, const common::ObString &bitmap_buf);
  // Calculate the leading zeros of value. In HyperLogLogCount, several leading bits of a hash value are used for bucketing,
  // Here the input parameter value is obtained by left-shifting to remove the bucket part, and its actual effective bit width is the high bit_width bits.
  static uint64_t llc_leading_zeros(uint64_t value, uint64_t bit_width);
  static bool llc_is_num_buckets_valid(int64_t num_buckets);
  // for engine 3.0
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const override;
  static int calc_estimate_ndv_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                    ObDatum &res_datum);
private:
  // Calculate the function for alpha * m^2 in HyperLogLogCount. The calculation involves variable m(uint64_t)
  // Convert to double steps, caller needs to consider possible precision loss (currently m usually does not exceed 4096, no loss).
  static inline double llc_alpha_times_m_square(const uint64_t m);
  // According to Google's HLLC paper, the number of buckets should be at least 2^4 (16) and at most 2^16 (65536).
  static const int LLC_NUM_BUCKETS_MIN = (1 << 4);
  static const int LLC_NUM_BUCKETS_MAX = (1 << 16);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprEstimateNdv);
};
} /* namespace sql */
} /* namespace oceanbase */



#endif /* OCEANBASE_SQL_ENGINE_EXPR_ESTIMATE_NDV_H_ */
