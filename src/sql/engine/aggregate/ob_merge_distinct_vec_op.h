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
#ifndef OCEANBASE_SRC_SQL_ENGINE_AGGREGATE_OB_MERGE_DISTINCT_VEC_OP_H_
#define OCEANBASE_SRC_SQL_ENGINE_AGGREGATE_OB_MERGE_DISTINCT_VEC_OP_H_

#include "sql/engine/ob_operator.h"
#include "sql/engine/aggregate/ob_distinct_op.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"


namespace oceanbase
{
namespace sql
{
class ObCompactRow;
class RowMeta;

class ObMergeDistinctVecSpec : public ObDistinctSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObMergeDistinctVecSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);
};

class ObMergeDistinctVecOp : public ObOperator
{
public:
  ObMergeDistinctVecOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);

  virtual int inner_open() override;
  virtual int inner_close() override;
  virtual int inner_rescan() override;
  virtual int inner_get_next_row() override { return common::OB_NOT_IMPLEMENT; }
  virtual int inner_get_next_batch(const int64_t max_row_cnt);
  virtual void destroy() override;

  template<typename InputVec, bool ALL_ROWS_ACTIVE, bool FIRST_COL, bool HAS_NULL>
  int compare_in_column(InputVec * vec, int64_t first_no_skip_idx,
                      const ObBatchRows *child_brs, int64_t col_idx, int64_t &last_idx);

  template<typename InputVec>
  int compare_in_column_with_format(InputVec *vec, const ObBatchRows *child_brs, int64_t first_no_skip_idx, 
                                    int64_t col_idx, int64_t &last_idx);
  
  typedef ObFixedLengthVector<int64_t, VectorBasicOp<VEC_TC_INTEGER>> FixedLengthVectorBigInt;
  typedef ObDiscreteVector<VectorBasicOp<VEC_TC_STRING>> DiscreteVectorString;

  class Compare
  {
  public:
    Compare() : eval_ctx_(nullptr), cmp_funcs_(nullptr), ret_code_(common::OB_SUCCESS)
    {}

    int init(ObEvalCtx *eval_ctx, const common::ObIArray<ObCmpFunc> *cmp_funcs);
    int equal_in_row(const common::ObIArray<ObExpr*> *set_exprs,
                        const sql::LastCompactRow *r,
                        const int64_t curr_idx,
                        bool &equal);
    ObEvalCtx *eval_ctx_;
    const common::ObIArray<ObCmpFunc> *cmp_funcs_;
    int ret_code_;
  };
  int deduplicate_for_batch(bool has_last, const ObBatchRows *child_brs);
  bool first_got_row_; // whether it is the first time to get data
  common::ObArenaAllocator alloc_;
  LastCompactRow last_row_;
  ObBitVector *out_;
  Compare cmp_;
};

} // end sql
} // end oceanabse
#endif // OCEANBASE_SRC_SQL_ENGINE_AGGREGATE_OB_MERGE_DISTINCT_VEC_OP_H_
