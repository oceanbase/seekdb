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

#define USING_LOG_PREFIX SERVER

#include "ob_inner_sql_result.h"
#include "share/datum/typed_datum_compare.h"
#include "lib/signal/ob_signal_struct.h"
#include "observer/omt/ob_server_runtime_controller.h"
#include "share/rc/ob_server_runtime.h"

#include "omt/ob_server_runtime.h"
#include "observer/ob_inner_sql_connection.h"

namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
using namespace share;
using namespace share::schema;
using namespace sql;

namespace observer
{
inline int ObInnerSQLResult::check_extend_value(const common::DatumValue &obj)
{
  int ret = OB_SUCCESS;
  if (obj.is_null()) {
    ret = OB_ERR_NULL_VALUE;
  } else if (obj.is_min_value()) {
    ret = OB_ERR_MIN_VALUE;
  } else if (obj.is_max_value()) {
    ret = OB_ERR_MIN_VALUE;
  }
  return ret;
}

ObInnerSQLResult::ObInnerSQLResult(
    ObSQLSessionInfo &session,
    query::ObIPlanCacheAccessService &plan_cache_access_service,
    bool is_inner_session)
    : column_map_created_(false), column_indexed_(false), column_map_(),
      mem_context_(nullptr),
      mem_context_destroy_guard_(mem_context_),
      sql_ctx_(), schema_guard_(share::schema::ObSchemaMgrItem::MOD_INNER_SQL_RESULT),
      opened_(false), session_(session),
      plan_cache_access_service_(plan_cache_access_service),
      result_set_(nullptr), row_(NULL), native_row_(), native_result_(false),
      execute_start_ts_(0), execute_end_ts_(0),
      is_inited_(false),
      store_first_row_(false),
      iter_end_(false),
      is_read_(true),
      runtime_(nullptr),
      is_inner_session_(is_inner_session),
      interrupt_checker_()

{
  sql_ctx_.exec_type_ = InnerSql;
}

int ObInnerSQLResult::init()
{
  int ret = OB_SUCCESS;
  lib::ContextParam param;
  param.set_mem_attr(ObModIds::OB_RESULT_SET,
                     ObCtxIds::DEFAULT_CTX_ID)
    .set_properties(lib::USE_TL_PAGE_OPTIONAL)
    .set_page_size(OB_MALLOC_MIDDLE_BLOCK_SIZE);
  if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(mem_context_, param))) {
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>()->lock_runtime(runtime_))) {
    if (OB_IN_STOP_STATE == ret) {
      ret = OB_SERVER_RUNTIME_NOT_READY;
    }
  }
  if (OB_SUCC(ret)) {
    {
      // Inner SQL executes in the server runtime that owns this result.
      SERVER_MODULE_SCOPE {
        result_set_ = new (buf_) ObResultSet(
            session_, mem_context_->get_arena_allocator(),
            plan_cache_access_service_);
        result_set_->set_is_inner_result_set(true);
      }
    }
    is_inited_ = true;
  }
  return ret;
}

ObInnerSQLResult::~ObInnerSQLResult()
{
  close();
  if (result_set_ != nullptr) {
    result_set_->~ObResultSet();
  }
  if (runtime_ != nullptr) {
    runtime_->unlock();
    runtime_ = nullptr;
  }
}

int ObInnerSQLResult::open()
{
  int ret = OB_SUCCESS;
  execute_start_ts_ = ObTimeUtility::current_time();
  ObInnerSqlWaitGuard guard(is_inner_session(), &session_);
  ObInterruptCheckerGuard interrupt_guard(interrupt_checker_);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else {
    result_set().get_exec_context().set_plan_start_time(execute_start_ts_);
    SQL_INFO_GUARD(session_.get_current_query_string(), session_.get_cur_sql_id());
    ObInnerSQLSessionGuard sess_guard(&session_);
    bool is_select = ObStmt::is_select_stmt(result_set_->get_stmt_type());
    WITH_CONTEXT(mem_context_) {
      if (opened_) {
        ret = OB_INIT_TWICE;
      } else if (OB_FAIL(result_set_->open())) {
        result_set_->get_exec_context().get_das_ctx().record_access_error(ret);
        // move after precess_retry().
//        result_set_->close();
      } else if (is_read_&& is_select) {
        //prefetch 1 row for throwing error code and retry
        opened_ = true;
        if (OB_FAIL(native_result_ ? result_set_->get_next_native_result_row(native_row_)
                                       : result_set_->get_next_row(row_))) {
          if (OB_ITER_END == ret) {
            iter_end_ = true;
            ret = OB_SUCCESS;
          } else {
            result_set_->get_exec_context().get_das_ctx().record_access_error(ret);
          }
        } else {
          store_first_row_ = true;
        }
      } else {
        opened_ = true;
      }
    }
  }
  execute_end_ts_ = ObTimeUtility::current_time();
  return ret;
}

