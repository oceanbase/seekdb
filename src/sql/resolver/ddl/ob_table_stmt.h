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

#ifndef OCEANBASE_SQL_RESOLVER_OB_TABLE_STMT_H_
#define OCEANBASE_SQL_RESOLVER_OB_TABLE_STMT_H_ 1
#include "share/ob_rpc_struct.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"
#include "sql/resolver/ddl/ob_partitioned_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObPartitionResolveResult
{
  const static int OB_DEFAULT_ARRAY_SIZE = 16;
  typedef common::ObSEArray<ObRawExpr *,
                            OB_DEFAULT_ARRAY_SIZE,
                            common::ModulePageAllocator,
                            true> array_t;
  typedef common::ObSEArray<array_t,
                            OB_DEFAULT_ARRAY_SIZE,
                            common::ModulePageAllocator,
                            true> array_array_t;
public:
  ObPartitionResolveResult() {}
  ~ObPartitionResolveResult() {}
  array_t &get_part_fun_exprs() { return part_fun_exprs_; }
  array_t &get_part_values_exprs() { return part_values_exprs_; }
  array_t &get_subpart_fun_exprs() { return subpart_fun_exprs_; }
  array_t &get_template_subpart_values_exprs() { return template_subpart_values_exprs_; }
  array_array_t &get_individual_subpart_values_exprs() { return individual_subpart_values_exprs_; }
  TO_STRING_KV(K_(part_fun_exprs),
               K_(part_values_exprs),
               K_(subpart_fun_exprs),
               K_(template_subpart_values_exprs),
               K_(individual_subpart_values_exprs));
private:
  array_t part_fun_exprs_;       // for part fun expr
  array_t part_values_exprs_;   // for part values expr
  array_t subpart_fun_exprs_;    // for subpart fun expr
  array_t template_subpart_values_exprs_;    // for template subpart fun expr
  array_array_t individual_subpart_values_exprs_; //for individual subpart values expr
};

// AI column definition carried from the resolver to the DDL executor.  The
// executor uses it after the table schema has been committed to persist the
// metadata row in __all_ai_column.
struct ObAiColumnDef
{
public:
  ObAiColumnDef() : max_retries_(0), result_type_(0), ai_func_type_(0), ai_seq_(0),
                    result_column_id_(common::OB_INVALID_ID),
                    state_column_id_(common::OB_INVALID_ID),
                    request_column_id_(common::OB_INVALID_ID),
                    result_spec_column_id_(common::OB_INVALID_ID) {}
  void reset()
  {
    column_name_.reset();
    normalized_expr_.reset();
    sync_mode_.reset();
    database_name_.reset();
    table_name_.reset();
    state_column_name_.reset();
    request_column_name_.reset();
    result_spec_column_name_.reset();
    dep_column_names_.reset();
    max_retries_ = 0;
    result_type_ = 0;
    ai_func_type_ = 0;
    ai_seq_ = 0;
    result_column_id_ = common::OB_INVALID_ID;
    state_column_id_ = common::OB_INVALID_ID;
    request_column_id_ = common::OB_INVALID_ID;
    result_spec_column_id_ = common::OB_INVALID_ID;
  }
  TO_STRING_KV(K_(database_name), K_(table_name), K_(column_name),
               K_(state_column_name), K_(request_column_name),
               K_(result_spec_column_name), K_(max_retries),
               K_(sync_mode), K_(result_type), K_(ai_func_type), K_(ai_seq),
               K_(normalized_expr), K_(dep_column_names), K_(result_column_id),
               K_(state_column_id), K_(request_column_id), K_(result_spec_column_id));
  common::ObString database_name_;
  common::ObString table_name_;
  common::ObString column_name_;
  common::ObString state_column_name_;
  common::ObString request_column_name_;
  common::ObString result_spec_column_name_;
  common::ObString sync_mode_;
  common::ObString normalized_expr_;
  common::ObString dep_column_names_;
  int64_t max_retries_;
  int64_t result_type_;
  int64_t ai_func_type_;
  int64_t ai_seq_;
  uint64_t result_column_id_;
  uint64_t state_column_id_;
  uint64_t request_column_id_;
  uint64_t result_spec_column_id_;
};

class ObTableStmt : public ObPartitionedStmt
{
  const static int OB_DEFAULT_ARRAY_SIZE = 16;
public:

  ObTableStmt(common::ObIAllocator *name_pool, stmt::StmtType type)
      : ObPartitionedStmt(name_pool, type),
        part_type_(share::schema::PARTITION_FUNC_TYPE_MAX)
  {
  }
  explicit ObTableStmt(stmt::StmtType type)
      : ObPartitionedStmt(type),
        part_type_(share::schema::PARTITION_FUNC_TYPE_MAX)
  {
  }
  virtual ~ObTableStmt() {}
  common::ObSArray<ObAiColumnDef> &get_ai_column_defs() { return ai_column_defs_; }
  const common::ObSArray<ObAiColumnDef> &get_ai_column_defs() const { return ai_column_defs_; }
  int add_ai_column_def(const ObAiColumnDef &def) { return ai_column_defs_.push_back(def); }

  share::schema::ObPartitionFuncType get_part_type() const
  {
    return part_type_;
  }
  common::ObSArray<ObPartitionResolveResult> &get_index_partition_resolve_results()
  {
    return index_partition_resolve_results_;
  }
  TO_STRING_KV(K_(part_type));
private:
  common::ObSArray<ObPartitionResolveResult> index_partition_resolve_results_;
  common::ObSArray<ObAiColumnDef> ai_column_defs_;
  share::schema::ObPartitionFuncType part_type_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTableStmt);
};
}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SQL_RESOLVER_OB_TABLE_STMT_H_ */
