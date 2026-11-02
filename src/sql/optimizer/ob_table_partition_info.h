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

#ifndef OCEANBASE_SQL_OB_TABLE_PARTITION_INFO_
#define OCEANBASE_SQL_OB_TABLE_PARTITION_INFO_

#include "sql/optimizer/table_access_targets.h"
#include "sql/optimizer/table_access_plan.h"

namespace oceanbase
{
namespace sql
{

class ObTablePartitionInfo
{
public:
  ObTablePartitionInfo() :
  inner_allocator_(common::ObModIds::OB_SQL_TABLE_ACCESS_PLAN),
  allocator_(inner_allocator_)
  {}
  ObTablePartitionInfo(common::ObIAllocator &allocator)
  : allocator_(allocator),
  table_access_plan_(allocator)
  {}
  virtual ~ObTablePartitionInfo() {}

  int assign(const ObTablePartitionInfo &other);

  int init_access_plan(ObSqlSchemaGuard &schema_guard,
                          const ObDMLStmt &stmt,
                          ObExecContext *exec_ctx,
                          const common::ObIArray<ObRawExpr*> &filter_exprs,
                          const uint64_t table_id,
                          const uint64_t ref_table_id,
                          //const uint64_t index_table_id,
                          const common::ObIArray<ObObjectID> *part_ids,
                          const common::ObDataTypeCastParams &dtc_params,
                          const bool is_dml_table,
                          common::ObIArray<ObRawExpr *> *sort_exprs = NULL);

  int get_not_insert_dml_part_sort_expr(const ObDMLStmt &stmt,
                                        common::ObIArray<ObRawExpr *> *sort_exprs) const;

  int calculate_access_targets(ObExecContext &exec_ctx,
                                        const ParamStore &params,
                                        const common::ObDataTypeCastParams &dtc_params);

  int replace_access_source(ObExecContext &exec_ctx, uint64_t ref_table_id, bool is_local_index);

  TableAccessShape get_access_shape() const;

  TableAccessPlan &get_table_access_plan()
  {
    return table_access_plan_;
  }

  const TableAccessPlan &get_table_access_plan() const
  {
    return table_access_plan_;
  }

  const TableAccessTargets &get_access_targets() const
  {
    return table_targets_;
  }
  TableAccessTargets &get_access_targets_for_update()
  {
    return table_targets_;
  }

  void set_table_access_plan(const TableAccessPlan &tbl)
  {
    table_access_plan_ = tbl;
  }

  uint64_t get_table_id() const { return table_access_plan_.get_table_id(); }
  uint64_t get_ref_table_id() const { return table_access_plan_.get_ref_table_id(); }
  share::schema::ObPartitionLevel get_part_level() const { return table_access_plan_.get_part_level(); }
  TO_STRING_KV(K_(table_access_plan),
               K_(table_targets));

private:
  DISALLOW_COPY_AND_ASSIGN(ObTablePartitionInfo);

private:
  common::ObArenaAllocator inner_allocator_;
  ObIAllocator &allocator_;
  TableAccessPlan table_access_plan_;
  TableAccessTargets table_targets_;
};

}
}
#endif // OCEANBASE_SQL_OB_TABLE_PARTITION_INFO_
