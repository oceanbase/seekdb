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

#ifndef OCEANBASE_BASIC_OB_SCALAR_GROUPBY_OP_H_
#define OCEANBASE_BASIC_OB_SCALAR_GROUPBY_OP_H_

#include "common/row/ob_row_store.h"
#include "sql/engine/aggregate/ob_groupby_op.h"
#include "sql/engine/basic/ob_hp_infrastructure_manager.h"

namespace oceanbase
{
namespace sql
{
class ObScalarAggregateSpec : public ObGroupBySpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObScalarAggregateSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObGroupBySpec(alloc, type), enable_hash_base_distinct_(false)
  {}

public:
  bool enable_hash_base_distinct_;

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObScalarAggregateSpec);
};

class ObScalarAggregateOp : public ObGroupByOp
{
public:
  friend ObAggregateProcessor;
  ObScalarAggregateOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObGroupByOp(exec_ctx, spec, input), started_(false), dir_id_(-1),
      profile_(ObSqlWorkAreaType::HASH_WORK_AREA),
      sql_mem_processor_(profile_, op_monitor_info_),
      hp_infras_mgr_(MTL_ID())
  {
  }

  virtual int inner_open() override;
  virtual int inner_close() override;
  virtual int inner_rescan() override;
  virtual int inner_switch_iterator() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_batch(const int64_t max_row_cnt) override;
  virtual void destroy() override;
  // reset default value of %cur_rownum_ && %rownum_limit_
private:
  int init_hp_infras_group_mgr();
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObScalarAggregateOp);

private:
  bool started_;
  int64_t dir_id_;
  ObSqlWorkAreaProfile profile_;
  ObSqlMemMgrProcessor sql_mem_processor_;
  HashPartInfrasMgr hp_infras_mgr_;
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_BASIC_OB_SCALAR_GROUPBY_OP_H_
