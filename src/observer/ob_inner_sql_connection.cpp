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

#include "lib/allocator/ob_malloc.h"
#include "lib/objectpool/ob_resource_pool.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "ob_inner_sql_connection.h"
#include "query/session/ob_inner_sql_connection_access.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/engine/ob_physical_plan.h"
#include "storage/tx/ob_trans_service.h"
#include "share/ob_time_utility2.h"
#include "observer/ob_server.h"
#include "observer/ob_server_runtime_access.h"
#include "share/ob_structured_event_logger.h"
#include "observer/mysql/obmp_base.h"
#include "ob_inner_sql_read_context.h"
#include "storage/tablelock/ob_lock_inner_connection_util.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
using namespace share;
using namespace share::schema;
using namespace transaction::tablelock;

namespace observer
{

constexpr const char ObInnerSQLConnection::LABEL[];

class ObInnerSQLConnection::ObSqlQueryExecutor : public sqlclient::ObIExecutor
{
public:
  explicit ObSqlQueryExecutor(const ObString &sql) : sql_(sql) {}
  explicit ObSqlQueryExecutor(const char *sql) : sql_(ObString::make_string(sql)) {}

  virtual ~ObSqlQueryExecutor() {}

  virtual int execute(sql::ObSql &engine, sql::ObSqlCtx &ctx, sql::ObResultSet &res)
  {
    int ret = OB_SUCCESS;
    SQL_INFO_GUARD(sql_, ObString(OB_MAX_SQL_ID_LENGTH, ctx.sql_id_));
    // Deep copy sql, because sql may be destroyed before result iteration.
    const int64_t alloc_size = sizeof(ObString) + sql_.length() + 1; // 1 for C terminate char
    void *mem = res.get_mem_pool().alloc(alloc_size);
    if (NULL == mem) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory failed", K(ret));
    } else {
      ObString *dup_sql = new (mem) ObString(sql_.length(), sql_.length(),
                                             static_cast<char *>(mem) + sizeof(ObString));
      MEMCPY(dup_sql->ptr(), sql_.ptr(), sql_.length());
      dup_sql->ptr()[sql_.length()] = '\0';
      res.get_session().store_query_string(*dup_sql);
      ret = engine.stmt_query(*dup_sql, ctx, res);
    }
    return ret;
  }

  // process result after result open
  virtual int process_result(sql::ObResultSet &) override { return OB_SUCCESS; }

  INHERIT_TO_STRING_KV("ObIExecutor", ObIExecutor, K_(sql));

private:
  ObString sql_;
};

ObInnerSQLConnection::TimeoutGuard::TimeoutGuard(ObInnerSQLConnection &conn)
  : conn_(conn)
{
  int ret = OB_SUCCESS;
  worker_timeout_ = THIS_WORKER.get_timeout_ts();
  if (OB_FAIL(conn_.get_session().get_query_timeout(query_timeout_))
      || OB_FAIL(conn_.get_session().get_tx_timeout(trx_timeout_))) {
    LOG_ERROR("get timeout failed", KR(ret), K(query_timeout_), K(trx_timeout_));
  }
}

ObInnerSQLConnection::TimeoutGuard::~TimeoutGuard()
{
  int ret = OB_SUCCESS;
  if (THIS_WORKER.get_timeout_ts() != worker_timeout_) {
    THIS_WORKER.set_timeout_ts(worker_timeout_);
  }

  int64_t query_timeout = 0;
  int64_t trx_timeout = 0;
  if (OB_FAIL(conn_.get_session().get_query_timeout(query_timeout))
      || OB_FAIL(conn_.get_session().get_tx_timeout(trx_timeout))) {
    LOG_ERROR("get timeout failed", KR(ret), K(query_timeout), K(trx_timeout));
  } else {
    if (query_timeout != query_timeout_ || trx_timeout != trx_timeout_) {
      if (OB_FAIL(conn_.set_session_timeout(query_timeout_, trx_timeout_))) {
      }
    }
  }
}

ObInnerSQLConnection::ObInnerSQLConnection()
    : inited_(false), extern_session_(NULL), inner_session_(NULL),
      self_weak_guard_(),
      is_spi_conn_(false),
      ob_sql_(NULL), vt_iter_creator_(NULL),
      ref_ctx_(NULL),
      sql_modifier_(NULL),
      init_timestamp_(0),
      tid_(-1),
      bt_size_(0),
      bt_addrs_(),
      execute_start_timestamp_(0),
      execute_end_timestamp_(0),
      is_in_trans_(false),
      use_external_session_(false),
      group_id_(0),
      user_timeout_(0),
      inner_sess_query_locked_(false)
{
  free_session_ctx_.sessid_ = ObSQLSessionInfo::INVALID_SESSID;
}

ObInnerSQLConnection::~ObInnerSQLConnection()
{
  if (OB_NOT_NULL(inner_session_) && OB_NOT_NULL(inner_session_->get_tx_desc())) {
    if (OB_SUCCESS == share::check_server_runtime_ready()) {
      ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>()->release_tx(*inner_session_->get_tx_desc());
    }
    {
      ObSQLSessionInfo::LockGuard guard(inner_session_->get_thread_data_lock());
      inner_session_->get_tx_desc() = NULL;
    }
  }
}

int ObInnerSQLConnection::create_connection_with_owned_session(
    const bool use_static_engine,
    const int32_t group_id,
    sqlclient::ObISQLConnectionGuard &conn)
{
  return create_impl(NULL, use_static_engine, group_id,
                     false, conn);
}

int ObInnerSQLConnection::create_connection_with_external_session(
    ObSQLSessionInfo *session_info,
    sqlclient::ObISQLConnectionGuard &conn)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session is null", K(ret));
  } else {
    ret = create_impl(session_info, false, 0, false, conn);
  }
  return ret;
}

int ObInnerSQLConnection::create_spi_connection_with_external_session(
    ObSQLSessionInfo *session_info,
    sqlclient::ObISQLConnectionGuard &conn)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session is null", K(ret));
  } else {
    ret = create_impl(session_info, true, 0, true, conn);
  }
  return ret;
}

int ObInnerSQLConnection::create_impl(
    ObSQLSessionInfo *extern_session,
    const bool use_static_engine,
    const int32_t group_id,
    const bool use_spi_allocator,
    sqlclient::ObISQLConnectionGuard &conn)
{
  int ret = OB_SUCCESS;
  ObInnerSQLConnection *new_conn = NULL;
  conn.reset();
  sql::ObSql *sql_engine = get_observer_sql_engine();
  if (OB_ISNULL(sql_engine) || OB_ISNULL(::oceanbase::share::server_service<::oceanbase::observer::ObVTIterCreator>())) {
    ret = OB_NOT_INIT;
    LOG_WARN("inner sql dependency is null", K(ret), KP(sql_engine),
             KP(::oceanbase::share::server_service<::oceanbase::observer::ObVTIterCreator>()));
  } else if (use_spi_allocator) {
    if (OB_ISNULL(new_conn = rp_alloc(ObInnerSQLConnection,
                                      ObInnerSQLConnection::LABEL))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate spi inner sql connection failed", K(ret));
    }
  } else {
    void *mem = ob_malloc(sizeof(ObInnerSQLConnection),
                          ObMemAttr(ObModIds::OB_INNER_SQL_CONN));
    if (OB_ISNULL(mem)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate inner sql connection failed", K(ret));
    } else {
      new_conn = new (mem) ObInnerSQLConnection();
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(new_conn->init(sql_engine,
                                              ::oceanbase::share::server_service<::oceanbase::observer::ObVTIterCreator>(),
                                              extern_session,
                                              NULL,
                                              use_static_engine,
                                              group_id))) {
    LOG_WARN("init inner sql connection failed", K(ret));
  }

  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(new_conn)) {
      new_conn->is_spi_conn_ = use_spi_allocator;
      new_conn->free_self();
      new_conn = NULL;
    }
  } else {
    new_conn->is_spi_conn_ = use_spi_allocator;
    if (OB_FAIL(conn.assign(
            static_cast<sqlclient::ObISQLConnection *>(new_conn),
            [](sqlclient::ObISQLConnection *conn) {
              static_cast<ObInnerSQLConnection *>(conn)->free_self();
            }))) {
      LOG_WARN("create shared guard for inner sql connection failed", K(ret));
      new_conn->free_self();
      new_conn = NULL;
    } else {
      (void)new_conn->self_weak_guard_.assign(conn);
    }
  }
  return ret;
}

