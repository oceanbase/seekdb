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

#define USING_LOG_PREFIX SQL_RESV
#include "ob_sql_context.h"

#include "sql/optimizer/ob_log_plan.h"
#include "sql/optimizer/ob_table_partition_info.h"
#include "share/schema/ob_schema_getter_guard.h"

using namespace ::oceanbase::common;
namespace oceanbase
{
using namespace share::schema;
namespace sql
{

bool LocationConstraint::operator==(const LocationConstraint &other) const {
  return key_ == other.key_ && phy_loc_type_ == other.phy_loc_type_ && constraint_flags_ == other.constraint_flags_ ;
}



int ObLocationConstraintContext::calc_constraints_inclusion(const ObPwjConstraint *left,
                                                            const ObPwjConstraint *right,
                                                            InclusionType &inclusion_result)
{
  int ret = OB_SUCCESS;
  inclusion_result = NotSubset;
  if (OB_ISNULL(left) || OB_ISNULL(right)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(left), K(right));
  } else {
    const ObPwjConstraint *set1 = NULL, *set2 = NULL;
    bool is_subset = true;
    // insure set1.count() >= set2.count()
    if (left->count() >= right->count()) {
      inclusion_result = LeftIsSuperior;
      set1 = left;
      set2 = right;
    } else {
      inclusion_result = RightIsSuperior;
      set1 = right;
      set2 = left;
    }

    for (int64_t i = 0; is_subset && i < set2->count(); i++) {
      bool detected = false;
      for (int64_t j = 0; !detected && j < set1->count(); j++) {
        if (set2->at(i) == set1->at(j)) {
          detected = true;
        }
      }
      // if the element is not in set1, set1 can not contain all the elements in set2
      if (!detected) {
        is_subset = false;
      }
    }
    if (!is_subset) {
      inclusion_result = NotSubset;
    }
  }

  return ret;
}

int ObQueryRetryInfo::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("init twice", K(ret));
  } else {
    inited_ = true;
  }
  return ret;
}

void ObQueryRetryInfo::reset()
{
  inited_ = false;
  is_rpc_timeout_ = false;
  last_query_retry_err_ = OB_SUCCESS;
  retry_cnt_ = 0;
}

void ObQueryRetryInfo::clear()
{
  // Here cannot set inited_ to false
  is_rpc_timeout_ = false;
  //last_query_retry_err_ = OB_SUCCESS;
}

void ObQueryRetryInfo::set_is_rpc_timeout(bool is_rpc_timeout)
{
  is_rpc_timeout_ = is_rpc_timeout;
}

bool ObQueryRetryInfo::is_rpc_timeout() const
{
  return is_rpc_timeout_;
}

ObSqlCtx::ObSqlCtx()
  : session_info_(NULL),
    schema_guard_(NULL),
    secondary_namespace_(NULL),
    plan_cache_hit_(false),
    self_add_plan_(false),
    disable_privilege_check_(PRIV_CHECK_FLAG_NORMAL),
    force_print_trace_(false),
    is_show_trace_stmt_(false),
    retry_times_(OB_INVALID_COUNT),
    exec_type_(InvalidType),
    is_prepare_protocol_(false),
    is_mock_prepare_(false),
    is_prepare_stage_(false),
    is_dynamic_sql_(false),
    is_cursor_(false),
    statement_id_(common::OB_INVALID_ID),
    stmt_type_(stmt::T_NONE),
    partition_infos_(NULL),
    partition_infos_allocator_(NULL),
    is_restore_(false),
    all_plan_const_param_constraints_(nullptr),
    all_possible_const_param_constraints_(nullptr),
    all_equal_param_constraints_(nullptr),
    all_pre_calc_constraints_(nullptr),
    all_expr_constraints_(nullptr),
    all_priv_constraints_(nullptr),
    need_match_all_params_(false),
    all_local_session_vars_(nullptr),
    is_ddl_from_primary_(false),
    cur_stmt_(NULL),
    cur_plan_(nullptr),
    is_sensitive_(false),
    snapshot_query_expr_(nullptr),
    is_execute_call_stmt_(false),
    is_text_ps_mode_(false),
    first_plan_hash_(0),
    ins_opt_ctx_(),
    flags_(0)
{
  sql_id_[0] = '\0';
  sql_id_[common::OB_MAX_SQL_ID_LENGTH] = '\0';
  format_sql_id_[0] = '\0';
  format_sql_id_[common::OB_MAX_SQL_ID_LENGTH] = '\0';
}

