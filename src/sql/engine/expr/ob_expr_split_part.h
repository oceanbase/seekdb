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

#ifndef _OB_SQL_EXPR_SPLIT_PART_H_
#define _OB_SQL_EXPR_SPLIT_PART_H_
#include "sql/engine/expr/ob_expr_operator.h"
namespace oceanbase
{
namespace sql
{
class ObExprSplitPart : public ObStringExprOperator
{
public:
  explicit ObExprSplitPart(common::ObIAllocator &alloc);

  virtual ~ObExprSplitPart();

  virtual int calc_result_typeN(ObExprResType &type,
                                 ObExprResType *types,
                                 int64_t param_num,
                                 common::ObExprTypeCtx &type_ctx) const;


  virtual int cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
                      
  OB_INLINE static int calc_split_part(ObCollationType cs_type,
                                     const ObString &str,
                                     const ObString &delimiter,
                                     int64_t start_part, int64_t end_part,
                                     bool &null_res,
                                     ObString &result)
  {
    int ret = OB_SUCCESS;
    null_res = false;
    if (delimiter.empty()) {
      if (start_part == 1 || start_part == 0 || start_part == -1) {
        result.assign_ptr(str.ptr(), str.length());
      } else {
        result.reset();
      }
    } else if (str.empty()) {
      result.reset();
    } else {
      int64_t total_splits = 0;
      int64_t cur_pos = 0;
      ObSEArray<int64_t, 32> splits_pos;
      // the array is [padding_start, ...split_positions, padding_end]
      int64_t idx = 0;
      OZ(splits_pos.push_back(-delimiter.length())); // padding_start
      while (OB_SUCC(ret) && cur_pos <= str.length()) {
        idx = ObCharset::instrb(cs_type, str.ptr() + cur_pos, str.length() - cur_pos,
                                        delimiter.ptr(), delimiter.length());
        if (idx == -1 || (end_part > 0 && total_splits + 1 > end_part)) {
          break;
        } else {
          OZ(splits_pos.push_back(idx + cur_pos));
          total_splits++;
          cur_pos += idx + delimiter.length();
        }
      }
      OZ(splits_pos.push_back(str.length())); // padding_end
      if (OB_SUCC(ret)) {
        if (end_part < start_part) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("Invalid part position, the end part must be greater than "
                   "start part",
                   K(ret), K(start_part), K(end_part));
          LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                         "SPLIT_PART function. The end part must be greater "
                         "than start part.");
        } else if (start_part < 0 && end_part < 0) {
          start_part = start_part + splits_pos.count();
          end_part = end_part + splits_pos.count();
        } else if (start_part < 0 || end_part < 0) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("Invalid part position", K(ret), K(start_part), K(end_part));
          LOG_USER_ERROR(
              OB_INVALID_ARGUMENT,
              "SPLIT_PART function. The start_part and end_part must both be "
              "either positive or negative numbers.");
        }
        if (OB_FAIL(ret)){
        } else if (total_splits + 1 < start_part || 1 > start_part || 1 > end_part) {
          // out of range
          result.reset();
        } else {
          int64_t start_pos = 0;
          int64_t end_pos = 0;
          // the return string is [start_pos, end_pos)
          start_pos = splits_pos.at(start_part - 1) + delimiter.length();
          end_pos = end_part > total_splits + 1 ? str.length() : splits_pos.at(end_part);
          if (start_pos >= end_pos) {
            result.reset();
          } else {
            result.assign_ptr(str.ptr() + start_pos, end_pos - start_pos);
          }
        }
      }
    }
    return ret;
  }
  static int calc_split_part_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                        ObDatum &res);
  static int calc_split_part_expr_vec(const ObExpr &expr,
                                      ObEvalCtx &ctx,
                                      const ObBitVector &skip,
                                      const EvalBound &bound);

  template <typename StartPartVecType, typename EndPartVecType>
  static int calc_split_part_expr_dispatch(const ObExpr &expr,
                                            ObEvalCtx &ctx,
                                            ObCollationType cs_type,
                                            const ObBitVector &skip,
                                            const EvalBound &bound,
                                            ObBitVector &eval_flags,
                                            ObIVector *res_vec,
                                            ObIVector *str_vec,
                                            ObIVector *delimiter_vec,
                                            StartPartVecType *start_part_vec,
                                            EndPartVecType *end_part_vec);
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprSplitPart);
};
}
}

#endif /* _OB_SQL_EXPR_SPLIT_PART_H_ */
