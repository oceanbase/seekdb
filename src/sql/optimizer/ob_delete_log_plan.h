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

#ifndef _OB_DELETE_LOG_PLAN_H
#define _OB_DELETE_LOG_PLAN_H
#include "sql/resolver/dml/ob_delete_stmt.h"
#include "sql/optimizer/ob_del_upd_log_plan.h"

namespace oceanbase
{
namespace sql
{
/**
 *  Logical Plan for 'delete' statement
 */
class ObDeleteLogPlan : public ObDelUpdLogPlan
{
public:
  ObDeleteLogPlan(ObOptimizerContext &ctx, const ObDeleteStmt *delete_stmt)
    : ObDelUpdLogPlan(ctx, delete_stmt)
  {}
  virtual ~ObDeleteLogPlan() {}
  const ObDeleteStmt *get_stmt() const override
  { return reinterpret_cast<const ObDeleteStmt*>(stmt_); }

protected:
  virtual int generate_normal_raw_plan() override;

private:
  DISALLOW_COPY_AND_ASSIGN(ObDeleteLogPlan);
  // Allocate delete log operator for normal delete plan
  int candi_allocate_delete();
  int create_delete_plan(ObLogicalOperator *&top);
  int create_delete_plans(ObIArray<CandidatePlan> &candi_plans,
                          const bool force_no_multi_part,
                          const bool force_multi_part,
                          ObIArray<CandidatePlan> &delete_plans);
  int allocate_delete_as_top(ObLogicalOperator *&top, bool is_multi_part_dml);
  // Allocate delete log operator in pdml delete plan
  int candi_allocate_pdml_delete();
  virtual int prepare_dml_infos() override;
  virtual int prepare_table_dml_info_special(const ObDmlTableInfo& table_info,
                                             IndexDMLInfo* table_dml_info,
                                             ObIArray<IndexDMLInfo*> &index_dml_infos,
                                             ObIArray<IndexDMLInfo*> &all_index_dml_infos) override;
};
}
}
#endif
