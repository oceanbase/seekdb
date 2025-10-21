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

#ifndef OCEANBASE_EXPR_CMP_FUNC_H_
#define OCEANBASE_EXPR_CMP_FUNC_H_

#include "sql/engine/expr/ob_expr.h"
#include "common/object/ob_object.h"
#include "common/object/ob_obj_compare.h"
#include "lib/charset/ob_charset.h"

namespace oceanbase
{
namespace common
{
struct ObDatum;
}
namespace sql
{
typedef int (*DatumCmpFunc)(const common::ObDatum &datum1, const common::ObDatum &datum2, int &cmp_ret);
class ObExprCmpFuncsHelper
{
public:
  static sql::ObExpr::EvalFunc get_eval_expr_cmp_func(
      const common::ObObjType type1,
      const common::ObObjType type2,
      const common::ObScale scale1,
      const common::ObScale scale2,
      const common::ObPrecision prec1,
      const common::ObPrecision prec2,
      const common::ObCmpOp cmp_op,
      const bool is_oracle_mode,
      const common::ObCollationType cs_type,
      const bool has_lob_header);

  static sql::ObExpr::EvalBatchFunc get_eval_batch_expr_cmp_func(
      const common::ObObjType type1,
      const common::ObObjType type2,
      const common::ObScale scale1,
      const common::ObScale scale2,
      const common::ObPrecision prec1,
      const common::ObPrecision prec2,
      const common::ObCmpOp cmp_op,
      const bool is_oracle_mode,
      const common::ObCollationType cs_type,
      const bool has_lob_header);

  static DatumCmpFunc get_datum_expr_cmp_func(
      const common::ObObjType type1,
      const common::ObObjType type2,
      const common::ObScale scale1,
      const common::ObScale scale2,
      const common::ObPrecision prec1,
      const common::ObPrecision prec2,
      const bool is_oracle_mode,
      const common::ObCollationType cs_type,
      const bool has_lob_header);
};
}
} // end namespace oceanbase
#endif // !OCEANBASE_EXPR_CMP_FUNC_H_
