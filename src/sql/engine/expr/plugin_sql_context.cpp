/*
 * Copyright (c) 2026 OceanBase.
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

#include "sql/engine/expr/plugin_sql_context.h"

#include <cstring>
#include <limits>
#include <new>
#include <vector>
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "query/session/ob_inner_sql_connection_access.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/ob_result_set.h"
#include "sql/ob_sql.h"
#include "sql/ob_sql_trans_control.h"
#include "sql/parser/ob_parser.h"
#include "sql/resolver/ddl/catalog_routine_lookup.h"
#include "sql/resolver/ddl/extension_routine_resolver.h"
#include "share/schema/routine_catalog_transaction.h"
#include "sql/session/ob_inner_sql_connection.h"

namespace oceanbase { namespace sql {
using namespace common;

namespace {
constexpr uint64_t SQL_BYTE_LIMIT = 16ULL * 1024 * 1024;

int convert_parameters(const seekdb_plugin_sql_value_v1_t *values,
                       uint32_t count, ParamStore &parameters)
{
  int ret = OB_SUCCESS;
  uint64_t bytes = 0;
  for (uint32_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    const auto &value = values[i];
    ObObjParam parameter;
    if (value.struct_size < sizeof(value) || value.reserved[0] || value.reserved[1]
        || value.data_size > SQL_BYTE_LIMIT - bytes
        || (value.data_size && !value.data)) return OB_INVALID_ARGUMENT;
    bytes += value.data_size;
    switch (value.kind) {
      case SEEKDB_PLUGIN_SQL_NULL:
        if (value.data_size != 0) return OB_INVALID_ARGUMENT;
        parameter.set_null();
        break;
      case SEEKDB_PLUGIN_SQL_INT64: {
        if (value.data_size != sizeof(int64_t)) return OB_INVALID_ARGUMENT;
        int64_t number;
        std::memcpy(&number, value.data, sizeof(number));
        parameter.set_int(number);
        break;
      }
      case SEEKDB_PLUGIN_SQL_UINT64: {
        if (value.data_size != sizeof(uint64_t)) return OB_INVALID_ARGUMENT;
        uint64_t number;
        std::memcpy(&number, value.data, sizeof(number));
        parameter.set_uint64(number);
        break;
      }
      case SEEKDB_PLUGIN_SQL_FLOAT64: {
        if (value.data_size != sizeof(double)) return OB_INVALID_ARGUMENT;
        double number;
        std::memcpy(&number, value.data, sizeof(number));
        parameter.set_double(number);
        break;
      }
      case SEEKDB_PLUGIN_SQL_TEXT:
      case SEEKDB_PLUGIN_SQL_BYTES: {
        ObString text(static_cast<int32_t>(value.data_size),
                      static_cast<const char *>(value.data));
        if (value.kind == SEEKDB_PLUGIN_SQL_BYTES) {
          parameter.set_varbinary(text);
        } else {
          parameter.set_varchar(text);
          parameter.set_collation_type(CS_TYPE_UTF8MB4_BIN);
          parameter.set_collation_level(CS_LEVEL_COERCIBLE);
        }
        break;
      }
      default: return OB_NOT_SUPPORTED;
    }
    parameter.set_param_meta();
    ret = parameters.push_back(parameter);
  }
  return ret;
}

// Use actual SQL prepare/execute, not rendered SQL or a privileged catalog
// connection. Each executor is synchronous; no result set outlives the nested
// statement state saved by run_sql().
class PluginSqlPrepare final : public sqlclient::ObIExecutor
{
public:
  PluginSqlPrepare(const ObString &sql, ObSQLSessionInfo &session)
      : sql_(sql), session_(session), cache_(nullptr), id_(OB_INVALID_ID),
        type_(stmt::T_NONE), parameter_count_(0), trans_type_(session.get_trans_type()) {}
  ~PluginSqlPrepare() { close(); }

  int close()
  {
    int ret = OB_SUCCESS;
    if (id_ != OB_INVALID_ID && cache_ != nullptr) {
      ret = session_.close_ps_stmt(*cache_, id_);
      id_ = OB_INVALID_ID;
    }
    return ret;
  }

  int execute(ObSql &engine, ObSqlCtx &context, ObResultSet &result) override
  {
    int ret = close(); // also releases a handle allocated by an earlier retry
    if (OB_FAIL(ret)) return ret;
    cache_ = &engine.get_ps_cache();
    session_.set_trans_type(trans_type_);
    context.is_prepare_protocol_ = true;
    context.is_prepare_stage_ = true;
    context.disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
    result.set_user_sql(true);
    result.set_ps_protocol();
    result.get_exec_context().set_is_plugin_sql(true);
    if (OB_FAIL(engine.stmt_prepare(sql_, context, result, false))) return ret;
    id_ = result.get_statement_id();
    type_ = result.get_stmt_type();
    if (result.get_param_fields() == nullptr) return OB_ERR_UNEXPECTED;
    parameter_count_ = result.get_param_fields()->count();
    switch (type_) {
      case stmt::T_SELECT:
      case stmt::T_INSERT:
      case stmt::T_REPLACE:
      case stmt::T_UPDATE:
      case stmt::T_DELETE: return OB_SUCCESS;
      default: return OB_NOT_SUPPORTED;
    }
  }
  int process_result(ObResultSet &) override { return OB_SUCCESS; }
  ObPsStmtId id() const { return id_; }
  stmt::StmtType type() const { return type_; }
  int64_t parameter_count() const { return parameter_count_; }
private:
  ObString sql_;
  ObSQLSessionInfo &session_;
  ObPsCache *cache_;
  ObPsStmtId id_;
  stmt::StmtType type_;
  int64_t parameter_count_;
  transaction::ObTxClass trans_type_;
};

union PluginSqlNumber { int64_t integer; uint64_t unsigned_integer; double floating; };

class PluginSqlExecute final : public sqlclient::ObIExecutor
{
public:
  PluginSqlExecute(const PluginSqlPrepare &prepared, const ParamStore &parameters,
      uint64_t max_rows, seekdb_plugin_sql_consume_row_v1_fn consume, void *consumer,
      seekdb_plugin_sql_result_v1_t &output, transaction::ObTxClass trans_type)
      : prepared_(prepared), parameters_(parameters), max_rows_(max_rows),
        consume_(consume), consumer_(consumer), output_(output), trans_type_(trans_type) {}

  int execute(ObSql &engine, ObSqlCtx &context, ObResultSet &result) override
  {
    context.is_prepare_protocol_ = true;
    context.is_prepare_stage_ = false;
    context.disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
    result.set_user_sql(true);
    result.set_ps_protocol();
    result.get_session().set_trans_type(trans_type_);
    result.get_exec_context().set_is_plugin_sql(true);
    return engine.stmt_execute(prepared_.id(), prepared_.type(), parameters_, context, result, false);
  }

  int process_result(ObResultSet &result) override
  {
    if (prepared_.type() != stmt::T_SELECT) {
      output_.affected_rows = result.get_affected_rows();
      return OB_SUCCESS;
    }
    uint64_t bytes = 0;
    const ObNewRow *row = nullptr;
    int ret = OB_SUCCESS;
    std::vector<seekdb_plugin_sql_value_v1_t> values;
    std::vector<PluginSqlNumber> numbers;
    while (OB_SUCCESS == (ret = result.get_next_row(row))) {
      if (!row || row->count_ < 0 || row->count_ > SEEKDB_PLUGIN_MAX_ARGUMENTS) return OB_NOT_SUPPORTED;
      if (output_.returned_rows >= max_rows_) return OB_SIZE_OVERFLOW;
      values.assign(row->count_, {});
      numbers.resize(row->count_);
      for (int64_t i = 0; i < row->count_; ++i) {
        const ObObj &cell = row->cells_[i];
        auto &value = values[i];
        value.struct_size = sizeof(value);
        if (cell.is_null()) {
          value.kind = SEEKDB_PLUGIN_SQL_NULL;
        } else if (ob_is_int_tc(cell.get_type())) {
          value.kind = SEEKDB_PLUGIN_SQL_INT64;
          numbers[i].integer = cell.get_int();
          value.data = &numbers[i].integer;
          value.data_size = sizeof(int64_t);
        } else if (ob_is_uint_tc(cell.get_type())) {
          value.kind = SEEKDB_PLUGIN_SQL_UINT64;
          numbers[i].unsigned_integer = cell.get_uint64();
          value.data = &numbers[i].unsigned_integer;
          value.data_size = sizeof(uint64_t);
        } else if (ob_is_float_type(cell.get_type()) || ob_is_double_type(cell.get_type())) {
          value.kind = SEEKDB_PLUGIN_SQL_FLOAT64;
          numbers[i].floating = ob_is_float_type(cell.get_type()) ? cell.get_float() : cell.get_double();
          value.data = &numbers[i].floating;
          value.data_size = sizeof(double);
        } else if (ob_is_string_type(cell.get_type()) && !cell.is_lob_storage()) {
          if (cell.get_collation_type() == CS_TYPE_BINARY) {
            value.kind = SEEKDB_PLUGIN_SQL_BYTES;
          } else if (ObCharset::charset_type_by_coll(cell.get_collation_type()) == CHARSET_UTF8MB4) {
            value.kind = SEEKDB_PLUGIN_SQL_TEXT;
          } else {
            return OB_NOT_SUPPORTED; // caller can explicitly CONVERT to utf8mb4
          }
          value.data = cell.get_string().ptr();
          value.data_size = cell.get_string().length();
        } else {
          return OB_NOT_SUPPORTED; // never expose LOB locators/number internals as bytes
        }
        if (value.data_size > SQL_BYTE_LIMIT - bytes) return OB_SIZE_OVERFLOW;
        bytes += value.data_size;
      }
      if (consume_(consumer_, values.data(), static_cast<uint32_t>(values.size()))
          != SEEKDB_PLUGIN_STATUS_OK) return OB_CANCELED;
      ++output_.returned_rows;
    }
    return ret == OB_ITER_END ? OB_SUCCESS : ret;
  }
private:
  const PluginSqlPrepare &prepared_;
  const ParamStore &parameters_;
  uint64_t max_rows_;
  seekdb_plugin_sql_consume_row_v1_fn consume_;
  void *consumer_;
  seekdb_plugin_sql_result_v1_t &output_;
  transaction::ObTxClass trans_type_;
};

int run_sql(ObExecContext &context, const ObString &sql, const ParamStore &parameters,
    uint64_t max_rows, seekdb_plugin_sql_consume_row_v1_fn consume, void *consumer,
    seekdb_plugin_sql_result_v1_t &output)
{
  int ret = context.check_status();
  auto *session = context.get_my_session();
  if (OB_FAIL(ret)) return ret;
  if (!session || session->get_cur_exec_ctx() != &context) return OB_STATE_NOT_MATCH;
  if (context.get_nested_level() >= 64) return OB_SIZE_OVERFLOW;
  // The PS engine is not the multi-statement protocol boundary. Match the
  // normal PREPARE command's parser-based check (semicolons in literals and
  // comments are not separators).
  ObArenaAllocator parse_allocator;
  ObParser parser(parse_allocator, session->get_sql_mode(), session->get_charsets4parser());
  ObSEArray<ObString, 1> statements;
  ObMPParseStat parse_status;
  if (OB_FAIL(parser.split_multiple_stmt(sql, statements, parse_status, false, true))) return ret;
  if (statements.count() != 1) return OB_NOT_SUPPORTED;
  if (OB_FAIL(ObSqlTransControl::prepare_plugin_sql(context))) return ret;
  sqlclient::ObISQLConnectionGuard guard;
  if (OB_FAIL(query::ObInnerSQLConnectionAccess::create_spi_connection_with_external_session(session, guard))) return ret;
  auto *connection = as_inner_sql_connection(guard.get_ptr());
  if (!connection) return OB_ERR_UNEXPECTED;
  connection->set_check_priv(true);
  ObSQLSessionInfo::StmtSavedValue saved_session;
  ObIInnerSQLConnection::SavedValue saved_connection;
  const auto trans_type = session->get_trans_type();
  if (OB_FAIL(connection->begin_nested_session(saved_session, saved_connection, false))) return ret;
  // Restore on exceptions as well as normal/error returns. The opaque plugin
  // callback cannot cause prepared handles or nested session state to escape.
  try {
    PluginSqlPrepare prepared(sql, *session);
    if (OB_FAIL(connection->execute(prepared))) {
    } else if (prepared.parameter_count() != parameters.count()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (prepared.type() == stmt::T_SELECT && !consume) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      PluginSqlExecute execution(prepared, parameters, max_rows, consume, consumer, output, trans_type);
      ret = connection->execute(execution);
    }
    const int close_ret = prepared.close();
    if (OB_SUCC(ret)) ret = close_ret;
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  const int restore_ret = connection->end_nested_session(saved_session, saved_connection);
  session->set_trans_type(trans_type);
  return OB_SUCC(ret) ? restore_ret : ret;
}
} // namespace

PluginSqlContext::PluginSqlContext(ObExecContext &context)
    : context_(context), thread_(std::this_thread::get_id()), executing_(false), error_(OB_SUCCESS) {}

const seekdb_plugin_sql_api_v1_t *PluginSqlContext::sql_api()
{
  static const seekdb_plugin_sql_api_v4_t API = {{{{
      sizeof(API), SEEKDB_PLUGIN_SQL_SPI_MAJOR, SEEKDB_PLUGIN_SQL_CATALOG_MUTATION_MINOR, 0, execute, {0}},
      poll_query, {0}}, lookup_routine, {0}}, mutate_routine, {0}};
  return &API.v3.v2.v1;
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL PluginSqlContext::mutate_routine(
    seekdb_plugin_sql_context_handle_t *opaque, const char *sql, uint64_t sql_size,
    seekdb_plugin_routine_mutation_result_v1_t *output)
{
  using namespace share::schema;
  if (!opaque) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &self = *reinterpret_cast<PluginSqlContext *>(opaque);
  if (self.thread_ != std::this_thread::get_id()) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  if (!output || output->struct_size < sizeof(*output)) {
    if (self.error_ == OB_SUCCESS) self.error_ = OB_INVALID_ARGUMENT;
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  *output = {}; output->struct_size = sizeof(*output);
  if (self.executing_ || self.error_ != OB_SUCCESS) {
    if (self.error_ == OB_SUCCESS) self.error_ = OB_STATE_NOT_MATCH;
  } else if (!sql || sql_size == 0 || sql_size > 4ULL * 1024 * 1024 || std::memchr(sql, '\0', sql_size)) {
    self.error_ = OB_INVALID_ARGUMENT;
  } else {
    self.executing_ = true;
    int ret = OB_SUCCESS;
    try {
      std::string error;
      // A reentrant plugin call can set the invocation's first error even if
      // an extra status checker ignores it. Return it INSIDE the Rust action
      // frame, so any operation effects are rolled back before returning.
      class Mutation final : public ICallerCatalogMutation {
      public:
        Mutation(const std::string &sql, std::string &error, const int &invocation_error)
            : routine_(sql, error), error_(invocation_error) {}
        int preflight(ObExecContext &context) override {
          if (error_ != OB_SUCCESS) return error_;
          const int ret = routine_.preflight(context);
          return error_ == OB_SUCCESS ? ret : error_;
        }
        int apply(ObExecContext &context, ObMySQLTransaction &transaction,
            RoutineSchemaOverlay &schema, RoutinePrivilegeOverlay &privileges,
            rootserver::IRoutineCacheInvalidation &invalidation) override {
          if (error_ != OB_SUCCESS) return error_;
          const int ret = routine_.apply(context, transaction, schema, privileges, invalidation);
          return error_ == OB_SUCCESS ? ret : error_;
        }
        CallerRoutineMutation routine_;
        const int &error_;
      } mutation(std::string(sql, sql_size), error, self.error_);
      CatalogOperationResult result;
      ret = run_caller_catalog_operation(self.context_, mutation, result);
      if (ret != OB_SUCCESS) {
        // Phase/status only: routine SQL can contain sensitive literals.
        // INFO remains enabled by the default EDIAG threshold; WDIAG does not.
        LOG_INFO("plugin caller catalog operation failed", K(ret),
                 "phase", result.failed_phase_, "close_error", result.close_error_,
                 "identity_error", result.identity_error_,
                 "data_rollback_error", result.data_rollback_error_,
                 "view_rollback_error", result.view_rollback_error_,
                 "poison_error", result.poison_error_);
      }
      switch (result.outcome_) {
        case CatalogOperationResult::Outcome::NOT_STARTED: output->outcome = SEEKDB_PLUGIN_CATALOG_NOT_STARTED; break;
        case CatalogOperationResult::Outcome::APPLIED: output->outcome = SEEKDB_PLUGIN_CATALOG_APPLIED; break;
        case CatalogOperationResult::Outcome::ROLLED_BACK: output->outcome = SEEKDB_PLUGIN_CATALOG_ROLLED_BACK; break;
        case CatalogOperationResult::Outcome::REQUIRES_ABORT: output->outcome = SEEKDB_PLUGIN_CATALOG_REQUIRES_ABORT; break;
      }
      output->close_error = result.close_error_;
      output->identity_error = result.identity_error_;
      output->data_rollback_error = result.data_rollback_error_;
      output->view_rollback_error = result.view_rollback_error_;
      output->poison_error = result.poison_error_;
      if (ret == OB_SUCCESS && self.error_ == OB_SUCCESS && output->outcome == SEEKDB_PLUGIN_CATALOG_APPLIED)
        output->object_id = mutation.routine_.object_id();
    } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) { ret = OB_ERR_UNEXPECTED; }
    self.executing_ = false;
    if (self.error_ == OB_SUCCESS) self.error_ = ret;
  }
  output->database_error = self.error_;
  if (self.error_ == OB_SUCCESS) return SEEKDB_PLUGIN_STATUS_OK;
  output->object_id = 0;
  if (self.error_ == OB_ALLOCATE_MEMORY_FAILED) return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  if (self.error_ == OB_INVALID_ARGUMENT) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (self.error_ == OB_TIMEOUT) return SEEKDB_PLUGIN_STATUS_TIMEOUT;
  return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL PluginSqlContext::lookup_routine(
    seekdb_plugin_sql_context_handle_t *opaque, uint32_t kind, const char *name, uint64_t name_size,
    seekdb_plugin_routine_lookup_result_v1_t *output)
{
  if (!opaque || !output || output->struct_size < sizeof(*output)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &self = *reinterpret_cast<PluginSqlContext *>(opaque);
  if (self.thread_ != std::this_thread::get_id()) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  *output = {}; output->struct_size = sizeof(*output);
  if (self.executing_ || self.error_ != OB_SUCCESS) {
    if (self.error_ == OB_SUCCESS) self.error_ = OB_STATE_NOT_MATCH;
  } else if (!name || !name_size || name_size > OB_MAX_ROUTINE_NAME_BINARY_LENGTH ||
      std::memchr(name, '\0', name_size) || (kind != 1 && kind != 2)) {
    self.error_ = OB_INVALID_ARGUMENT;
  } else {
    self.executing_ = true;
    int ret = OB_SUCCESS;
    try {
      ret = self.context_.check_status();
      auto *session = self.context_.get_my_session();
      auto *sql_context = self.context_.get_sql_ctx();
      if (ret == OB_SUCCESS && (!session || session->get_cur_exec_ctx() != &self.context_ ||
          !sql_context || sql_context->session_info_ != session || !sql_context->schema_guard_ ||
          !self.context_.get_physical_plan_ctx() || !self.context_.get_physical_plan_ctx()->get_phy_plan())) {
        ret = OB_STATE_NOT_MATCH;
      }
      if (ret == OB_SUCCESS && self.error_ == OB_SUCCESS) ret = lookup_catalog_routine(*session, *sql_context->schema_guard_,
          session->get_database_id(), static_cast<share::plugin::CatalogRoutineKind>(kind),
          std::string(name, name_size), output->object_id);
    } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) { ret = OB_ERR_UNEXPECTED; }
    self.executing_ = false;
    if (self.error_ == OB_SUCCESS) self.error_ = ret;
  }
  output->database_error = self.error_;
  if (self.error_ == OB_SUCCESS) return SEEKDB_PLUGIN_STATUS_OK;
  output->object_id = 0;
  if (self.error_ == OB_ALLOCATE_MEMORY_FAILED) return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  if (self.error_ == OB_INVALID_ARGUMENT) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (self.error_ == OB_TIMEOUT) return SEEKDB_PLUGIN_STATUS_TIMEOUT;
  return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
}

void PluginSqlContext::attach(seekdb_plugin_execution_context_v2_t &context)
{
  context.sql_api = sql_api();
  context.sql_context = reinterpret_cast<seekdb_plugin_sql_context_handle_t *>(this);
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL PluginSqlContext::poll_query(
    seekdb_plugin_sql_context_handle_t *opaque, seekdb_plugin_query_status_v1_t *output)
{
  if (!opaque) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &self = *reinterpret_cast<PluginSqlContext *>(opaque);
  if (self.thread_ != std::this_thread::get_id()) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  if (!output || output->struct_size < sizeof(*output)) {
    if (self.error_ == OB_SUCCESS) self.error_ = OB_INVALID_ARGUMENT;
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  *output = {}; output->struct_size = sizeof(*output);
  if (self.error_ == OB_SUCCESS) {
    try {
      if (self.executing_) self.error_ = OB_STATE_NOT_MATCH;
      else self.error_ = self.context_.check_status();
      if (self.error_ == OB_SUCCESS) {
        int64_t remaining = 0;
        const auto *plan = self.context_.get_physical_plan_ctx();
        if (!plan) self.error_ = OB_NOT_INIT;
        else if (plan->is_exec_timeout(&remaining)) self.error_ = OB_TIMEOUT;
        else output->remaining_us = plan->get_timeout_timestamp() > 0 ? std::max(int64_t{0}, remaining) : -1;
      }
    } catch (const std::bad_alloc &) { self.error_ = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) { self.error_ = OB_ERR_UNEXPECTED; }
  }
  output->database_error = self.error_;
  if (self.error_ == OB_SUCCESS) return SEEKDB_PLUGIN_STATUS_OK;
  output->remaining_us = 0;
  if (self.error_ == OB_TIMEOUT) return SEEKDB_PLUGIN_STATUS_TIMEOUT;
  if (self.error_ == OB_ALLOCATE_MEMORY_FAILED) return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  if (self.error_ == OB_INVALID_ARGUMENT) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
}

void PluginSqlContext::attach(seekdb_plugin_table_execution_context_v2_t &context)
{
  context.query_context = reinterpret_cast<seekdb_plugin_sql_context_handle_t *>(this);
  context.poll_query = poll_query;
}

void PluginSqlContext::attach(seekdb_plugin_table_execution_context_v3_t &context)
{
  attach(context.v2);
  context.sql_api = sql_api();
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL PluginSqlContext::execute(
    seekdb_plugin_sql_context_handle_t *opaque, const char *sql, uint64_t sql_size,
    const seekdb_plugin_sql_value_v1_t *parameters, uint32_t parameter_count,
    uint64_t max_rows, seekdb_plugin_sql_consume_row_v1_fn consume, void *consumer,
    seekdb_plugin_sql_result_v1_t *output)
{
  if (!opaque || !output || output->struct_size < sizeof(*output)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &self = *reinterpret_cast<PluginSqlContext *>(opaque);
  // Do not mutate shared state on a wrong-thread call.
  if (self.thread_ != std::this_thread::get_id()) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  *output = {};
  output->struct_size = sizeof(*output);
  if (self.executing_ || self.error_ != OB_SUCCESS) {
    if (self.error_ == OB_SUCCESS) self.error_ = OB_STATE_NOT_MATCH;
  } else if (!sql || sql_size == 0 || sql_size > SQL_BYTE_LIMIT
      || std::memchr(sql, '\0', sql_size) != nullptr
      || parameter_count > SEEKDB_PLUGIN_MAX_ARGUMENTS || (parameter_count && !parameters)) {
    self.error_ = OB_INVALID_ARGUMENT;
  } else {
    self.executing_ = true;
    int ret = OB_SUCCESS;
    try {
      // ParamStore's default ObWrapperAllocator has no backing allocator.
      // Keep parameter storage local to this synchronous callback; a query
      // may invoke a streaming plugin many times before its arena is reset.
      ObArenaAllocator parameter_allocator(ObMemAttr("PluginSqlParam"));
      ParamStore values((ObWrapperAllocator(parameter_allocator)));
      if (OB_FAIL(convert_parameters(parameters, parameter_count, values))) {
      } else {
        ret = run_sql(self.context_, ObString(static_cast<int32_t>(sql_size), sql),
                      values, max_rows, consume, consumer, *output);
      }
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
    self.executing_ = false;
    if (self.error_ == OB_SUCCESS) self.error_ = ret;
  }
  output->database_error = self.error_;
  if (self.error_ == OB_SUCCESS) return SEEKDB_PLUGIN_STATUS_OK;
  if (self.error_ == OB_ALLOCATE_MEMORY_FAILED) return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  if (self.error_ == OB_INVALID_ARGUMENT) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (self.error_ == OB_TIMEOUT) return SEEKDB_PLUGIN_STATUS_TIMEOUT;
  return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
}
} }