int ObInnerSQLConnection::init(ObSql *ob_sql,
                               ObVTIterCreator *vt_iter_creator,
                               sql::ObSQLSessionInfo *extern_session, /* = NULL */
                               ObRestoreSQLModifier *sql_modifier /* = NULL */,
                               const bool use_static_engine /* = false */,
                               const int32_t group_id /* = 0 */)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("connection init twice", K(ret));
  } else if (NULL == ob_sql || NULL == vt_iter_creator) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("inner sql dependency should not be NULL", K(ret),
        KP(ob_sql), KP(vt_iter_creator));
  } else {
    ob_sql_ = ob_sql;
    vt_iter_creator_ = vt_iter_creator;
    sql_modifier_ = sql_modifier;
    init_timestamp_ = ObTimeUtility::current_time();
    tid_ = GETTID();
    if (NULL == extern_session || 0 != EVENT_CALL(EventTable::EN_INNER_SQL_CONN_LEAK_CHECK)) {
      // Only backtrace internal used connection to avoid performance problems.
      bt_size_ = ob_backtrace(bt_addrs_, MAX_BT_SIZE);
    }
    if (OB_FAIL(init_session(extern_session, use_static_engine))) {
      LOG_WARN("init session failed", K(ret));
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = destroy_inner_session())) {
      }
    } else {
      group_id_ = group_id;
      inited_ = true;
    }
  }
  return ret;
}

int ObInnerSQLConnection::destroy()
{
  int ret = OB_SUCCESS;
  try_release_query_lock();
  // uninited connection can be destroy too
  if (inited_) {
    // continue execute while error happen.
    inited_ = false;
    if (OB_SUCC(ret) && OB_FAIL(destroy_inner_session())) {
      LOG_WARN("failed to destroy inner session when inner sql connection destroy", K(ret));
    }
    extern_session_ = NULL;
    ref_ctx_ = NULL;
    user_timeout_ = 0;
  }
  return ret;
}

void ObInnerSQLConnection::free_self()
{
  const bool use_spi_allocator = is_spi_conn_;
  const int ret = destroy();
  if (OB_SUCCESS != ret) {
    LOG_WARN_RET(ret, "destroy inner sql connection failed");
  }
  if (use_spi_allocator) {
    rp_free(this, ObInnerSQLConnection::LABEL);
  } else {
    this->~ObInnerSQLConnection();
    ob_free(this);
  }
}

int ObInnerSQLConnection::try_acquire_query_lock()
{
  int ret = OB_SUCCESS;
  if (!inner_sess_query_locked_) {
    if (OB_FAIL(inner_session_->get_query_lock().lock())) {
    } else {
      inner_sess_query_locked_ = true;
    }
  }
  return ret;
}

void ObInnerSQLConnection::try_release_query_lock()
{
  int ret = OB_SUCCESS;
  if (inner_sess_query_locked_) {
    if (OB_FAIL(inner_session_->get_query_lock().unlock())) {
    } else {
      inner_sess_query_locked_ = false;
    }
  }
}

int ObInnerSQLConnection::set_ddl_info(const void *ddl_info)
{
  int ret = OB_SUCCESS;
  sql::ObSQLSessionInfo &session = get_session();
  if (OB_ISNULL(ddl_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(ddl_info));
  } else {
    const ObSessionDDLInfo *tmp_ddl_info = reinterpret_cast<const ObSessionDDLInfo *>(ddl_info);
    session.set_ddl_info(*tmp_ddl_info);
  }
  return ret;
}

int ObInnerSQLConnection::set_tz_info_wrap(const ObTimeZoneInfoWrap &tz_info_wrap)
{
  int ret = OB_SUCCESS;
  sql::ObSQLSessionInfo &session = get_session();
  if (OB_FAIL(session.set_tz_info_wrap(tz_info_wrap))) {
  }
  return ret;
}

void ObInnerSQLConnection::set_is_load_data_exec(bool v)
{
  get_session().set_load_data_exec_session(v);
}

void ObInnerSQLConnection::set_ob_enable_pl_cache(bool v)
{
  get_session().set_local_ob_enable_pl_cache(v);
}
ERRSIM_POINT_DEF(NOT_SPEED_UP_INIT_SESSION_INFO);
int ObInnerSQLConnection::init_session_info(
    sql::ObSQLSessionInfo *session,
    const bool is_extern_session,
    const bool is_ddl)
{
  int ret = OB_SUCCESS;
  if (NULL == session) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fail to init session info, not pointer", K(ret), KPC(session));
  } else {
    // called in init(), can not check inited_ flag.
    const bool print_info_log = false;
    const bool use_server_defaults = true;
    ObPCMemPctConf pc_mem_conf;
    session->set_inner_session();
    ObObj mysql_sql_mode;
    mysql_sql_mode.set_uint(ObUInt64Type, DEFAULT_MYSQL_MODE);
    if (!NOT_SPEED_UP_INIT_SESSION_INFO && OB_FAIL(session->load_essential_sys_vars_only(print_info_log, use_server_defaults))) {
      LOG_WARN("session load default system variable failed", K(ret));
    } else if (NOT_SPEED_UP_INIT_SESSION_INFO && OB_FAIL(session->load_default_sys_variable(print_info_log, use_server_defaults))) {
      LOG_WARN("session load default system variable failed", K(ret));
    } else if (OB_FAIL(session->update_max_packet_size())) {
    } else if (OB_FAIL(session->init_runtime(OB_SERVER_RUNTIME_NAME))) {
    } else {
      if (!is_extern_session) { // if not exetern session
        if (OB_FAIL(session->set_user(OB_SYS_USER_NAME, OB_SYS_HOST_NAME, OB_SYS_USER_ID))) {
        } else {
          session->set_user_priv_set(OB_PRIV_ALL | OB_PRIV_GRANT);
          session->set_database_id(OB_SYS_DATABASE_ID);
          session->set_shadow(true); // inner session will not be show
          session->set_real_inner_session(true);
          session->set_current_trace_id(ObCurTraceId::get_trace_id());
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(session->update_sys_variable(
            SYS_VAR_SQL_MODE, mysql_sql_mode))) {
        } else {
          ObString database_name(OB_SYS_DATABASE_NAME);
          if (OB_FAIL(session->set_default_database(database_name))) {
          } else if (OB_FAIL(session->get_pc_mem_conf(pc_mem_conf))) {
          } else {
            session->set_database_id(OB_SYS_DATABASE_ID);
            //TODO shengle ?
            session->get_ddl_info().set_is_ddl(is_ddl);
            session->reset_timezone();
          }
        }
      }
    }
  }
  return ret;
}