int ObInnerSQLResult::close()
{
  int ret = OB_SUCCESS;
  // close can be executed too if result set not open.
  if (opened_) {
    // opened=true imply is_inited=true
    if (OB_FAIL(inner_close())) {
    }
  }
  column_map_.clear();
  column_indexed_ = false;
  return ret;
}

int ObInnerSQLResult::force_close()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(inner_close())) {
  }
  column_map_.clear();
  column_indexed_ = false;
  return ret;
}
int ObInnerSQLResult::inner_close()
{
  int ret = OB_SUCCESS;
  SQL_INFO_GUARD(session_.get_current_query_string(), session_.get_cur_sql_id());
  ObInnerSQLSessionGuard sess_guard(&session_);
  ObInterruptCheckerGuard interrupt_guard(interrupt_checker_);

  ObInnerSqlWaitGuard guard(is_inner_session(), &session_);

  WITH_CONTEXT(mem_context_) {
    if (OB_FAIL(result_set_->close())) {
      result_set_->get_exec_context().get_das_ctx().record_access_error(ret);
    }
  }
  row_ = nullptr;
  native_row_ = ResultRowView();
  opened_ = false;
  store_first_row_ = false;
  iter_end_ = false;
  return ret;
}

int ObInnerSQLResult::next()
{
  int ret = OB_SUCCESS;
  ObInnerSqlWaitGuard guard(is_inner_session(), &session_);
  ObInterruptCheckerGuard interrupt_guard(interrupt_checker_);
  if (!opened_) {
    ret = OB_NOT_INIT;
  } else if (iter_end_) {
    ret = OB_ITER_END;
  } else if (store_first_row_) {
    store_first_row_ = false;
  } else {
    row_ = NULL;
    native_row_ = ResultRowView();
    SQL_INFO_GUARD(session_.get_current_query_string(), session_.get_cur_sql_id());
    ObInnerSQLSessionGuard sess_guard(&session_);
    WITH_CONTEXT(mem_context_) {
      if (OB_FAIL(native_result_ ? result_set_->get_next_native_result_row(native_row_)
                                       : result_set_->get_next_row(row_))) {
        if (OB_ITER_END != ret) {
          result_set_->get_exec_context().get_das_ctx().record_access_error(ret);
        }
      }

    }
  }
  return ret;
}

int ObInnerSQLResult::get_native_value(int64_t col_idx, ObIAllocator &owner,
                                        DatumValue &value) const
{
  int ret = OB_SUCCESS;
  if (!opened_) { ret = OB_NOT_INIT; }
  else if (!native_result_) { ret = OB_INVALID_ARGUMENT; }
  else {
    DatumValue borrowed;
    OZ(get_value(col_idx, borrowed));
    OZ(clone_typed_value(borrowed, value, owner));
  }
  return ret;
}

