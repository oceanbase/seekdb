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

#ifndef OCEANBASE_OBSERVER_OB_TABLE_INSERT_EXECUTOR_H
#define OCEANBASE_OBSERVER_OB_TABLE_INSERT_EXECUTOR_H
#include "ob_table_modify_executor.h"
#include "ob_table_context.h"

namespace oceanbase
{
namespace table
{

class ObTableApiInsertSpec : public ObTableApiModifySpec
{
public:
  typedef common::ObArrayWrap<ObTableInsCtDef*> ObTableInsCtDefArray;
  ObTableApiInsertSpec(common::ObIAllocator &alloc, const ObTableExecutorType type)
      : ObTableApiModifySpec(alloc, type),
        ins_ctdefs_()
  {
  }

  int init_ctdefs_array(int64_t size);
  virtual ~ObTableApiInsertSpec();
public:
  OB_INLINE const ObTableInsCtDefArray& get_ctdefs() const { return ins_ctdefs_; }
  OB_INLINE ObTableInsCtDefArray& get_ctdefs() { return ins_ctdefs_; }
private:
  ObTableInsCtDefArray ins_ctdefs_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTableApiInsertSpec);
};

class ObTableApiInsertExecutor : public ObTableApiModifyExecutor
{
public:
  typedef common::ObArrayWrap<ObTableInsRtDef> ObTableInsRtDefArray;
  ObTableApiInsertExecutor(ObTableCtx &ctx, const ObTableApiInsertSpec &spec)
      : ObTableApiModifyExecutor(ctx),
        ins_spec_(spec),
        ins_rtdefs_(),
        cur_idx_(0)
  {
  }

public:
  virtual int open() override;
  virtual int get_next_row() override;
  virtual int close() override;
private:
  int inner_open_with_das();
  int process_single_operation(const ObTableEntity *entity);
  int get_next_row_from_child();
  int ins_rows_post_proc();
  int insert_row_to_das();

private:
  const ObTableApiInsertSpec &ins_spec_;
  ObTableInsRtDefArray ins_rtdefs_;
  int64_t cur_idx_;
};

}  // namespace table
}  // namespace oceanbase

#endif /* OCEANBASE_OBSERVER_OB_TABLE_INSERT_EXECUTOR_H */