int ObInnerSQLConnection::init_session(sql::ObSQLSessionInfo* extern_session, const bool is_ddl)
{
  int ret = OB_SUCCESS;
  if (NULL == extern_session) {
    const bool is_extern_session = false;
    const bool is_create_session_mgr = OB_NOT_NULL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>());
    if (is_create_session_mgr && is_inner_session_mgr_enable()) {
      if (OB_FAIL(create_session_by_mgr())) {
      }

      if (OB_FAIL(ret)) {
        if (OB_FAIL(create_default_session())) {
        } else {
          ret = OB_SUCCESS;
        }
      }
    } else {
      if (OB_FAIL(create_default_session())) {
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(init_session_info(inner_session_, is_extern_session, is_ddl))) {
      }
    }

    if (OB_SUCC(ret) && OB_FAIL(try_acquire_query_lock())) {
      LOG_WARN("failed to acquire inner session query lock", K(ret));
    }
  } else {
    extern_session_ = extern_session;
  }
  return ret;
}

int ObInnerSQLConnection::init_result(ObInnerSQLResult &res,
                                      ObVirtualTableIteratorFactory *vt_iter_factory,
                                      int64_t retry_cnt,
                                      ObSchemaGetterGuard &schema_guard,
                                      pl::ObPLBlockNS *secondary_namespace,
                                      bool is_prepare_protocol,
                                      bool is_prepare_stage,
                                      bool is_dynamic_sql,
                                      bool is_cursor)
{
  int ret = OB_SUCCESS;
  UNUSED(vt_iter_factory);
  sql::ObResultSet &result_set = res.result_set();
  const ObGlobalContext &gctx = ObServer::get_instance().get_gctx();
  result_set.get_exec_context().get_sql_exec_ctx().schema_service_ = gctx.schema_service_;
  result_set.get_exec_context().set_sql_ctx(&res.sql_ctx());
  res.sql_ctx().retry_times_ = retry_cnt;
  res.sql_ctx().session_info_ = &get_session();
  res.sql_ctx().disable_privilege_check_ = is_check_priv()
                                            ? PRIV_CHECK_FLAG_NORMAL : PRIV_CHECK_FLAG_DISABLE;
  res.sql_ctx().secondary_namespace_ = secondary_namespace;
  res.sql_ctx().is_prepare_protocol_ = is_prepare_protocol;
  res.sql_ctx().is_prepare_stage_ = is_prepare_stage;
  res.sql_ctx().is_dynamic_sql_ = is_dynamic_sql;
  res.sql_ctx().is_cursor_ = is_cursor;
  res.sql_ctx().schema_guard_ = &schema_guard;
  if (OB_FAIL(res.result_set().init())) {
  } else if (is_prepare_protocol
             && NULL == secondary_namespace
             && !is_dynamic_sql) {
    result_set.set_simple_ps_protocol();
  } else { /*do nothing*/ }
  return ret;
}

int ObInnerSQLConnection::process_retry(ObInnerSQLResult &res,
                                        int last_ret,
                                        int64_t abs_timeout_us,
                                        bool &need_retry,
                                        int64_t retry_cnt)
{
  UNUSED(abs_timeout_us);
  UNUSED(retry_cnt);
  int client_ret = OB_SUCCESS;
  bool force_local_retry = true;
  bool is_inner_sql = true;
  retry_ctrl_.test_and_save_retry_state(GCTX, res.sql_ctx(), res.result_set(),
                                        last_ret, client_ret,
                                        force_local_retry, is_inner_sql);
  need_retry = (ObQueryRetryType::RETRY_TYPE_LOCAL == retry_ctrl_.get_retry_type());
  return client_ret;
}

class ObInnerSQLTimeRecord : public ObITimeRecord
{
public:
  ObInnerSQLTimeRecord(sql::ObSQLSessionInfo &session)
    : session_(session),
      execute_start_timestamp_(0),
      execute_end_timestamp_(0) {}

  int64_t get_send_timestamp() const { return session_.get_query_start_time(); }
  int64_t get_receive_timestamp() const { return session_.get_query_start_time(); }
  int64_t get_enqueue_timestamp() const { return session_.get_query_start_time(); }
  int64_t get_run_timestamp() const { return session_.get_query_start_time(); }
  int64_t get_process_timestamp() const { return session_.get_query_start_time(); }
  int64_t get_single_process_timestamp() const { return session_.get_query_start_time(); }
  int64_t get_exec_start_timestamp() const { return execute_start_timestamp_; }
  int64_t get_exec_end_timestamp() const { return execute_end_timestamp_; }

  void set_execute_start_timestamp(int64_t v) { execute_start_timestamp_ = v; }
  void set_execute_end_timestamp(int64_t v) { execute_end_timestamp_ = v; }

private:
  sql::ObSQLSessionInfo &session_;
  int64_t execute_start_timestamp_;
  int64_t execute_end_timestamp_;
};

int ObInnerSQLConnection::process_record(sql::ObResultSet &result_set,
                                         sql::ObSqlCtx &sql_ctx,
                                         sql::ObSQLSessionInfo &session,
                                         ObITimeRecord &time_record,
                                         int last_ret,
                                         int64_t execution_id,
                                         int64_t ps_stmt_id,
                                         ObWaitEventDesc &max_wait_desc,
                                         ObWaitEventStat &total_wait_desc,
                                         ObExecRecord &exec_record,
                                         ObExecTimestamp &exec_timestamp,
                                         const ObString &ps_sql,
                                         bool is_from_pl,
                                         ObString *pl_exec_params)
{
  int ret = OB_SUCCESS;
  UNUSED(total_wait_desc);

  ObAuditRecordData &audit_record = session.get_raw_audit_record();

  // some statistics must be recorded for plan stat, even though sql audit disabled
  bool first_record = (1 == audit_record.try_cnt_);
  ObExecStatUtils::record_exec_timestamp(time_record, first_record, exec_timestamp);
  audit_record.exec_timestamp_ = exec_timestamp;
  audit_record.exec_timestamp_.update_stage_time();
  audit_record.plsql_exec_time_ = session.get_plsql_exec_time();
  audit_record.plsql_compile_time_ = session.get_plsql_compile_time();
  if (audit_record.pl_trace_id_.is_invalid() &&
        result_set.is_pl_stmt(result_set.get_stmt_type()) &&
        OB_NOT_NULL(ObCurTraceId::get_trace_id())) {
    audit_record.pl_trace_id_ = *ObCurTraceId::get_trace_id();
  }
  session.update_pure_sql_exec_time(audit_record.exec_timestamp_.elapsed_t_);

  {
    audit_record.stmt_type_ = result_set.get_stmt_type();
    exec_record.max_wait_event_ = max_wait_desc;
    exec_record.wait_time_end_ = total_wait_desc.time_waited_;
    exec_record.wait_count_end_ = total_wait_desc.total_waits_;
    audit_record.exec_record_ = exec_record;
    audit_record.update_event_stage_state();
  }
  ret = process_audit_record(result_set, sql_ctx, session, last_ret, execution_id,
            ps_stmt_id, ps_sql, is_from_pl);
  if (NULL != pl_exec_params) {
    audit_record.params_value_ = pl_exec_params->ptr();
    audit_record.params_value_len_ = pl_exec_params->length();
  }
  // memory allocated by temporary allocator needs to be set to NULL here
  {
    audit_record.params_value_ = NULL;
    audit_record.params_value_len_ = 0;
  }
  return ret;
}