int ObInnerSQLResult::build_column_map() const
{
  int ret = OB_SUCCESS;
  if (!opened_) {
    ret = OB_NOT_INIT;
  } else if (!column_map_created_) {
    if (OB_FAIL(column_map_.create(COLUMN_MAP_BUCKET_NUM,
        ObModIds::OB_HASH_BUCKET_SQL_COLUMN_MAP, ObModIds::OB_HASH_NODE_SQL_COLUMN_MAP))) {
    } else {
      column_map_created_ = true;
    }
  }
  if (OB_SUCC(ret)) {
    column_map_.clear();
    const ColumnsFieldIArray *fields = result_set_->get_field_columns();
    if (OB_ISNULL(fields)) {
      ret = OB_INVALID_ARGUMENT;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < fields->count(); ++i) {
      if (nullptr == fields->at(i).cname_) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        if (OB_FAIL(column_map_.set_refactored(fields->at(i).cname_, i))) {
          // ignore duplicate column name
          if (OB_HASH_EXIST == ret) {
            ret = OB_SUCCESS;
          } else {
          }
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    column_indexed_ = true;
  }

  return ret;
}

int ObInnerSQLResult::find_idx(const char *col_name, int64_t &idx) const
{
  idx = OB_INVALID_INDEX;
  int ret = OB_SUCCESS;
  if (!opened_) {
    ret = OB_NOT_INIT;
  } else if (nullptr == col_name) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (OB_UNLIKELY(!column_indexed_)) {
      if (OB_FAIL(build_column_map())) {
      }
    }
    if (OB_SUCC(ret)) {
      ret = column_map_.get_refactored(ObString::make_string(col_name), idx);
      if (OB_SUCCESS != ret && OB_HASH_NOT_EXIST != ret) {
      } else if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_ENTRY_NOT_EXIST;
      } else if (OB_INVALID_INDEX == idx) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

#define DEF_GET_VALUE_BY_INDEX_IMPL(name, getter, type) \
  int ObInnerSQLResult::name(const int64_t col_idx, type &val) const \
  { \
    int ret = OB_SUCCESS; \
    DatumValue value; \
    if (OB_FAIL(get_value(col_idx, value))) {} \
    else if (OB_FAIL(check_extend_value(value))) {} \
    else { ret = value.getter(val); } \
    return ret; \
  }
#define DEF_GET_VALUE_BY_INDEX(name, type) DEF_GET_VALUE_BY_INDEX_IMPL(name, name, type)

DEF_GET_VALUE_BY_INDEX_IMPL(get_uint, get_uint64, uint64_t);
#define DEF_GET_TEMPORAL_VALUE(name, predicate, type) \
  int ObInnerSQLResult::name(const int64_t col_idx, type &val) const \
  { \
    int ret = OB_SUCCESS; DatumValue value; \
    if (OB_FAIL(get_value(col_idx, value))) {} \
    else if (OB_FAIL(check_extend_value(value))) {} \
    else if (!value.predicate()) { ret = OB_OBJ_TYPE_ERROR; } \
    else { val = value.name(); } \
    return ret; \
  }
DEF_GET_TEMPORAL_VALUE(get_datetime, is_datetime, int64_t);
DEF_GET_TEMPORAL_VALUE(get_date, is_date, int32_t);
DEF_GET_TEMPORAL_VALUE(get_time, is_time, int64_t);
DEF_GET_TEMPORAL_VALUE(get_year, is_year, uint8_t);
#undef DEF_GET_TEMPORAL_VALUE

int ObInnerSQLResult::get_timestamp(const int64_t col_idx,
    const common::ObTimeZoneInfo *tz_info, int64_t &val) const
{
  UNUSED(tz_info);
  int ret = OB_SUCCESS; DatumValue value;
  if (OB_FAIL(get_value(col_idx, value))) {}
  else if (OB_FAIL(check_extend_value(value))) {}
  else {
    // This historical getter reads scalar bits without a timestamp type check.
    // Native DATE/YEAR/FLOAT payloads have fewer bytes than the old union.
    const SqlValueDatumMapType mapping = ObDatum::get_sql_value_datum_map_type(value.get_type());
    const int64_t bytes = ObDatum::get_reserved_size(mapping);
    if ((mapping != OBJ_DATUM_8BYTE_DATA && mapping != OBJ_DATUM_4BYTE_DATA
         && mapping != OBJ_DATUM_1BYTE_DATA)
        || value.datum_.ptr_ == nullptr || value.datum_.len_ != bytes) {
      ret = OB_OBJ_TYPE_ERROR;
    } else {
      uint64_t scalar = 0;
      MEMCPY(&scalar, value.datum_.ptr_, bytes);
      val = static_cast<int64_t>(scalar);
    }
  }
  return ret;
}

int ObInnerSQLResult::get_bool(const int64_t col_idx, bool &bool_val) const
{
  int64_t v = 0;
  int ret = OB_SUCCESS;
  if (!opened_) {
    ret = OB_NOT_INIT;
  } else if (OB_INVALID_INDEX == col_idx) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_int(col_idx, v))) {
  } else {
    bool_val = v;
  }
  return ret;
}

DEF_GET_VALUE_BY_INDEX(get_varchar, ObString);
DEF_GET_VALUE_BY_INDEX(get_float, float);
DEF_GET_VALUE_BY_INDEX(get_double, double);

int ObInnerSQLResult::get_int(const int64_t col_idx, int64_t &int_val) const
{
  int ret = OB_SUCCESS; DatumValue value;
  if (OB_FAIL(get_value(col_idx, value))) {}
  else if (OB_FAIL(check_extend_value(value))) {}
  else if (value.get_type_class() != ObIntTC) { ret = OB_OBJ_TYPE_ERROR; }
  else { int_val = value.get_int(); }
  return ret;
}

int ObInnerSQLResult::get_number_impl(const int64_t col_idx, number::ObNumber &ret_nmb) const
{
  int ret = OB_SUCCESS; DatumValue value;
  if (OB_FAIL(get_value(col_idx, value))) {}
  else if (OB_FAIL(check_extend_value(value))) {}
  else if (value.is_decimal_int()) {
    ret = wide::to_number(value.get_decimal_int(), value.get_int_bytes(), value.get_scale(),
        mem_context_->get_arena_allocator(), ret_nmb);
  } else { ret = value.get_number(ret_nmb); }
  return ret;
}

#undef DEF_GET_VALUE_BY_INDEX
#undef DEF_GET_VALUE_BY_INDEX_IMPL

int ObInnerSQLResult::get_type(const int64_t col_idx, SqlTypeMeta &type) const
{
  DatumValue value;
  const int ret = get_value(col_idx, value);
  if (ret == OB_SUCCESS) { type = value.meta_; }
  return ret;
}

int64_t ObInnerSQLResult::get_column_count() const
{
  return !opened_ ? -1 : native_result_ ? native_row_.count()
      : row_ == nullptr ? -1 : row_->get_count();
}

int ObInnerSQLResult::get_value(int64_t col_idx, DatumValue &value) const
{
  int ret = OB_SUCCESS;
  DatumValue candidate;
  if (!opened_) { ret = OB_NOT_INIT; }
  else if (col_idx < 0) { ret = OB_INVALID_ARGUMENT; }
  else if (native_result_) { ret = native_row_.get_value(col_idx, candidate); }
  else if (row_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
  else if (col_idx >= row_->get_count()) { ret = OB_SIZE_OVERFLOW; }
  else {
    const int64_t idx = row_->projector_ == nullptr ? col_idx : row_->projector_[col_idx];
    if (idx < 0 || idx >= row_->count_) { ret = OB_ERR_UNEXPECTED; }
    else { candidate = row_->cells_[idx]; }
  }
  if (OB_SUCC(ret)) { value = candidate; }
  return ret;
}

#define DEF_GET_VALUE_BY_NAME(name, type)                                        \
  int ObInnerSQLResult::name(const char *col_name, type &val) const              \
  {                                                                              \
    int ret = OB_SUCCESS;                                                        \
    int64_t idx = OB_INVALID_INDEX;                                              \
    if (!opened_) {                                                              \
      ret = OB_NOT_INIT;                                                         \
      LOG_WARN("not opened", K(ret));                                            \
    } else if (NULL == col_name) {                                               \
      ret = OB_INVALID_ARGUMENT;                                                 \
      LOG_WARN("column name can not be NULL", K(ret));                           \
    } else if (OB_FAIL(find_idx(col_name, idx))) {                               \
      if (OB_ENTRY_NOT_EXIST == ret) {                                           \
        ret = OB_ERR_COLUMN_NOT_FOUND;                                           \
      } else {                                                                   \
        LOG_WARN("find column index failed", K(ret), K(col_name));               \
      }                                                                          \
    } else if (OB_INVALID_INDEX == idx) {                                        \
      ret = OB_ERR_UNEXPECTED;                                                   \
      LOG_WARN("invalid index returned", K(ret), K(idx));                        \
    } else if (OB_FAIL(name(idx, val))) {                                        \
      if (OB_ERR_NULL_VALUE != ret) {                                            \
        LOG_WARN(#name" failed", K(ret), K(idx));                                \
      }                                                                          \
    }                                                                            \
    return ret;                                                                  \
  }

DEF_GET_VALUE_BY_NAME(get_int, int64_t);
DEF_GET_VALUE_BY_NAME(get_uint, uint64_t);
DEF_GET_VALUE_BY_NAME(get_datetime, int64_t);
DEF_GET_VALUE_BY_NAME(get_date, int32_t);
DEF_GET_VALUE_BY_NAME(get_time, int64_t);
DEF_GET_VALUE_BY_NAME(get_year, uint8_t);
DEF_GET_VALUE_BY_NAME(get_bool, bool);
DEF_GET_VALUE_BY_NAME(get_varchar, ObString);
DEF_GET_VALUE_BY_NAME(get_float, float);
DEF_GET_VALUE_BY_NAME(get_double, double);
DEF_GET_VALUE_BY_NAME(get_number_impl, number::ObNumber);
DEF_GET_VALUE_BY_NAME(get_type, SqlTypeMeta);
DEF_GET_VALUE_BY_NAME(get_value, DatumValue);
#undef DEF_GET_VALUE_BY_NAME

int ObInnerSQLResult::get_timestamp(const char *col_name,  const common::ObTimeZoneInfo *tz_info,
    int64_t &val) const
{
  UNUSED(tz_info);
  int ret = OB_SUCCESS;
  int64_t idx = OB_INVALID_INDEX;
  if (OB_UNLIKELY(!opened_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(NULL == col_name)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(find_idx(col_name, idx))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_ERR_COLUMN_NOT_FOUND;
    } else {
    }
  } else if (OB_UNLIKELY(OB_INVALID_INDEX == idx)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(get_timestamp(idx, tz_info, val))) {
    if (OB_ERR_NULL_VALUE != ret) {
    }
  }
  return ret;
}

int ObInnerSQLResult::get_number(const int64_t col_idx,
                                 common::number::ObNumber &nmb_val) const
{
  int ret = OB_SUCCESS;
  if (!opened_) {
    ret = OB_NOT_INIT;
  } else if (OB_INVALID_INDEX == col_idx) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_number_impl(col_idx, nmb_val))) {
  }
  return ret;
}

int ObInnerSQLResult::get_number(const char *col_name, common::number::ObNumber &nmb_val) const
{
  int ret = OB_SUCCESS;
  if (!opened_) {
    ret = OB_NOT_INIT;
  } else if (NULL == col_name) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_number_impl(col_name, nmb_val))) {
  }
  return ret;
}

int ObInnerSQLResult::inner_get_number(const int64_t col_idx, number::ObNumber &nmb_val,
    IAllocator &allocator) const
{
  UNUSED(allocator);
  int ret = OB_SUCCESS;
  if (!opened_) {
    ret = OB_NOT_INIT;
  } else if (OB_INVALID_INDEX == col_idx) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_number_impl(col_idx, nmb_val))) {
  }
  return ret;
}

int ObInnerSQLResult::inner_get_number(const char *col_name, number::ObNumber &nmb_val,
    IAllocator &allocator) const
{
  UNUSED(allocator);
  int ret = OB_SUCCESS;
  if (!opened_) {
    ret = OB_NOT_INIT;
  } else if (NULL == col_name) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_number_impl(col_name, nmb_val))) {
  }
  return ret;
}

int ObInnerSQLResult::print_info() const
{
  int ret = OB_SUCCESS;
  if (!opened_) {
    ret = OB_NOT_INIT;
  } else {
    LOG_INFO("result", K_(result_set));
  }
  return ret;
}
} // end of namespace observer
} // end of namespace oceanbase
