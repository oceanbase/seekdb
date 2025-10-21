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

#ifndef OCEANBASE_BASIC_OB_SET_OB_MERGE_SET_OP_H_
#define OCEANBASE_BASIC_OB_SET_OB_MERGE_SET_OP_H_

#include "sql/engine/set/ob_set_op.h"
#include "share/datum/ob_datum_funcs.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"

namespace oceanbase
{
namespace sql
{

class ObMergeSetSpec : public ObSetSpec
{
OB_UNIS_VERSION_V(1);
public:
  ObMergeSetSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);
};

/**
 * Here the CTE implementation will be separated, the CTE implementation uses another set of implementation, which is unrelated to Set, CTE should no longer inherit from ObMergeSetOp
 **/
class ObMergeSetOp : public ObOperator
{
public:
  ObMergeSetOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);

  virtual int inner_open() override;
  virtual int inner_close() override;
  virtual int inner_rescan() override;
  virtual void destroy() override;
  class Compare
  {
  public:
    Compare() : sort_collations_(nullptr), cmp_funcs_(nullptr), ret_code_(common::OB_SUCCESS)
    {}
    int init(const common::ObIArray<ObSortFieldCollation> *sort_collations,
      const common::ObIArray<common::ObCmpFunc> *cmp_funcs);
    int operator()(
      const common::ObIArray<ObExpr*> &l,
      const common::ObIArray<ObExpr*> &r,
      ObEvalCtx &eval_ctx,
      int &cmp);
    int operator()(
      const ObChunkDatumStore::StoredRow &l,
      const common::ObIArray<ObExpr*> &r,
      ObEvalCtx &eval_ctx,
      int &cmp);
    int operator() (const common::ObIArray<ObExpr*> &l,
                    const common::ObIArray<ObExpr*> &r,
                    const int64_t l_idx,
                    const int64_t r_idx,
                    ObEvalCtx &eval_ctx,
                    int &cmp);
    int operator() (const ObChunkDatumStore::StoredRow &l,
                    const common::ObIArray<ObExpr*> &r,
                    const int64_t r_idx,
                    ObEvalCtx &eval_ctx,
                    int &cmp);
    const common::ObIArray<ObSortFieldCollation> *sort_collations_;
    const common::ObIArray<common::ObCmpFunc> *cmp_funcs_;
    int ret_code_;
  };
protected:
  int do_strict_distinct(ObOperator &child_op,
    const common::ObIArray<ObExpr*> &compare_row,
    const common::ObIArray<ObExpr*> *&row,
    int &cmp);
  template<typename T>
  int do_strict_distinct(ObOperator &child_op, const T *compare_row,
    const common::ObIArray<ObExpr*> *&output_row);

  int convert_row(const common::ObIArray<ObExpr*> &src_exprs,
                  const common::ObIArray<ObExpr*> &dst_exprs,
                  const int64_t src_idx = 0,
                  const int64_t dst_idx = 0);
  int convert_batch(const common::ObIArray<ObExpr*> &src_exprs,
                    const common::ObIArray<ObExpr*> &dst_exprs,
                    ObBatchRows &brs,
                    bool is_union_all = false /* other cases can filter rows*/);
  bool get_need_skip_init_row() const { return need_skip_init_row_; }
  void set_need_skip_init_row(bool need_skip_init_row)
  { need_skip_init_row_ = need_skip_init_row; }
  //locate next valid left rows, do strict disitnct in it
  int locate_next_left_inside(ObOperator &child_op, const int64_t last_idx,
                              const ObBatchRows &row_brs, int64_t &curr_idx, bool &is_first);
  //locate next valid right rows, simply move to next, if a batch is end, get next batch
  int locate_next_right(ObOperator &child_op, const int64_t batch_size,
                        const ObBatchRows *&child_brs, int64_t &curr_idx);

protected:
  common::ObArenaAllocator alloc_;
  ObChunkDatumStore::LastStoredRow last_row_;
  Compare cmp_;
  bool need_skip_init_row_; // Whether to skip comparison with the initial last_output_row_; false: no; true: yes;
                            //Currently only for merge except and merge intersect set to TRUE, because cannot distinguish last_output_row_
                            // is from initialization with all NULL or left child with all NULL, see bug
  int64_t last_row_idx_;
  bool use_last_row_;
};
// Same as above, implicitly taking data from which child op, then the outer layer takes the output result from child_op
// Implement as a template function, convenient for comparing compare_row is StoredRow or ObIArray<ObExpr*>
template<typename T>
int ObMergeSetOp::do_strict_distinct(
  ObOperator &child_op,
  const T *compare_row,
  const common::ObIArray<ObExpr*> *&output_row)
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  bool is_break = false;
  while (OB_SUCC(ret) && !is_break) {
    if (OB_FAIL(child_op.get_next_row())) {
      if(OB_ITER_END != ret) {
        SQL_ENG_LOG(WARN, "failed to get next row", K(ret));
      }
    } else if (OB_UNLIKELY(get_need_skip_init_row())) {
      set_need_skip_init_row(false);
      is_break = true;
      // Save the first row, as the row for the next match, the previous logic should have been problematic, the reason it didn't cause a problem is probably that all rows were null
      if (OB_NOT_NULL(compare_row)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_ENG_LOG(WARN, "first row: compare row must be null", K(ret));
      } else if (OB_FAIL(last_row_.save_store_row(child_op.get_spec().output_, eval_ctx_, 0))) {
        SQL_ENG_LOG(WARN, "failed to save right row", K(ret));
      }
    } else if (OB_NOT_NULL(compare_row)) {
      if (OB_FAIL(cmp_(
          *compare_row, child_op.get_spec().output_, eval_ctx_, cmp_ret))) {
        SQL_ENG_LOG(WARN, "strict compare with last_row failed", K(ret), K(compare_row));
      } else if (0 != cmp_ret) {
        is_break = true;
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      SQL_ENG_LOG(WARN, "unexpected status", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    output_row = &child_op.get_spec().output_;
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_BASIC_OB_SET_OB_MERGE_SET_OP_H_