int ObInnerSQLConnection::process_audit_record(sql::ObResultSet &result_set,
                                               sql::ObSqlCtx &sql_ctx,
                                               sql::ObSQLSessionInfo &session,
                                               int last_ret,
                                               int64_t execution_id,
                                               int64_t ps_stmt_id,
                                               const ObString &ps_sql,
                                               bool is_from_pl)
{
  int ret = OB_SUCCESS;

  ObAuditRecordData &audit_record = session.get_raw_audit_record();
    audit_record.try_cnt_++;
    ObPhysicalPlan *plan = result_set.get_physical_plan();
    audit_record.seq_ = 0;  //don't use now
    audit_record.status_ = (0 == last_ret || OB_ITER_END == last_ret)
        ? 0 : last_ret;

    audit_record.client_addr_ = session.get_peer_addr();
    audit_record.user_client_addr_ = session.get_user_client_addr();
    audit_record.user_group_ = 0;
    audit_record.execution_id_ = execution_id;
    audit_record.ps_stmt_id_ = ps_stmt_id;
    audit_record.ps_inner_stmt_id_ = ps_stmt_id;
    if (ps_sql.length() != 0) {
      audit_record.sql_ = const_cast<char *>(ps_sql.ptr());
      audit_record.sql_len_ = min(ps_sql.length(), session.get_query_record_size_limit());
    }
    MEMCPY(audit_record.sql_id_, sql_ctx.sql_id_, (int32_t)sizeof(audit_record.sql_id_));
    audit_record.affected_rows_ = result_set.get_affected_rows();
    audit_record.return_rows_ = result_set.get_return_rows();
    if (NULL != result_set.get_exec_context().get_sql_executor_ctx()) {
      audit_record.partition_cnt_ = result_set.get_exec_context()
                                                    .get_das_ctx()
                                                    .get_related_tablet_cnt();
    }

    if (NULL != result_set.get_physical_plan()) {
      audit_record.plan_type_ = result_set.get_physical_plan()->get_plan_type();
      audit_record.table_scan_ = result_set.get_physical_plan()->contain_table_scan();
      audit_record.plan_id_ = result_set.get_physical_plan()->get_plan_id();
      audit_record.plan_hash_ = result_set.get_physical_plan()->get_plan_hash_value();
    }

    audit_record.is_executor_rpc_ = false;
    audit_record.is_inner_sql_ = !is_from_pl;
    audit_record.is_hit_plan_cache_ = result_set.get_is_from_plan_cache();
    audit_record.is_multi_stmt_ = false; // whether it is multi sql

    ObIArray<ObTableRowCount> *table_row_count_list = NULL;
    ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(result_set.get_exec_context());
    if (NULL != plan_ctx) {
      audit_record.consistency_level_ = plan_ctx->get_consistency_level();
      audit_record.table_scan_stat_ = plan_ctx->get_table_scan_stat();
      table_row_count_list = &(plan_ctx->get_table_row_count_list());
    }

    //update v$sql statistics
    if (OB_SUCC(last_ret) && session.get_local_ob_enable_plan_cache()) {
      if (NULL != plan) {
        if (!(sql_ctx.self_add_plan_) && sql_ctx.plan_cache_hit_) {
          plan->update_plan_stat(audit_record,
                                false, // false mean not first update plan stat
                                table_row_count_list);
        } else if (sql_ctx.self_add_plan_ && !sql_ctx.plan_cache_hit_) {
          plan->update_plan_stat(audit_record,
                                true,
                                table_row_count_list);
        }
      }
    }
  return ret;
}

template <typename T>
int ObInnerSQLConnection::process_final(const T &sql,
                                        ObInnerSQLResult &res,
                                        int last_ret)
{
  int ret = OB_SUCCESS;
  UNUSED(res);
  {
    int64_t process_time = ObTimeUtility::current_time() - get_session().get_query_start_time();
    if (process_time > 1L * 1000 * 1000) {
      LOG_INFO("slow inner sql", K(last_ret), K(sql), K(process_time));
    }
  }
  return ret;
}

