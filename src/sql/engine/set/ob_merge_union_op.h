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

#ifndef OCEANBASE_BASIC_OB_SET_OB_MERGE_UNION_OP_H_
#define OCEANBASE_BASIC_OB_SET_OB_MERGE_UNION_OP_H_

#include "sql/engine/set/ob_merge_set_op.h"

namespace oceanbase
{
namespace sql
{

class ObMergeUnionSpec : public ObMergeSetSpec
{
OB_UNIS_VERSION_V(1);
public:
  ObMergeUnionSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);
};

class ObMergeUnionOp : public ObMergeSetOp
{
public:
  ObMergeUnionOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);

  virtual int inner_open() override;
  virtual int inner_close() override;
  virtual int inner_rescan() override;
  virtual void destroy() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_batch(const int64_t max_row_cnt);

private:
  int get_first_row(const ObIArray<ObExpr*> *&output_row);
  int get_first_row_vectorize(const int64_t batch_size);
  int distinct_get_next_row();
  int distinct_get_next_batch(const int64_t batch_size);
  int all_get_next_row();
  int all_get_next_batch(const int64_t batch_size);
  void switch_curr_and_candidate();
private:
  struct UnionOpInfo {
    UnionOpInfo () : op_idx_(0), op_brs_(nullptr), op_added_(false) {}
    void reset() 
    { 
      op_idx_ = 0;
      op_brs_ = nullptr;
      op_added_ = false;
    }
    int64_t op_idx_;
    const ObBatchRows *op_brs_;
    bool op_added_;
  };
  typedef int (ObMergeUnionOp::*GetNextRowFunc)();
  typedef int (ObMergeUnionOp::*GetNextBatchFunc)(const int64_t batch_size);
  int do_strict_distinct_vectorize(ObOperator &child_op,
                                   const ObChunkDatumStore::StoredRow *compare_row,
                                   const common::ObIArray<ObExpr*> &compare_expr,
                                   const int64_t batch_size,
                                   const int64_t compare_idx,
                                   UnionOpInfo &curr_info,
                                   int &cmp,
                                   bool &row_return);
  ObOperator *cur_child_op_;
  ObOperator *candidate_child_op_;
  const ObIArray<ObExpr*> *candidate_output_row_;
  int64_t next_child_op_idx_;
  bool first_got_row_;
  GetNextRowFunc get_next_row_func_;
  GetNextBatchFunc get_next_batch_func_;
  UnionOpInfo left_info_;
  UnionOpInfo right_info_;
  UnionOpInfo *curr_info_;
  UnionOpInfo *candidate_info_;
  //idx indicates a row need to store as last_row, if we store a row, reset last_valid_idx_ to -1
  //if we record a idx, reset last_row.store_row_ to nullptr
  int64_t last_valid_idx_;
  //we will not push candidate op to next row until curr equal to candidate
  //in batch mode, if candidate move to last in a batch, we may have to return batch firstly,
  //then at the start of next batch, 
  //we decide if need get a new batch from candidate by this parameter
  bool need_push_candidate_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_BASIC_OB_SET_OB_MERGE_UNION_OP_H_ */