void ObSqlCtx::reset()
{
  multi_stmt_item_.reset();
  session_info_ = NULL;
  schema_guard_ = NULL;
  plan_cache_hit_ = false;
  self_add_plan_ = false;
  disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
  force_print_trace_ = false;
  is_show_trace_stmt_ = false;
  retry_times_ = OB_INVALID_COUNT;
  sql_id_[0] = '\0';
  sql_id_[common::OB_MAX_SQL_ID_LENGTH] = '\0';
  format_sql_id_[0] = '\0';
  format_sql_id_[common::OB_MAX_SQL_ID_LENGTH] = '\0';
  exec_type_ = InvalidType;
  is_prepare_protocol_ = false;
  is_mock_prepare_ = false;
  is_prepare_stage_ = false;
  is_dynamic_sql_ = false;
  is_restore_ = false;
  all_plan_const_param_constraints_ = nullptr;
  all_possible_const_param_constraints_ = nullptr;
  all_equal_param_constraints_ = nullptr;
  all_pre_calc_constraints_ = nullptr;
  all_expr_constraints_ = nullptr;
  all_priv_constraints_ = nullptr;
  need_match_all_params_ = false;
  all_local_session_vars_ = nullptr;
  is_ddl_from_primary_ = false;
  is_sensitive_ = false;
  first_plan_hash_ = 0;
  first_outline_data_.reset();
  first_equal_param_cons_cnt_ = 0;
  first_const_param_cons_cnt_ = 0;
  first_expr_cons_cnt_ = 0;
  clear();
  snapshot_query_expr_ = nullptr;
  stmt_type_ = stmt::T_NONE;
  cur_plan_ = nullptr;
  is_execute_call_stmt_ = false;
  is_text_ps_mode_ = false;
  enable_strict_defensive_check_ = false;
  ins_opt_ctx_.reset();
  reconstruct_ps_sql_.reset();
}

//release dynamic allocated memory
void ObSqlCtx::clear()
{
  if (OB_NOT_NULL(partition_infos_)) {
    typedef common::ObFixedArray<ObTablePartitionInfo *, common::ObIAllocator>
        PartitionInfoStorage;
    PartitionInfoStorage *storage = static_cast<PartitionInfoStorage *>(partition_infos_);
    storage->~PartitionInfoStorage();
    partition_infos_allocator_->free(storage);
    partition_infos_ = NULL;
    partition_infos_allocator_ = NULL;
  }
  related_user_var_names_.reset();
  base_constraints_.reset();
  strict_constraints_.reset();
  non_strict_constraints_.reset();
  multi_stmt_rowkey_pos_.reset();
  plan_key_.reset();
  cur_stmt_ = nullptr;
  is_text_ps_mode_ = false;
  ins_opt_ctx_.clear();
  cur_plan_ = nullptr;
}

OB_SERIALIZE_MEMBER(ObSqlCtx, stmt_type_);

void ObSqlSchemaGuard::reset()
{
  schema_guard_ = NULL;
}

int ObSqlSchemaGuard::get_table_schema(uint64_t table_id,
                                      uint64_t ref_table_id,
                                      const ObDMLStmt *stmt,
                                      const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_INVALID_ARGUMENT;;
    LOG_WARN("get unexpected null", K(ret), K(stmt));
  } else if (OB_FAIL(get_table_schema(ref_table_id, table_schema))) {
    LOG_WARN("failed to get table schema", K(table_id), K(ref_table_id), K(ret));
  }
  return ret;
}

int ObSqlSchemaGuard::get_table_schema(uint64_t table_id,
                                      const TableItem *table_item,
                                      const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_item) ) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get unexpected null", K(ret), K(table_item));
  } else if (OB_FAIL(get_table_schema(table_id, table_schema))) {
    LOG_WARN("failed to get table schema", K(table_id), K(ret));
  }
  return ret;
}

int ObSqlSchemaGuard::get_table_schema(uint64_t table_id,
                                         const ObTableSchema *&table_schema,
                                         bool is_link /* = false*/) const
{
  int ret = OB_SUCCESS;
  OV (OB_NOT_NULL(schema_guard_));
  OZ (schema_guard_->get_table_schema( table_id, table_schema), table_id, is_link);
  return ret;
}

int ObSqlSchemaGuard::get_table_schema(
                                      const uint64_t table_id,
                                      const share::schema::ObTableSchema *&table_schema,
                                      bool is_link /* = false*/)
{
  int ret = OB_SUCCESS;
  OV (OB_NOT_NULL(schema_guard_));
  OZ (schema_guard_->get_table_schema( table_id, table_schema), table_id, is_link);
  return ret;
}

int ObSqlSchemaGuard::get_database_schema(
                                          const uint64_t database_id,
                                          const ObDatabaseSchema *&database_schema)
{
  int ret = OB_SUCCESS;
  database_schema = NULL;
  OV(OB_NOT_NULL(schema_guard_));
  OZ(schema_guard_->get_database_schema( database_id, database_schema), database_id);
  return ret;
}