int ObInnerSQLConnection::do_query(sqlclient::ObIExecutor &executor, ObInnerSQLResult &res)
{
  int ret = OB_SUCCESS;
  WITH_CONTEXT(res.mem_context_) {
    // are there no restrictions on internal SQL such as refresh schema?
    // MEM_TRACKER_GUARD(CURRENT_CONTEXT);
    // restore has its own inner_sql_connection, sql_modifier is not null
    bool is_restore = NULL != sql_modifier_;
    res.sql_ctx().is_restore_ = is_restore;
    get_session().set_process_query_time(ObTimeUtility::current_time());
    if (!inited_) {
      ret = OB_NOT_INIT;
      LOG_WARN("not init", K(ret));
    } else if (OB_ISNULL(ob_sql_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("ob_sql_ is NULL", K(ret));
    } else if (OB_FAIL(executor.execute(*ob_sql_, res.sql_ctx(), res.result_set()))) {
    } else {
      ObSQLSessionInfo &session = res.result_set().get_session();
      if (OB_ISNULL(res.sql_ctx().schema_guard_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema guard is null");
      } else if (OB_FAIL(session.update_query_sensitive_system_variable(*(res.sql_ctx().schema_guard_)))) {
      } else if (OB_UNLIKELY(is_restore)
                 && OB_FAIL(sql_modifier_->modify(res.result_set()))) {
        LOG_WARN("fail modify sql", K(res.result_set().get_statement_name()), K(ret));
      } else if (res.sql_ctx().is_prepare_stage_) {
        // PS prepare stage: skip result set open — prepare only parses SQL
        // and populates ps cache, no physical plan is generated yet.
        // Opening the result set would attempt to execute/prefetch, which
        // fails with OB_NOT_INIT because there is no physical plan.
      } else if (OB_FAIL(res.open())) {
      }
    }
  }

  return ret;
}

int ObInnerSQLConnection::query(sqlclient::ObIExecutor &executor,
                                ObInnerSQLResult &res,
                                ObVirtualTableIteratorFactory *vt_iter_factory)
{
  int ret = OB_SUCCESS;
  ObExecRecord exec_record;
  ObExecTimestamp exec_timestamp;
  ObExecutingSqlStatRecord sqlstat_record;

  exec_timestamp.exec_type_ = sql::InnerSql;
  const ObGlobalContext &gctx = ObServer::get_instance().get_gctx();
  int64_t start_time = ObTimeUtility::current_time();
  get_session().set_query_start_time(start_time); //FIXME temporarily written like this
  get_session().set_trans_type(transaction::ObTxClass::SYS);
  int64_t abs_timeout_us = 0;
  int64_t execution_id = 0;
  const uint64_t* trace_id_val = ObCurTraceId::get();
  bool is_trace_id_init = true;
  ObQueryRetryInfo &retry_info = get_session().get_retry_info_for_update();
  if (0 == trace_id_val[0]) {
    is_trace_id_init = false;
    common::ObCurTraceId::init(observer::ObServer::get_instance().get_self());
  }

  // backup && restore worker/session timeout.
  TimeoutGuard timeout_guard(*this);

  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (NULL != ref_ctx_) {
    ret = OB_REF_NUM_NOT_ZERO;
    LOG_ERROR("connection still be referred by previous sql result, can not execute sql now",
              K(ret), K(executor));
  } else if (OB_FAIL(set_timeout(abs_timeout_us))) {
  } else if (OB_ISNULL(ob_sql_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid sql engine", K(ret), K(ob_sql_));
  } else if (OB_UNLIKELY(retry_info.is_inited())) {
    if (is_inner_session()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("retry info is inited", K(ret), K(retry_info), K(executor));
    }
  } else if (OB_FAIL(retry_info.init())) {
  }

  // Set the effective schema identity for this inner SQL connection.
  

  if (OB_SUCC(ret)) {
    SERVER_MODULE_SCOPE {
      execution_id = ob_sql_->get_execution_id();
      bool need_retry = true;
      retry_ctrl_.clear_state_before_each_retry(get_session().get_retry_info_for_update());
      retry_ctrl_.reset_retry_times();
      for (int64_t retry_cnt = 0; need_retry; ++retry_cnt) {
        need_retry = false;
        retry_info.clear_state_before_each_retry();
        res.set_is_read(true);
        if (retry_cnt > 0) { // reset result set
          bool is_user_sql = res.result_set().is_user_sql();
          res.~ObInnerSQLResult();
          new (&res) ObInnerSQLResult(
              get_session(), ob_sql_->get_plan_cache_access_service(),
              is_inner_session());
          if (OB_FAIL(res.init())) {
          } else {
            res.result_set().set_user_sql(is_user_sql);
            res.set_is_read(true);
          }
        }
        int64_t local_database_schema_version = -1;
        ObWaitEventDesc max_wait_desc;
        ObWaitEventStat total_wait_desc;
        ObInnerSQLTimeRecord time_record(get_session());
        const bool enable_sqlstat = get_session().is_sqlstat_enabled();
        {
          ObMaxWaitGuard max_wait_guard(&max_wait_desc);
          ObTotalWaitGuard total_wait_guard(&total_wait_desc);

          {
            exec_record.record_start();
          }

          if (enable_sqlstat) {
            sqlstat_record.record_sqlstat_start_value(
                ob_sql_->get_query_runtime_environment());
            sqlstat_record.set_is_in_retry(retry_cnt > 0);
            if (is_inner_session()) {
              get_session().sql_sess_record_sql_stat_start_value(sqlstat_record);
            }
          }

          

          if (OB_FAIL(ret)){
            // do nothing
          } else if (OB_FAIL(gctx.schema_service_->get_runtime_schema_guard(res.schema_guard_))) {
          } else if (OB_FAIL(init_result(res, vt_iter_factory, retry_cnt,
                                         res.schema_guard_, NULL, false, false))) {
          } else if (OB_FAIL(res.schema_guard_.get_schema_version(local_database_schema_version))) {
          } else {
            res.result_set().get_exec_context().get_sql_exec_ctx().set_query_begin_schema_version(local_database_schema_version);
          }

          int ret_code = OB_SUCCESS;
          if (OB_FAIL(ret)) {
            // do nothing
          } else if (OB_FAIL(SMART_CALL(do_query(executor, res)))) {
            ret_code = ret;
            LOG_WARN("execute failed", K(ret), K(executor), K(retry_cnt),
                K(local_database_schema_version));
            ret = process_retry(res, ret, abs_timeout_us, need_retry, retry_cnt);
            // moved here from ObInnerSQLConnection::do_query() -> ObInnerSQLResult::open().
            int close_ret = res.force_close();
            if (OB_SUCCESS != close_ret) {
            }
          } else if (retry_cnt > 0) {
            int64_t total_time_cost_us = (ObTimeUtility::current_time() - start_time);
            LOG_INFO("[OK] inner sql execute success after retry!", K(retry_cnt), K(total_time_cost_us));
          }
          get_session().set_session_in_retry(need_retry, ret_code);
          //Monitoring item statistics start
          execute_start_timestamp_ = (res.get_execute_start_ts() > 0)
                                      ? res.get_execute_start_ts()
                                      : ObTimeUtility::current_time();
          //Monitoring item statistics end
          execute_end_timestamp_ = (res.get_execute_end_ts() > 0)
                                    ? res.get_execute_end_ts()
                                    : ObTimeUtility::current_time();

          time_record.set_execute_start_timestamp(execute_start_timestamp_);
          time_record.set_execute_end_timestamp(execute_end_timestamp_);
          {
            exec_record.record_end();
          }
        }

        if (res.is_inited()) {
          ObString dummy_ps_sql;
          int record_ret = process_record(res.result_set(), res.sql_ctx(), get_session(),
                                time_record, ret, execution_id, OB_INVALID_ID,
                                max_wait_desc, total_wait_desc, exec_record, exec_timestamp, dummy_ps_sql);
          if (OB_SUCCESS != record_ret) {
          }

          if (enable_sqlstat) {
            sqlstat_record.record_sqlstat_end_value(
                ob_sql_->get_query_runtime_environment());
            sqlstat_record.set_rows_processed(res.result_set().get_affected_rows() + res.result_set().get_return_rows());
            sqlstat_record.set_partition_cnt(res.result_set().get_exec_context().get_das_ctx().get_related_tablet_cnt());
            sqlstat_record.set_is_plan_cache_hit(res.sql_ctx().plan_cache_hit_);
            sqlstat_record.move_to_sqlstat_cache(get_session(),
                                                ob_sql_->get_plan_cache(),
                                                ob_sql_->get_plan_cache_access_service(),
                                                res.sql_ctx().cur_sql_,
                                                res.result_set().get_physical_plan());
          }
        }

        if (res.is_inited()) {
          if (OB_SUCC(ret) && get_session().get_in_transaction()) {
            if (ObStmt::is_dml_write_stmt(res.result_set().get_stmt_type()) ||
                ObStmt::is_savepoint_stmt(res.result_set().get_stmt_type())) {
              get_session().set_has_exec_inner_dml(true);
            }
          }
        }
      }
    }
  }
  if (res.is_inited()) {
    int aret = process_final(executor, res, ret);
    if (OB_SUCCESS != aret) {
    }
  }

  if (false == is_trace_id_init) {
    common::ObCurTraceId::reset();
  }
  if (is_inner_session()) {
    retry_info.reset();
  }
  return ret;
}

template <typename T>
int ObInnerSQLConnection::execute_with_timeout(T function)
{
  int ret = OB_SUCCESS;
  int64_t abs_timeout_us = 0;
  get_session().set_query_start_time(ObTimeUtility::current_time());
  TimeoutGuard timeout_guard(*this);

  if (OB_FAIL(set_timeout(abs_timeout_us))) {
  } else if (OB_FAIL(function())) {
  }
  return ret;
}

int ObInnerSQLConnection::start_transaction(
    bool with_snap_shot /* = false */)
{
  int ret = OB_SUCCESS;
  auto function = [&]() { return start_transaction_inner(with_snap_shot); };
  if (OB_FAIL(execute_with_timeout(function))) {
  }
  return ret;
}


int ObInnerSQLConnection::start_transaction_inner(
    bool with_snap_shot /* = false */)
{
  int ret = OB_SUCCESS;
  ObString sql;
  if (with_snap_shot) {
    sql = ObString::make_string("START TRANSACTION WITH CONSISTENT SNAPSHOT");
  } else {
    sql = ObString::make_string("START TRANSACTION");
  }
  ObSqlQueryExecutor executor(sql);
  SMART_VAR(ObInnerSQLResult, res, get_session(),
            ob_sql_->get_plan_cache_access_service(), is_inner_session()) {
    if (!inited_) {
      ret = OB_NOT_INIT;
      LOG_WARN("connection not inited", K(ret));
    }
    if (OB_SUCC(ret)) {
      if (is_in_trans()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inner conn is already in trans", K(ret));
      } else if (OB_FAIL(res.init())) {
      } else {
        if (OB_FAIL(query(executor, res))) {
        } else if (OB_FAIL(res.close())) {
        }
      }
      if (OB_SUCC(ret)) {
        set_is_in_trans(true);
      }
    }
  }

  return ret;
}

int ObInnerSQLConnection::register_multi_data_source(
                                                     const transaction::ObTxDataSourceType type,
                                                     const char *buf,
                                                     const int64_t buf_len,
                                                     const transaction::ObRegisterMdsFlag & register_flag)
{
  int ret = OB_SUCCESS;
  transaction::ObTxDesc *tx_desc = nullptr;

  SMART_VAR(ObInnerSQLResult, res, get_session(),
            ob_sql_->get_plan_cache_access_service(), is_inner_session())
  {
    if (!inited_) {
      ret = OB_NOT_INIT;
      LOG_WARN("connection not inited", K(ret));
    }

    if (OB_SUCC(ret)) {
      if (!is_in_trans()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inner conn must be already in trans when register multi source data", K(ret));
      } else if (OB_FAIL(res.init())) {
      } else if (OB_ISNULL(tx_desc = get_session().get_tx_desc())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Invalid tx_desc", K(ret), K(type));
      } else {
        SERVER_MODULE_SCOPE
        {
          if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>()->register_mds_into_tx(*tx_desc,
                                                                         type,
                                                                         buf,
                                                                         buf_len,
                                                                         register_flag))) {
          } else if (OB_FAIL(res.close())) {
          }
        }
      }
    }
  }


  LOG_INFO("register mds in inner_sql_connection",
           KR(ret),
           KP(this),
           K(get_session().get_server_sid()),
           KPC(get_session().get_tx_desc()));
  return ret;
}

int ObInnerSQLConnection::rollback()
{
  int ret = OB_SUCCESS;
  ObSqlQueryExecutor executor("ROLLBACK");
  if (!is_in_trans()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("inner conn is not in trans", K(ret));
  } else {
    SMART_VAR(ObInnerSQLResult, res, get_session(),
              ob_sql_->get_plan_cache_access_service(), is_inner_session()) {
      if (!inited_) {
        ret = OB_NOT_INIT;
        LOG_WARN("connection not inited", K(ret));
      } else if (OB_FAIL(res.init())) {
      } else {
        if (OB_FAIL(query(executor, res))) {
        } else if (OB_FAIL(res.close())) {
        }
      }
    }
  }
  set_is_in_trans(false);
  return ret;
}

int ObInnerSQLConnection::commit()
{
  int ret = OB_SUCCESS;
  DEBUG_SYNC(BEFORE_INNER_SQL_COMMIT);
  ObSqlQueryExecutor executor("COMMIT");
  if (!is_in_trans()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("inner conn is not in trans", K(ret));
  } else {
    SMART_VAR(ObInnerSQLResult, res, get_session(),
              ob_sql_->get_plan_cache_access_service(), is_inner_session()) {
      if (!inited_) {
        ret = OB_NOT_INIT;
        LOG_WARN("connection not inited", K(ret));
      } else if (OB_FAIL(res.init())) {
      } else {
        if (OB_FAIL(query(executor, res))) {
        } else if (OB_FAIL(res.close())) {
        }
      }
    }
  }
  set_is_in_trans(false);
  return ret;
}

int ObInnerSQLConnection::execute_write(const ObString &sql,
    int64_t &affected_rows, bool is_user_sql)
{
  int ret = OB_SUCCESS;
  auto function = [&]() { return execute_write_inner(sql, affected_rows, is_user_sql); };
  if (OB_FAIL(execute_with_timeout(function))) {
  }
  return ret;
}

int ObInnerSQLConnection::execute_proc(ObIAllocator &allocator,
                                      ParamStore &params,
                                      ObString &sql,
                                      const share::schema::ObRoutineInfo &routine_info,
                                      const common::ObIArray<const pl::ObUserDefinedType *> &udts,
                                      const ObTimeZoneInfo *tz_info,
                                      ObObj *result,
                                      bool is_sql)
{
  UNUSEDx(allocator, params, sql, routine_info, udts, tz_info, result, is_sql);
  int ret = OB_SUCCESS;
  return ret;
}

int ObInnerSQLConnection::execute_write_inner(const ObString &sql,
    int64_t &affected_rows, bool is_user_sql)
{
  int ret = OB_SUCCESS;
  ObSqlQueryExecutor executor(sql);
  SMART_VAR(ObInnerSQLResult, res, get_session(),
            ob_sql_->get_plan_cache_access_service(), is_inner_session()) {
    if (!inited_) {
      ret = OB_NOT_INIT;
      LOG_WARN("connection not inited", K(ret));
    } else if (0 == sql.length() || NULL == sql.ptr()  || '\0' == *(sql.ptr())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(sql));
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(res.init())) {
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      res.result_set().set_user_sql(is_user_sql);
      if (OB_FAIL(query(executor, res))) {
      } else if (FALSE_IT(affected_rows = res.result_set().get_affected_rows())) {
      } else if (OB_FAIL(res.close())) {
      }
      if (get_session().get_ddl_info().is_ddl()) {
        SERVER_EVENT_ADD(
          "ddl", "local execute ddl inner sql",
          "trace_id", *ObCurTraceId::get_trace_id(),
          "ret", ret,
          "affected_rows", affected_rows,
          "start_ts", res.execute_start_ts_,
          "end_ts", res.execute_end_ts_);
      }
    }
#ifndef NDEBUG
    LOG_INFO("execute write sql", K(ret), K(affected_rows), K(sql), K(get_session().get_server_sid()));
#endif
  }

  return ret;
}

