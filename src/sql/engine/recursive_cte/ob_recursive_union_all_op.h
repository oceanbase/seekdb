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

#ifndef OCEANBASE_SQL_OB_RECURSIVE_UNION_ALL_OP_H_
#define OCEANBASE_SQL_OB_RECURSIVE_UNION_ALL_OP_H_

#include "sql/engine/set/ob_merge_set_op.h"
#include "sql/engine/ob_exec_context.h"
#include "lib/allocator/ob_malloc.h"
#include "ob_fake_cte_table_op.h"
#include "ob_recursive_inner_data_op.h"

namespace oceanbase
{
namespace sql
{

// Physical configuration shared by the current recursive CTE pump and the
// bounded graph feedback loop.  This class describes the mechanics needed to
// feed rows back through the right child; it does not give either concrete
// operator the other's SQL semantics.
class RecursivePumpSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  explicit RecursivePumpSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);
  ~RecursivePumpSpec() = default;
  friend class RecursivePumpOp;
  inline void set_search_strategy(ObRecursiveInnerDataOp::SearchStrategyType strategy)
  {
    strategy_ = strategy;
  }
  inline void set_fake_cte_table(uint64_t cte_table_id) { pump_operator_id_ = cte_table_id; };

protected:
  /**
   * @brief for specified phy operator to print it's member variable with json key-value format
   * @param buf[in] to string buffer
   * @param buf_len[in] buffer length
   * @return if success, return the length used by print string, otherwise return 0
   */
  //virtual int64_t to_string_kv(char *buf, const int64_t buf_len) const;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(RecursivePumpSpec);
public:
  //recursive union all operator's output T_OP_UNION expression, inner data get output rows after copying to these expressions' datum
  //inner data's output line never contains search, cycle pseudo column, so here it also does not contain the pseudo list expression.
  common::ObFixedArray<ObExpr *, common::ObIAllocator> output_union_exprs_;
protected:
  uint64_t pump_operator_id_{OB_INVALID_ID};
  ObRecursiveInnerDataOp::SearchStrategyType strategy_{
      ObRecursiveInnerDataOp::SearchStrategyType::BREADTH_FRIST};
};

class ObRecursiveUnionAllSpec : public RecursivePumpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  explicit ObRecursiveUnionAllSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);
  ~ObRecursiveUnionAllSpec() = default;
  static const int64_t UNUSED_POS;

protected:
  static const int32_t CMP_DIRECTION_ASC = 1;
  static const int32_t CMP_DIRECTION_DESC = -1;
  DISALLOW_COPY_AND_ASSIGN(ObRecursiveUnionAllSpec);
};

// Runtime helper for the two concrete feedback operators.  Keeping the pump in
// a semantic-neutral base lets GraphFeedbackLoop replace this CTE-oriented
// implementation incrementally without inheriting ObRecursiveUnionAllOp.
class RecursivePumpOp : public ObOperator
{
public:
  explicit RecursivePumpOp(ObExecContext &exec_ctx,
                           const ObOpSpec &spec,
                           ObOpInput *input)
      : ObOperator(exec_ctx, spec, input),
        inner_data_(get_eval_ctx(),
                    exec_ctx,
                    get_pump_spec().get_left()->output_,
                    get_pump_spec().output_union_exprs_)
  {
  }
  ~RecursivePumpOp() = default;
  virtual int inner_open() override;
  virtual int inner_close() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_batch(const int64_t max_row_cnt) override;
  virtual int inner_rescan() override;
  virtual void destroy()
  {
    inner_data_.~ObRecursiveInnerDataOp();
    ObOperator::destroy();
  }
  void set_search_strategy(ObRecursiveInnerDataOp::SearchStrategyType strategy) {
    inner_data_.set_search_strategy(strategy);
  }
  const RecursivePumpSpec &get_pump_spec() const
  { return static_cast<const RecursivePumpSpec &>(spec_); }
public:
  ObRecursiveInnerDataOp inner_data_;
};

class ObRecursiveUnionAllOp : public RecursivePumpOp
{
public:
  explicit ObRecursiveUnionAllOp(ObExecContext &exec_ctx,
                                 const ObOpSpec &spec,
                                 ObOpInput *input)
      : RecursivePumpOp(exec_ctx, spec, input)
  {
  }
  ~ObRecursiveUnionAllOp() = default;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_OB_SET_OPERATOR_H_ */
