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

#ifndef OCEANBASE_SRC_SQL_ENGINE_BASIC_OB_TEMP_TABLE_ACCESS_OP_H_
#define OCEANBASE_SRC_SQL_ENGINE_BASIC_OB_TEMP_TABLE_ACCESS_OP_H_

#include "sql/engine/ob_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/basic/ob_ra_row_store.h"
#include "sql/engine/basic/ob_chunk_row_store.h"
#include "sql/engine/ob_sql_mem_mgr_processor.h"
#include "sql/engine/ob_tenant_sql_memory_manager.h"
#include "sql/dtl/ob_dtl_interm_result_manager.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/dtl/ob_dtl_interm_result_manager.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObTempTableAccessOp;
class ObTempTableAccessOpInput : public ObOpInput
{
  OB_UNIS_VERSION_V(1);
  friend class ObTempTableAccessOp;
public:
  ObTempTableAccessOpInput(ObExecContext &ctx, const ObOpSpec &spec);
  virtual ~ObTempTableAccessOpInput();
  virtual void reset() override;
  virtual ObPhyOperatorType get_phy_op_type() const;
  virtual void set_deserialize_allocator(common::ObIAllocator *allocator);
  virtual int init(ObTaskInfo &task_info);
  int check_finish(bool &is_end, int64_t &interm_res_ids);
protected:
  common::ObIAllocator *deserialize_allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObTempTableAccessOpInput);
public:
  uint64_t unfinished_count_ptr_;
  common::ObSEArray<uint64_t, 8> interm_result_ids_;
};

class ObTempTableAccessOpSpec : public ObOpSpec
{
public:
  OB_UNIS_VERSION_V(1);
public:
  ObTempTableAccessOpSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type),
      output_indexs_(alloc),
      temp_table_id_(0),
      is_distributed_(false),
      access_exprs_(alloc) {}
  virtual ~ObTempTableAccessOpSpec() {}
  void set_temp_table_id(uint64_t temp_table_id) { temp_table_id_ = temp_table_id; }
  uint64_t get_table_id() const { return temp_table_id_; }
  void set_distributed(bool is_distributed) { is_distributed_ = is_distributed; }
  bool is_distributed() const { return is_distributed_; }
  int add_output_index(int64_t index) { return output_indexs_.push_back(index); }
  int init_output_index(int64_t count) { return output_indexs_.init(count); }
  int add_access_expr(ObExpr *access_expr) { return access_exprs_.push_back(access_expr); }
  int init_access_exprs(int64_t count) { return access_exprs_.init(count); }

  DECLARE_VIRTUAL_TO_STRING;
public:
  common::ObFixedArray<int64_t, common::ObIAllocator> output_indexs_;
  uint64_t temp_table_id_;
  bool is_distributed_;
  // Operator access exprs expressions
  ExprFixedArray access_exprs_;
};

class ObTempTableAccessOp : public ObOperator
{
public:
  ObTempTableAccessOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObOperator(exec_ctx, spec, input),
      datum_store_it_(),
      interm_result_ids_(),
      cur_idx_(0),
      can_rescan_(false),
      is_started_(false),
      stored_rows_(NULL),
      result_info_guard_() {}
  ~ObTempTableAccessOp() {}

  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_batch(const int64_t max_row_cnt) override;
  virtual int inner_close() override;
  virtual void destroy() override;
  int locate_next_interm_result(bool &is_end);
  int locate_interm_result(int64_t result_id);
  int get_local_interm_result_id(int64_t &result_id);

private:
  ObChunkDatumStore::Iterator datum_store_it_;
  // Here the result id is the current operator's available task (for rescan) or tasks that have been completed or are being completed
  // TempTableAccess's rescan will not reacquire tasks from the task pool, but instead choose to re-execute the previously acquired tasks
  common::ObSEArray<uint64_t, 8> interm_result_ids_;
  uint64_t cur_idx_;
  bool can_rescan_;
  // If it is local result, it can only be read once
  bool is_started_;
  const ObChunkDatumStore::StoredRow **stored_rows_;
  dtl::ObDTLIntermResultInfoGuard result_info_guard_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SRC_SQL_ENGINE_BASIC_OB_TEMP_TABLE_ACCESS_OP_H_ */