int ObInnerSQLConnection::execute_read(const ObString &sql,
                                       ObISQLClient::ReadResult &res,
                                       bool is_user_sql)
{
  int ret = OB_SUCCESS;
  auto function = [&]() {
    res.reuse();
    return execute_read_inner(sql, res, is_user_sql);
  };
  if (OB_FAIL(execute_with_timeout(function))) {
  }
  return ret;
}

int ObInnerSQLConnection::execute_read_inner(const ObString &sql,
                                             ObISQLClient::ReadResult &res,
                                             bool is_user_sql)
{
  int ret = OB_SUCCESS;
  ObInnerSQLReadContext *read_ctx = NULL;
  const static int64_t ctx_size = sizeof(ObInnerSQLReadContext);
  static_assert(ctx_size <= ObISQLClient::ReadResult::BUF_SIZE, "buffer not enough");
  ObSqlQueryExecutor executor(sql);

  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("connection not inited", K(ret));
  } else if (0 == sql.length() || NULL == sql.ptr()  || '\0' == *(sql.ptr())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(sql));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(res.create_handler(read_ctx, *this))) {
  } else if (OB_FAIL(read_ctx->get_result().init())) {
  } else {
    read_ctx->get_result().result_set().set_user_sql(is_user_sql);
    if (OB_FAIL(query(executor, read_ctx->get_result(),
                      &read_ctx->get_vt_iter_factory()))) {
    }
  }
  if (OB_SUCC(ret)) {
    ref_ctx_ = read_ctx;
  }
  return ret;
}