int ObSqlSchemaGuard::get_column_schema(uint64_t table_id, const ObString &column_name,
                                          const ObColumnSchemaV2 *&column_schema,
                                          bool is_link /* = false */) const
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = NULL;
  OV (OB_NOT_NULL(schema_guard_));
  OV ((OB_INVALID_ID != table_id && !column_name.empty()));
  OZ (schema_guard_->get_table_schema( table_id, table_schema));
  if (table_schema == NULL) {
    // do nothing, same as schema_guard_->get_column_schema()
  } else {
    OX (column_schema = table_schema->get_column_schema(column_name));
  }
  return ret;
}

int ObSqlSchemaGuard::get_column_schema(uint64_t table_id, uint64_t column_id,
                                          const ObColumnSchemaV2 *&column_schema,
                                          bool is_link /* = false */) const
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = NULL;
  OV (OB_NOT_NULL(schema_guard_));
  OV ((OB_INVALID_ID != table_id && OB_INVALID_ID != column_id));
  OZ (schema_guard_->get_table_schema( table_id, table_schema));
  if (table_schema == NULL) {
    // do nothing, same as schema_guard_->get_column_schema()
  } else {
    OX (column_schema = table_schema->get_column_schema(column_id));
  }
  return ret;
}

int ObSqlSchemaGuard::get_table_schema_version(const uint64_t table_id,
                                               int64_t &schema_version) const
{
  int ret = OB_SUCCESS;
  
  OV (OB_NOT_NULL(schema_guard_));
  OZ (schema_guard_->get_schema_version(TABLE_SCHEMA, table_id, schema_version), table_id);
  return ret;
}

int ObSqlSchemaGuard::get_can_read_index_array(uint64_t table_id,
                                                 uint64_t *index_tid_array,
                                                 int64_t &size,
                                                 bool with_global_index,
                                                 bool with_domain_index,
                                                 bool with_spatial_index,
                                                 bool with_vector_index)
{
  int ret = OB_SUCCESS;
  
  OV (OB_NOT_NULL(schema_guard_));
  OZ (schema_guard_->get_can_read_index_array(table_id,
                                              index_tid_array, size,
                                              with_global_index, with_domain_index,
                                              with_spatial_index, with_vector_index));
  return ret;
}

int ObSqlCtx::set_partition_infos(const ObTablePartitionInfoArray &info, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  int64_t count = info.count();
  if (OB_NOT_NULL(partition_infos_)) {
    typedef common::ObFixedArray<ObTablePartitionInfo *, common::ObIAllocator>
        PartitionInfoStorage;
    PartitionInfoStorage *storage = static_cast<PartitionInfoStorage *>(partition_infos_);
    storage->~PartitionInfoStorage();
    partition_infos_allocator_->free(storage);
    partition_infos_ = NULL;
    partition_infos_allocator_ = NULL;
  }
  if (count > 0) {
    typedef common::ObFixedArray<ObTablePartitionInfo *, common::ObIAllocator>
        PartitionInfoStorage;
    void *buf = allocator.alloc(sizeof(PartitionInfoStorage));
    PartitionInfoStorage *storage = NULL;
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate partition info storage failed", K(ret), K(count));
    } else if (FALSE_IT(storage = new (buf) PartitionInfoStorage(&allocator))) {
    } else if (OB_FAIL(storage->init(count))) {
      LOG_WARN("init partition info failed", K(ret), K(count));
    } else {
      partition_infos_ = storage;
      partition_infos_allocator_ = &allocator;
      for (int64_t i = 0; i < count && OB_SUCC(ret); ++i) {
        if (OB_FAIL(storage->push_back(info.at(i)))) {
          LOG_WARN("push partition info failed", K(ret), K(count));
        }
      }
    }
    if (OB_FAIL(ret) && OB_NOT_NULL(storage)) {
      storage->~PartitionInfoStorage();
      allocator.free(storage);
      partition_infos_ = NULL;
      partition_infos_allocator_ = NULL;
    }
  }
  return ret;
}

const ObTablePartitionInfoArray &ObSqlCtx::get_partition_infos() const
{
  typedef common::ObSEArray<ObTablePartitionInfo *, 1> EmptyPartitionInfoArray;
  static const EmptyPartitionInfoArray EMPTY_PARTITION_INFOS;
  if (OB_NOT_NULL(partition_infos_)) {
    return *partition_infos_;
  } else {
    return EMPTY_PARTITION_INFOS;
  }
}

int64_t ObSqlCtx::get_partition_info_count() const
{
  return OB_NOT_NULL(partition_infos_) ? partition_infos_->count() : 0;
}

