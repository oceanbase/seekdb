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

#ifndef _OB_SCALAR_AGGREGATE_OP_H_
#define _OB_SCALAR_AGGREGATE_OP_H_

#include "src/sql/engine/aggregate/ob_groupby_vec_op.h"
#include "src/sql/engine/aggregate/ob_scalar_aggregate_op.h"
#include "lib/rc/context.h"

namespace oceanbase
{
namespace sql
{

class ObScalarAggregateVecSpec: public ObScalarAggregateSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObScalarAggregateVecSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type) :
    ObScalarAggregateSpec(alloc, type), can_return_empty_set_(false)
  {}
  void set_cant_return_empty_set()
  {
    can_return_empty_set_ = true;
  }

  bool can_return_empty_set() const
  {
    return can_return_empty_set_;
  }

private:
  bool can_return_empty_set_;
};

class ObScalarAggregateVecOp: public ObGroupByVecOp
{
public:
  ObScalarAggregateVecOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input) :
    ObGroupByVecOp(exec_ctx, spec, input), started_(false), dir_id_(-1), row_(nullptr),
    row_meta_(&exec_ctx.get_allocator()), profile_(ObSqlWorkAreaType::HASH_WORK_AREA),
    sql_mem_processor_(profile_, op_monitor_info_), hp_infras_mgr_(MTL_ID()), mem_context_(nullptr)
  {}

  virtual int inner_open() override;
  virtual int inner_close() override;
  virtual int inner_rescan() override;
  virtual int inner_switch_iterator() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_batch(const int64_t max_row_cnt) override;
  virtual void destroy() override;
private:
  int add_batch_rows_for_3stage(const ObBatchRows &brs, aggregate::AggrRowPtr row);
  DISALLOW_COPY_AND_ASSIGN(ObScalarAggregateVecOp);
  int init_mem_context();
  int init_one_aggregate_row();
  int init_hp_infras_group_mgr();

private:
  bool started_;
  int64_t dir_id_;
  ObCompactRow *row_;
  RowMeta row_meta_;
  ObSqlWorkAreaProfile profile_;
  ObSqlMemMgrProcessor sql_mem_processor_;
  ObHashPartInfrasVecMgr hp_infras_mgr_;
  lib::MemoryContext mem_context_;
};
} // end sql
} // end namespace
#endif // _OB_SCALAR_AGGREGATE_OP_H_