int ObInnerSQLConnection::execute(
    sqlclient::ObIExecutor &executor)
{
  int ret = OB_SUCCESS;
  SMART_VAR(ObInnerSQLResult, res, get_session(),
            ob_sql_->get_plan_cache_access_service(), is_inner_session()) {
    if (OB_FAIL(res.init())) {
    } else if (!inited_) {
      ret = OB_NOT_INIT;
      LOG_WARN("connection not inited", K(ret));
    } else if (OB_FAIL(query(executor, res))) {
    } else {
      SERVER_MODULE_SCOPE {
        WITH_CONTEXT(res.mem_context_) {
          if (OB_FAIL(executor.process_result(res.result_set()))) {
          } else {
            if (OB_FAIL(res.close())) {
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObInnerSQLConnection::set_timeout(int64_t &abs_timeout_us)
{
  int ret = OB_SUCCESS;
  const ObTimeoutCtx &ctx = ObTimeoutCtx::get_ctx();
  const int64_t now = ObTimeUtility::current_time();
  int64_t timeout = 0;
  int64_t trx_timeout = 0;
  abs_timeout_us = 0;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  }

  if (OB_SUCC(ret)) {
    if (THIS_WORKER.is_timeout()) {
      ret = OB_TIMEOUT;
      LOG_WARN("already timeout", K(ret), K(abs_timeout_us), K(now), K(THIS_WORKER.get_timeout_ts()));
    } else {
      if (THIS_WORKER.get_timeout_remain() < OB_MAX_USER_SPECIFIED_TIMEOUT) {
        timeout = THIS_WORKER.get_timeout_remain();
        abs_timeout_us = THIS_WORKER.get_timeout_ts();
        trx_timeout = timeout;
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (ctx.is_timeout_set()) {
      if (ctx.get_abs_timeout() < abs_timeout_us || 0 == abs_timeout_us) {
        abs_timeout_us = ctx.get_abs_timeout();
        timeout = ctx.get_timeout();
        trx_timeout = timeout;
      }
      if (ctx.is_trx_timeout_set()) {
        trx_timeout = ctx.get_trx_timeout_us();
      }
      if (timeout <= 0) {
        ret = OB_TIMEOUT;
        LOG_WARN("already timeout", K(ret), K(ctx), K(abs_timeout_us));
      }
    }
#if !defined(NDEBUG)
    LOG_DEBUG("set timeout according to time_ctx", K(timeout), K(trx_timeout), K(abs_timeout_us));
#endif
  }

  if (OB_SUCC(ret)) {
    if (0 == abs_timeout_us) {
      timeout = (user_timeout_ > 0) ? user_timeout_ : GCONF.internal_sql_execute_timeout;
      trx_timeout = timeout;
      abs_timeout_us = now + timeout;
    }
  }

  // no need to set session timeout for outer session if no timeout ctx
  if (OB_SUCC(ret)
      && (is_inner_session() || ctx.is_timeout_set() || ctx.is_trx_timeout_set())) {
    if (OB_FAIL(set_session_timeout(timeout, trx_timeout))) {
    } else {
      THIS_WORKER.set_timeout_ts(get_session().get_query_start_time() + timeout);
    }
  }
  return ret;
}

int ObInnerSQLConnection::set_session_timeout(int64_t query_timeout, int64_t trx_timeout)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret)) {
    ObObj val;
    val.set_int(query_timeout);
    if (OB_FAIL(get_session().update_sys_variable(SYS_VAR_OB_QUERY_TIMEOUT, val))) {
    }
  }
  if (OB_SUCC(ret)) {
    ObObj val;
    val.set_int(trx_timeout);
    if (OB_FAIL(get_session().update_sys_variable(SYS_VAR_OB_TRX_TIMEOUT, val))) {
    }
  }

  return ret;
}

void ObInnerSQLConnection::dump_conn_bt_info()
{
  const int64_t BUF_SIZE = (1LL << 10);
  char buf_bt[BUF_SIZE];
  buf_bt[0] = '\0';
  char buf_time[OB_MAX_TIMESTAMP_LENGTH];
  int64_t pos = 0;
  (void)ObTimeUtility2::usec_to_str(init_timestamp_, buf_time, OB_MAX_TIMESTAMP_LENGTH, pos);
  pos = 0;
  parray(buf_bt, BUF_SIZE, (int64_t*)*&bt_addrs_, bt_size_);
  LOG_WARN_RET(OB_SUCCESS, "dump inner sql connection backtrace", "tid", tid_, "init time", buf_time, "backtrace", buf_bt);
}

int ObInnerSQLConnection::get_session_variable(const ObString &name, int64_t &val)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (0 == name.case_compare("tx_isolation")) {
    // Isolation level is a varchar value
    ObObj obj;
    if (OB_FAIL(get_session().get_sys_variable_by_name(name, obj))) {
    } else {
      // varchar conversion to int
      val = transaction::ObTransIsolation::get_level(obj.get_string());
    }
  } else {
    ret = get_session().get_sys_variable_by_name(name, val);
  }
  return ret;
}

int ObInnerSQLConnection::set_session_variable(const ObString &name, int64_t val)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (0 == name.case_compare("tx_isolation")) {
    // Isolation level is a string
    ObObj obj;
    obj.set_varchar(transaction::ObTransIsolation::get_name(val));
    obj.set_collation_type(ObCharset::get_system_collation());
    if (OB_FAIL(get_session().update_sys_variable_by_name(name, obj))) {
    }
  } else if (OB_FAIL(get_session().update_sys_variable_by_name(name, val))) {
  } else if (0 == name.case_compare("ob_read_consistency")) {
    LOG_INFO("inner session use weak consitency", K(val), "inner_connection_p", this);
  }
  return ret;
}

int ObInnerSQLConnection::set_session_variable(const ObString &name, const ObString &val)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(get_session().update_sys_variable(name, val))) {
  }
  return ret;
}

// nested session and sql execute for foreign key.

int ObInnerSQLConnection::begin_nested_session(ObSQLSessionInfo::StmtSavedValue &saved_session,
                                               SavedValue &saved_conn, bool skip_cur_stmt_tables)
{
  int ret = OB_SUCCESS;
  if (!is_extern_session()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("connection is not extern session", K(ret));
  } else if (OB_FAIL(extern_session_->begin_nested_session(saved_session, skip_cur_stmt_tables))) {
  } else {
    saved_conn.read_context_ = ref_ctx_;
    saved_conn.execute_start_timestamp_ = execute_start_timestamp_;
    saved_conn.execute_end_timestamp_ = execute_end_timestamp_;
  }
  return ret;
}

int ObInnerSQLConnection::end_nested_session(ObSQLSessionInfo::StmtSavedValue &saved_session,
                                             SavedValue &saved_conn)
{
  int ret = OB_SUCCESS;
  if (!is_extern_session()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("connection is not extern session", K(ret));
  } else if (OB_FAIL(extern_session_->end_nested_session(saved_session))) {
  } else {
    ref_ctx_ = static_cast<ObInnerSQLReadContext *>(saved_conn.read_context_);
    execute_start_timestamp_ = saved_conn.execute_start_timestamp_;
    execute_end_timestamp_ = saved_conn.execute_end_timestamp_;
    saved_conn.reset();
  }
  return ret;
}

int ObInnerSQLConnection::create_session_by_mgr()
{
  int ret = OB_SUCCESS;
  uint32_t sid = sql::ObSQLSessionInfo::INVALID_SESSID;
  
  if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()) || OB_ISNULL(::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session_mgr_ or runtime_controller_ is NULL", K(ret));
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()->create_sessid(sid))) {
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()->create_session(sid, inner_session_))) {
    inner_session_ = NULL;
    LOG_WARN("create session failed", K(ret), K(sid));
  } else {
    free_session_ctx_.sessid_ = sid;
    
    inner_session_->set_session_state(QUERY_ACTIVE);
    free_session_ctx_.has_inc_active_num_ = true;
  }
  return ret;
}

