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
#ifndef OB_STORAGE_COLUMN_STORE_OB_CG_ITER_PARAM_POOL_H_
#define OB_STORAGE_COLUMN_STORE_OB_CG_ITER_PARAM_POOL_H_
#include "lib/container/ob_se_array.h"
#include "lib/allocator/ob_allocator.h"
#include "sql/engine/expr/ob_expr.h"

namespace oceanbase
{
namespace storage
{
struct ObTableIterParam;
class ObCGIterParamPool
{
public:
  static const int64_t DEFAULT_ITER_PARAM_CNT = 2;
  ObCGIterParamPool(common::ObIAllocator &alloc);
  ~ObCGIterParamPool() { reset(); }
  void reset();
  int get_iter_param(
    const int32_t cg_idx,
    const ObTableIterParam &row_param,
    sql::ObExpr *expr,
    ObTableIterParam *&iter_param);
  int get_iter_param(
    const int32_t cg_idx,
    const ObTableIterParam &row_param,
    const common::ObIArray<sql::ObExpr*> &output_exprs,
    ObTableIterParam *&iter_param,
    const common::ObIArray<sql::ObExpr*> *agg_exprs = nullptr);
private:  
  int new_iter_param(
      const int32_t cg_idx,
      const ObTableIterParam &row_param,
      const common::ObIArray<sql::ObExpr*> &output_exprs,
      ObTableIterParam *&iter_param,
      const common::ObIArray<sql::ObExpr*> *agg_exprs);
  int fill_cg_iter_param(
      const ObTableIterParam &row_param,
      const int32_t cg_idx,
      const common::ObIArray<sql::ObExpr*> &output_exprs,
      ObTableIterParam &cg_param,
      const common::ObIArray<sql::ObExpr*> *agg_exprs);
  int fill_virtual_cg_iter_param(
      const ObTableIterParam &row_param,
      const int32_t cg_idx,
      const common::ObIArray<sql::ObExpr*> &exprs,
      ObTableIterParam &cg_param,
      const common::ObIArray<sql::ObExpr*> *agg_exprs);
  int generate_for_column_store(
      const ObTableIterParam &row_param,
      const sql::ExprFixedArray *output_exprs,
      const sql::ExprFixedArray *agg_exprs,
      const ObIArray<int32_t> *out_cols_project,
      const int32_t cg_idx,
      ObTableIterParam &cg_param);
  int copy_param_exprs(
      const common::ObIArray<sql::ObExpr*> &exprs,
      sql::ExprFixedArray *&param_exprs);
  int put_iter_param(ObTableIterParam *iter_param);
  void free_iter_param(ObTableIterParam *iter_param);
  common::ObIAllocator &alloc_;
  common::ObSEArray<ObTableIterParam*, DEFAULT_ITER_PARAM_CNT> iter_params_;
};

}
}

#endif