int ObSqlCtx::set_related_user_var_names(const ObIArray<ObString> &user_var_names,
                                         ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (user_var_names.count() > 0) {
    related_user_var_names_.reset();
    related_user_var_names_.set_allocator(&allocator);
    if (OB_FAIL(related_user_var_names_.init(user_var_names.count()))) {
      LOG_WARN("failed to init related_user_var_names", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < user_var_names.count(); i++) {
        if (OB_FAIL(related_user_var_names_.push_back(user_var_names.at(i)))) {
          LOG_WARN("failed to push back user var names", K(ret));
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    related_user_var_names_.reset();
  }
  return ret;
}

int ObSqlCtx::set_location_constraints(const ObLocationConstraintContext &location_constraint,
                                       ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  base_constraints_.reset();
  strict_constraints_.reset();
  non_strict_constraints_.reset();
  const ObIArray<LocationConstraint> &base_constraints = location_constraint.base_table_constraints_;
  const ObIArray<ObPwjConstraint *> &strict_constraints = location_constraint.strict_constraints_;
  const ObIArray<ObPwjConstraint *> &non_strict_constraints = location_constraint.non_strict_constraints_;
  if (base_constraints.count() > 0) {
    base_constraints_.set_allocator(&allocator);
    if (OB_FAIL(base_constraints_.init(base_constraints.count()))) {
      LOG_WARN("init base constraints failed", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < base_constraints.count(); i++) {
        if (OB_FAIL(base_constraints_.push_back(base_constraints.at(i)))) {
          LOG_WARN("failed to push back base constraint", K(ret));
        } else {
          // table_partition_info_ is only used during the plan generation phase
          base_constraints_.at(i).table_partition_info_ = NULL;
        }
      }
      LOG_DEBUG("set base constraints", K(base_constraints.count()));
    }
  }
  if (OB_SUCC(ret) && strict_constraints.count() > 0) {
    strict_constraints_.set_allocator(&allocator);
    if (OB_FAIL(strict_constraints_.init(strict_constraints.count()))) {
      LOG_WARN("init strict constraints failed", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < strict_constraints.count(); i++) {
        if (OB_FAIL(strict_constraints_.push_back(strict_constraints.at(i)))) {
          LOG_WARN("failed to push back location constraint", K(ret));
        }
      }
      LOG_DEBUG("set strict constraints", K(strict_constraints.count()));
    }
  }
  if (OB_SUCC(ret) && non_strict_constraints.count() > 0) {
    non_strict_constraints_.set_allocator(&allocator);
    if (OB_FAIL(non_strict_constraints_.init(non_strict_constraints.count()))) {
      LOG_WARN("init non strict constraints failed", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < non_strict_constraints.count(); i++) {
        if (OB_FAIL(non_strict_constraints_.push_back(non_strict_constraints.at(i)))) {
          LOG_WARN("failed to push back location constraint", K(ret));
        }
      }
      LOG_DEBUG("set non strict constraints", K(non_strict_constraints.count()));
    }
  }
  return ret;
}

int ObSqlCtx::set_multi_stmt_rowkey_pos(const common::ObIArray<int64_t> &multi_stmt_rowkey_pos,
                                        common::ObIAllocator &alloctor)
{
  int ret = OB_SUCCESS;
  if (!multi_stmt_rowkey_pos.empty()) {
    multi_stmt_rowkey_pos_.set_allocator(&alloctor);
    if (OB_FAIL(multi_stmt_rowkey_pos_.init(multi_stmt_rowkey_pos.count()))) {
      LOG_WARN("failed to init rowkey count", K(ret));
    } else if (OB_FAIL(append(multi_stmt_rowkey_pos_, multi_stmt_rowkey_pos))) {
      LOG_WARN("failed to append multi stmt rowkey pos", K(ret));
    } else { /*do nothing*/ }
  }
  return ret;
}

int ObQueryCtx::add_local_session_vars(ObIAllocator *alloc, const ObLocalSessionVar &local_session_var, int64_t &idx) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(all_local_session_vars_.push_back(ObLocalSessionVar()))) {
    LOG_WARN("push back local session var failed", K(ret));
  } else {
    idx = all_local_session_vars_.count() - 1;
    ObLocalSessionVar &local_var = all_local_session_vars_.at(idx);
    local_var.set_allocator(alloc);
    if (OB_FAIL(local_var.deep_copy(local_session_var))) {
      LOG_WARN("deep copy local session var failed", K(ret));
    }
  }
  return ret;
}

int ObQueryCtx::get_local_session_vars(const int64_t idx, const ObLocalSessionVar *&local_session_var) const 
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(idx < 0 || idx >= all_local_session_vars_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid idx", K(ret), K(idx), K(all_local_session_vars_.count()));
  } else {
    local_session_var = &all_local_session_vars_.at(idx);
  }
  return ret;
}

}
}