int ObInnerSQLConnection::create_default_session()
{
  int ret = OB_SUCCESS;
  ObArenaAllocator *allocator = NULL;
  void *buf = ob_malloc(sizeof(ObSQLSessionInfo), ObModIds::OB_SQL_SESSION_SBLOCK);
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("default session buf allocated failed", K(ret));
  } else if (FALSE_IT(inner_session_ = new(buf) ObSQLSessionInfo())) {
  } else if (FALSE_IT(free_session_ctx_.sessid_ = INNER_SQL_SESS_ID)) {
  } else if (OB_FAIL(inner_session_->init(INNER_SQL_SESS_ID, allocator))) {
  }
  return ret;
}

bool ObInnerSQLConnection::is_inner_session_mgr_enable()
{
  bool bret = false;
  

  bret = GCONF._enable_inner_session_mgr;

  return bret;
}

int ObInnerSQLConnection::destroy_inner_session()
{
  int ret = OB_SUCCESS;
  LOG_DEBUG("begin destroying inner session", K(ret), KP(inner_session_), K(free_session_ctx_), K(lbt()));
  if (NULL != inner_session_) {
    try_release_query_lock();
    if (INNER_SQL_SESS_ID == free_session_ctx_.sessid_) {
      if (OB_NOT_NULL(ob_sql_)) {
        const int close_ret = inner_session_->close_all_ps_stmt(ob_sql_->get_ps_cache());
        if (OB_UNLIKELY(OB_SUCCESS != close_ret)) {
        }
      }
      inner_session_->set_session_sleep();
      inner_session_->~ObSQLSessionInfo();
      ob_free(inner_session_);
    } else if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("session mgr is null", K(ret));
    } else {
      inner_session_->set_session_sleep();
      ::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()->revert_session(inner_session_);
      ::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()->free_session(free_session_ctx_);
    }
    inner_session_ = NULL;
  }
  free_session_ctx_.sessid_ = ObSQLSessionInfo::INVALID_SESSID;
  
  return ret;
}

ObInnerSqlWaitGuard::ObInnerSqlWaitGuard(const bool is_inner_session,
    sql::ObSQLSessionInfo *inner_session)
    : wait_guard_(is_inner_session ? ObWaitEventIds::INNER_SQL_EXEC_WAIT : -1,
          0 /* timeout_ms */,
          ObLocalDiagnosticInfo::get_inner_sql_wait_type(),
          OB_ISNULL(inner_session) ? 0 : inner_session->get_server_sid())
{
}

ObInnerSQLSessionGuard::ObInnerSQLSessionGuard(sql::ObSQLSessionInfo *session)
  : last_session_(NULL)
{
  last_session_ = THIS_WORKER.get_session();
  THIS_WORKER.set_session(session);
}

ObInnerSQLSessionGuard::~ObInnerSQLSessionGuard()
{
  THIS_WORKER.set_session(last_session_);
}

} // end of namespace observer

namespace common
{

int create_inner_sql_connection_for_proxy(
    bool is_ddl,
    int32_t group_id,
    sqlclient::ObISQLConnectionGuard &conn)
{
  int ret = OB_SUCCESS;
  conn.reset();
  if (OB_FAIL(observer::ObInnerSQLConnection::create_connection_with_owned_session(
          is_ddl, group_id, conn))) {
  }
  return ret;
}

} // end of namespace common

namespace query
{

int ObInnerSQLConnectionAccess::create_connection_with_external_session(
    sql::ObSQLSessionInfo *session,
    common::sqlclient::ObISQLConnectionGuard &connection)
{
  return observer::ObInnerSQLConnection::create_connection_with_external_session(
      session, connection);
}

int ObInnerSQLConnectionAccess::create_spi_connection_with_external_session(
    sql::ObSQLSessionInfo *session,
    common::sqlclient::ObISQLConnectionGuard &connection)
{
  return observer::ObInnerSQLConnection::
      create_spi_connection_with_external_session(session, connection);
}

sql::ObSQLSessionInfo *ObInnerSQLConnectionAccess::get_session(
    common::sqlclient::ObISQLConnection *connection)
{
  observer::ObInnerSQLConnection *native_connection =
      static_cast<observer::ObInnerSQLConnection *>(connection);
  return nullptr == native_connection ? nullptr
                                      : &native_connection->get_session();
}

int ObInnerSQLConnectionAccess::lock_obj(
    const transaction::tablelock::ObLockObjRequest &request,
    common::sqlclient::ObISQLConnection *connection)
{
  return nullptr == connection
      ? common::OB_INVALID_ARGUMENT
      : transaction::tablelock::ObInnerConnectionLockUtil::lock_obj(
            request, connection);
}

int ObInnerSQLConnectionAccess::register_multi_data_source(
    common::sqlclient::ObISQLConnection *connection,
    const transaction::ObTxDataSourceType type,
    const char *buffer,
    const int64_t buffer_size)
{
  return register_multi_data_source(
      connection,
      type,
      buffer,
      buffer_size,
      transaction::ObRegisterMdsFlag());
}

int ObInnerSQLConnectionAccess::register_multi_data_source(
    common::sqlclient::ObISQLConnection *connection,
    const transaction::ObTxDataSourceType type,
    const char *buffer,
    const int64_t buffer_size,
    const transaction::ObRegisterMdsFlag &flag)
{
  return nullptr == connection
      ? common::OB_INVALID_ARGUMENT
      : static_cast<observer::ObInnerSQLConnection *>(connection)
            ->register_multi_data_source(type, buffer, buffer_size, flag);
}

} // end of namespace query
} // end of namespace oceanbase
