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

#ifndef OCEANBASE_SQL_RESOLVER_OB_PARTITIONED_STMT_H_
#define OCEANBASE_SQL_RESOLVER_OB_PARTITIONED_STMT_H_ 1
#include "share/ob_rpc_struct.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObPartitionedStmt : public ObDDLStmt
{
public:
  ObPartitionedStmt(common::ObIAllocator *name_pool, stmt::StmtType type)
      : ObDDLStmt(name_pool, type), interval_expr_(NULL),  use_def_sub_part_(true),
        use_auto_partition_clause_(false) {}
  explicit ObPartitionedStmt(stmt::StmtType type)
      : ObDDLStmt(type), interval_expr_(NULL), use_def_sub_part_(true),
        use_auto_partition_clause_(false) {}
  virtual ~ObPartitionedStmt() {}

  array_t &get_part_fun_exprs() { return part_fun_exprs_; }
  array_t &get_part_values_exprs() { return part_values_exprs_; }
  array_t &get_subpart_fun_exprs() { return subpart_fun_exprs_; }
  array_t &get_template_subpart_values_exprs() { return template_subpart_values_exprs_; }
  array_array_t &get_individual_subpart_values_exprs() { return individual_subpart_values_exprs_; }
  ObRawExpr *get_interval_expr() { return interval_expr_; }
  void set_interval_expr(ObRawExpr* interval_expr) { interval_expr_ = interval_expr; }
  bool use_def_sub_part() const { return use_def_sub_part_; }
  void set_use_def_sub_part(bool use_def_sub_part) { use_def_sub_part_ = use_def_sub_part; }
  bool use_auto_partition_clause() const { return use_auto_partition_clause_; }
  void set_use_auto_partition_clause(bool use_auto_partition_clause)
  {
    use_auto_partition_clause_ = use_auto_partition_clause;
  }



  TO_STRING_KV(K_(part_fun_exprs),
               K_(part_values_exprs),
               K_(subpart_fun_exprs),
               K_(template_subpart_values_exprs),
               K_(individual_subpart_values_exprs),
               K_(interval_expr),
               K_(use_def_sub_part),
               K_(use_auto_partition_clause));
private:
/**
 * The organization form of part_values_exprs is as follows:
 * range partition tiling, e.g.
 * partition by range(c1) (partition p0 values less than (100), partition p1 values less than (200))
 * array = [100, 200]
 * partition by range columns (c1,c2) (partition p0 values less than (100, 200), partition p1 values less than (300, 300))
 * array = [100, 200, 300, 400]
 * list partition spreads the values of each partition into one row, array stores rows, e.g.
 * partition by list(c1) (partition p0 values in (1,2,3,4,5), partition p1 values in (6,7,8,9,10))
 * array = [(1,2,3,4,5), (6,7,8,9,10)]
 * partition by list columns (c1,c2) (partition p0 values in ((1,1),(2,2),(3,3)), partition p1 values in ((6,6),(7,7),(8,8)))
 * array = [(1,1,2,2,3,3), (6,6,7,7,8,8)]
 */
  array_t part_fun_exprs_;       // for part fun expr
  array_t part_values_exprs_;   // for part values expr
  array_t subpart_fun_exprs_;    // for subpart fun expr
  array_t template_subpart_values_exprs_;    // for template subpart fun expr
  array_array_t individual_subpart_values_exprs_; //for individual subpart values expr
  ObRawExpr *interval_expr_;
  bool use_def_sub_part_; // control resolver behaviour when resolve composited-partitioned table/tablegroup
  bool use_auto_partition_clause_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObPartitionedStmt);
};

}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SQL_RESOLVER_OB_PARTITIONED_STMT_H_ */
