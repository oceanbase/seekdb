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

#ifndef OCEANBASE_SQL_ENGINE_JOIN_OB_BASIC_NESTED_LOOP_JOIN_OP_
#define OCEANBASE_SQL_ENGINE_JOIN_OB_BASIC_NESTED_LOOP_JOIN_OP_
#include "ob_join_op.h"
namespace oceanbase
{
namespace sql
{
class ObBasicNestedLoopJoinSpec: public ObJoinSpec
{
  OB_UNIS_VERSION_V(1);
public:
 ObBasicNestedLoopJoinSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
   : ObJoinSpec(alloc, type),
     rescan_params_(alloc),
     gi_partition_id_expr_(nullptr),
     enable_gi_partition_pruning_(false),
     enable_px_batch_rescan_(false)
  {}
  virtual ~ObBasicNestedLoopJoinSpec() {};

  int init_param_count(int64_t count)
  { return rescan_params_.init(count); }

  int add_nlj_param(int64_t param_idx, ObExpr *org_expr, ObExpr *param_expr);

public:
  common::ObFixedArray<ObDynamicParamSetter, common::ObIAllocator> rescan_params_;
  // Indicates the position of the partition id column in the output rows, read part id through expr, used for right pruning
  ObExpr *gi_partition_id_expr_;
  bool enable_gi_partition_pruning_;
  bool enable_px_batch_rescan_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObBasicNestedLoopJoinSpec);
};

class ObBasicNestedLoopJoinOp: public ObJoinOp
{
public:
  static const int64_t DEFAULT_MEM_LIMIT = 10 * 1024 * 1024;
  static const int64_t DEFAULT_CACHE_LIMIT = 1000;
  ObBasicNestedLoopJoinOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);
  virtual ~ObBasicNestedLoopJoinOp() {};

  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int inner_close() final;

  virtual OperatorOpenOrder get_operator_open_order() const override final
  { return OPEN_SELF_FIRST; }

  int prepare_rescan_params(bool is_group = false);
  virtual void destroy() override { ObJoinOp::destroy(); }
  void set_param_null();

  const ObBasicNestedLoopJoinSpec &get_spec() const
  { return static_cast<const ObBasicNestedLoopJoinSpec &>(spec_); }

  virtual int get_next_left_row() override;

  int save_left_row();
  int recover_left_row();
private:
  DISALLOW_COPY_AND_ASSIGN(ObBasicNestedLoopJoinOp);
};

inline int ObBasicNestedLoopJoinSpec::add_nlj_param(int64_t param_idx,
                                                    ObExpr *org_expr,
                                                    ObExpr *param_expr)
{
  return rescan_params_.push_back(ObDynamicParamSetter(param_idx, org_expr, param_expr));
}

} // end namespace sql
} // end namespace oceanbase
#endif
