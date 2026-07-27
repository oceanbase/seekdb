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

#define USING_LOG_PREFIX SQL_SESSION

#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_sql_session_info.h"
#include "share/rc/ob_module_provider.h"
#include "storage/memtable/mvcc/ob_btree_iter_cache.h"
#include "storage/tx/ob_ts_mgr.h"
#include "rootserver/ob_local_ddl_serial_call.h"
#include "pl/ob_pl_package.h"
#include "observer/mysql/obmp_stmt_send_piece_data.h"
#include "observer/ob_server.h"
#include "sql/plan_cache/ob_ps_cache.h"
#include "sql/optimizer/stat/ob_opt_stat_manager.h" // for ObOptStatManager

using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::share;
using namespace oceanbase::pl;
using namespace oceanbase::obmysql;
using namespace oceanbase::observer;

const char *state_str[] =
{
  "INIT",
  "SLEEP",
  "ACTIVE",
  "QUERY_KILLED",
  "SESSION_KILLED",
};

static int create_tmp_sys_var(oceanbase::share::ObSysVarClassType sys_var_id,
                              ObBasicSysVar *&sys_var,
                              ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObBasicSysVar *sys_var_ptr = nullptr;
  if (OB_FAIL(ObSysVarFactory::create_sys_var(allocator, sys_var_id, sys_var_ptr))) {
    LOG_WARN("failed to create a temporary system variable", K(ret), K(sys_var_id));
  } else if (OB_ISNULL(sys_var_ptr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("system variable factory returned a null variable", K(ret), K(sys_var_id));
  } else {
    sys_var = sys_var_ptr;
  }
  if (OB_FAIL(ret) && nullptr != sys_var_ptr) {
    sys_var_ptr->~ObBasicSysVar();
    sys_var_ptr = nullptr;
  }
  return ret;
}

void ObCachedSchemaGuardInfo::reset()
{
  schema_guard_.reset();
  ref_ts_ = 0;
  schema_version_ = 0;
}

int ObCachedSchemaGuardInfo::refresh_runtime_schema_guard()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(OBSERVER.get_gctx().schema_service_->get_runtime_schema_guard(schema_guard_))) {
    LOG_WARN("get schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard_.get_schema_version(schema_version_))) {
    LOG_WARN("fail get schema version", K(ret));
  } else {
    ref_ts_ = ObClockGenerator::getClock();
  }

  return ret;
}

void ObCachedSchemaGuardInfo::try_revert_schema_guard()
{
  if (schema_guard_.is_inited()) {
    const int64_t MAX_SCHEMA_GUARD_CACHED_TIME = 10 * 1000 * 1000;
    if (ObClockGenerator::getClock() - ref_ts_ > MAX_SCHEMA_GUARD_CACHED_TIME) {
      LOG_DEBUG("revert schema guard success by sql",
               "session_id", schema_guard_.get_session_id(),
               K_(schema_version));
      reset();
    }
  }
}

ObSQLSessionInfo::ObSQLSessionInfo() :
      ObVersionProvider(),
      ObBasicSessionInfo{},
      is_inited_(false),
      warnings_buf_(),
      show_warnings_buf_(),
      end_trans_cb_(),
      user_priv_set_(),
      db_priv_set_(),
      curr_trans_start_time_(0),
      curr_trans_last_stmt_time_(0),
      sess_create_time_(0),
      has_temp_table_flag_(false),
      has_accessed_session_level_temp_table_(false),
      enable_early_lock_release_(false),
      is_for_trigger_package_(false),
      trans_type_(transaction::ObTxClass::USER),
      version_provider_(NULL),
      config_provider_(NULL),
      plan_cache_(NULL),
      ps_cache_(NULL),
      found_rows_(1),
      affected_rows_(-1),
      global_sessid_(0),
      read_uncommited_(false),
      trace_recorder_(NULL),
      inner_flag_(false),
      is_max_availability_mode_(false),
      next_client_ps_stmt_id_(0),
      session_type_(INVALID_TYPE),
      pl_context_(NULL),
      pl_can_retry_(true),
      plsql_exec_time_(0),
      plsql_compile_time_(0),
      pl_query_sender_(NULL),
      pl_ps_protocol_(false),
      inner_conn_(NULL),
      enable_role_array_(),
      in_definer_named_proc_(false),
      priv_user_id_(OB_INVALID_ID),
      cached_runtime_config_info_(this),
      prelock_(false),
      is_ignore_stmt_(false),
      ddl_info_(),
      is_table_name_hidden_(false),
      piece_cache_(NULL),
      is_load_data_exec_session_(false),
      pl_exact_err_msg_(),
      is_varparams_sql_prepare_(false),
      got_server_conn_res_(false),
      got_user_conn_res_(false),
      conn_res_user_id_(OB_INVALID_ID),
      cur_exec_ctx_(nullptr),
      in_bytes_(0),
      out_bytes_(0),
      job_info_(nullptr),
      btree_iter_cache_(nullptr),
      executing_sql_stat_record_()
{
}

ObSQLSessionInfo::~ObSQLSessionInfo()
{
  plan_cache_ = NULL;
  destroy(false);
}

int ObSQLSessionInfo::init(uint32_t sessid,
    common::ObIAllocator *bucket_allocator, const ObTZInfoMap *tz_info)
{
  int ret = OB_SUCCESS;
  static const int64_t PS_BUCKET_NUM = 64;
  if (OB_FAIL(ObBasicSessionInfo::init(sessid, bucket_allocator, tz_info))) {
    LOG_WARN("fail to init basic session info", K(ret));
  } else if (!is_acquire_from_pool() &&
             OB_FAIL(package_state_map_.create(hash::cal_next_prime(4),
                                               ObMemAttr("PackStateMap")))) {
    LOG_WARN("create package state map failed", K(ret));
  } else {
    sess_create_time_ = ObTimeUtility::current_time();
    is_inited_ = true;
    if (OB_ISNULL(btree_iter_cache_)) {
      void *buf = get_session_allocator().alloc(sizeof(memtable::ObBtreeIterCache));
      if (OB_NOT_NULL(buf)) {
        btree_iter_cache_ = new (buf) memtable::ObBtreeIterCache();
      }
    }
  }
  if (OB_FAIL(ret)) {
    package_state_map_.clear();
    sock_fd_map_.clear();
  }
  return ret;
}

//for test
int ObSQLSessionInfo::test_init(uint32_t version, uint32_t sessid,
    common::ObIAllocator *bucket_allocator)
{
  int ret = OB_SUCCESS;
  UNUSED(version);
  if (OB_FAIL(ObBasicSessionInfo::test_init(sessid, bucket_allocator))) {
    LOG_WARN("fail to init basic session info", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObSQLSessionInfo::reset(bool skip_sys_var)
{
  if (is_inited_) {
    // ObVersionProvider::reset();
    warnings_buf_.reset();
    show_warnings_buf_.reset();
    end_trans_cb_.reset(),
    audit_record_.reset();
    user_priv_set_ = 0;
    db_priv_set_ = 0;
    curr_trans_start_time_ = 0;
    curr_trans_last_stmt_time_ = 0;
    sess_create_time_ = 0;
    has_temp_table_flag_ = false;
    has_accessed_session_level_temp_table_ = false;
    is_for_trigger_package_ = false;
    trans_type_ = transaction::ObTxClass::USER;
    version_provider_ = NULL;
    config_provider_ = NULL;
    ps_cache_ = NULL;
    found_rows_ = 1;
    affected_rows_ = -1;
    global_sessid_ = 0;
    read_uncommited_ = false;
    trace_recorder_ = NULL;
    inner_flag_ = false;
    is_max_availability_mode_ = false;
    enable_early_lock_release_ = false;
    ps_session_info_map_.reuse();
    ps_name_id_map_.reuse();
    in_use_ps_stmt_id_set_.reuse();
    next_client_ps_stmt_id_ = 0;
    session_type_ = INVALID_TYPE;
    package_state_map_.reuse();
    sock_fd_map_.reuse();
    pl_context_ = NULL;
    pl_can_retry_ = true;
    plsql_exec_time_ = 0;
    plsql_compile_time_ = 0;
    pl_query_sender_ = NULL;
    pl_ps_protocol_ = false;
    if (pl_cursor_cache_.is_inited()) {
      // when select GV$OPEN_CURSOR, we will add get_thread_data_lock to fetch pl_cursor_map_
      // so we need get_thread_data_lock there
      ObSQLSessionInfo::LockGuard lock_guard(get_thread_data_lock());
      pl_cursor_cache_.reset();
    }
    inner_conn_ = NULL;
    session_stat_.reset();
    cached_schema_guard_info_.reset();
    enable_role_array_.reset();
    in_definer_named_proc_ = false;
    priv_user_id_ = OB_INVALID_ID;
    prelock_ = false;
    ddl_info_.reset();
    cur_exec_ctx_ = nullptr;
    plan_cache_ = NULL;
    client_app_info_.reset();
    int temp_ret = OB_SUCCESS;
    optimizer_tracer_.reset();
    //call at last time
    ObBasicSessionInfo::reset(skip_sys_var);
  }
  in_bytes_ = 0;
  out_bytes_ = 0;
  is_lock_session_ = false;
  job_info_ = nullptr;
  executing_sql_stat_record_.reset();
}

void ObSQLSessionInfo::clean_status()
{
  ObBasicSessionInfo::clean_status();
}

int ObSQLSessionInfo::is_force_temp_table_inline(bool &force_inline) const
{
  int ret = OB_SUCCESS;
  int64_t with_subquery_policy = 0;
  force_inline = false;
  
  {
    int64_t with_subquery_policy = GCONF._with_subquery;
    if (2 == with_subquery_policy) {
      force_inline = true;
    }
  }
  return ret;
}

int ObSQLSessionInfo::is_force_temp_table_materialize(bool &force_materialize) const
{
  int ret = OB_SUCCESS;
  int64_t with_subquery_policy = 0;
  force_materialize = false;
  
  {
    int64_t with_subquery_policy = GCONF._with_subquery;
    if (1 == with_subquery_policy) {
      force_materialize = true;
    }
  }
  return ret;
}

int ObSQLSessionInfo::is_groupby_placement_transformation_enabled(bool &transformation_enabled) const
{
  int ret = OB_SUCCESS;
  transformation_enabled = false;
  
  {
    transformation_enabled = GCONF._optimizer_group_by_placement;
  }
  return ret;
}

bool ObSQLSessionInfo::is_in_range_optimization_enabled() const
{
  bool bret = false;
  
  {
    bret = GCONF._enable_in_range_optimization;
  }
  return bret;
}

int64_t ObSQLSessionInfo::get_inlist_rewrite_threshold() const
{
  int64_t threshold = 1000;
  
  {
    threshold = GCONF._inlist_rewrite_threshold;
  }
  return threshold;
}

int ObSQLSessionInfo::is_better_inlist_enabled(bool &enabled) const
{
  int ret = OB_SUCCESS;
  enabled = false;
  
  {
    enabled = GCONF._optimizer_better_inlist_costing;
  }
  return ret;
}

int ObSQLSessionInfo::is_preserve_order_for_pagination_enabled(bool &enabled) const
{
  int ret = OB_SUCCESS;
  enabled = false;
  
  {
    enabled = GCONF._preserve_order_for_pagination;
  }
  return ret;
}

int ObSQLSessionInfo::is_preserve_order_for_groupby_enabled(bool &enabled) const
{
  int ret = OB_SUCCESS;
  enabled = false;
  
  {
    enabled = GCONF._preserve_order_for_groupby;
  }
  return ret;
}

bool ObSQLSessionInfo::is_pl_prepare_stage() const
{
  bool bret = false;
  if (OB_NOT_NULL(cur_exec_ctx_) && OB_NOT_NULL(cur_exec_ctx_->get_sql_ctx())) {
    bret = cur_exec_ctx_->get_sql_ctx()->is_prepare_stage_;
  }
  return bret;
}

bool ObSQLSessionInfo::is_qualify_filter_enabled() const
{
  bool bret = false;
  
  {
    bret = GCONF._enable_optimizer_qualify_filter;
  }
  return bret;
}

int ObSQLSessionInfo::is_enable_range_extraction_for_not_in(bool &enabled) const
{
  int ret = OB_SUCCESS;
  enabled = true;
  
  {
    enabled = GCONF._enable_range_extraction_for_not_in;
  }
  return ret;
}

bool ObSQLSessionInfo::is_var_assign_use_das_enabled() const
{
  bool bret = false;
  
  {
    bret = GCONF._enable_var_assign_use_das;
  }
  return bret;
}

int ObSQLSessionInfo::is_adj_index_cost_enabled(bool &enabled, int64_t &stats_cost_percent) const
{
  int ret = OB_SUCCESS;
  enabled = false;
  stats_cost_percent = 0;
  
  {
    stats_cost_percent = GCONF.optimizer_index_cost_adj;
    enabled = (0 != stats_cost_percent);
  }
  return ret;
}

//to control subplan filter and multiple level join group rescan
bool ObSQLSessionInfo::is_spf_mlj_group_rescan_enabled() const
{
  bool bret = false;
  
  {
    bret = GCONF._enable_spf_batch_rescan;
  }
  return bret;
}

bool ObSQLSessionInfo::enable_parallel_das_dml() const
{
  bool bret = false;
  
  {
    bret = GCONF._enable_parallel_das_dml;
  }
  return bret;
}

bool ObSQLSessionInfo::is_sqlstat_enabled()
{
  bool bret = false;
  bret = get_ob_sqlstat_enable();
  return bret;
}

void ObSQLSessionInfo::destroy(bool skip_sys_var)
{
  if (is_inited_) {
    int ret = OB_SUCCESS;
    // The deserialized session should not do end_trans etc cleanup work
    // bug: 
    if (false == get_is_deserialized()) {
      {
        // session disconnects, call ObTransService::end_trans to roll back the transaction,
        // Here stmt_timeout = current time + statement query timeout, not the start_time of the last sql, related bug_id : 7961445
        set_query_start_time(ObTimeUtility::current_time());
        // Here calling end_trans does not require locking, because calling reclaim_value means there is no query concurrently using the session
        // Call this function before session.set_session_state(SESSION_KILLED),
        bool need_disconnect = false;
        // NOTE: only rollback trans if it is started on this node
        // otherwise the transaction maybe rollbacked by idle session disconnect
        if (is_in_transaction() && (tx_desc_->get_session_id() == get_server_sid())) {
          transaction::ObTransID tx_id = get_tx_id();
          if (OB_SUCCESS == share::check_server_runtime_ready()) {
            if (OB_FAIL(ObSqlTransControl::rollback_trans(this, need_disconnect))) {
              LOG_WARN("fail to rollback transaction", K(get_server_sid()), K(ret));
            } else if (false == inner_flag_) {
              LOG_INFO("end trans successfully",
                       "sessid", get_server_sid(),
                       "trans id", tx_id);
            }
          } else {
            LOG_WARN("server runtime is not ready", K(tx_id));
          }
        }
      }
    }
    // Temporary table cannot be cleaned up when the slave session is destructed
    if (false == get_is_deserialized()) {
      int temp_ret = drop_temp_tables();
      if (OB_UNLIKELY(OB_SUCCESS != temp_ret)) {
        LOG_WARN("fail to drop temp tables", K(temp_ret));
      }
    }
    // slave session ps_session_info_map_ is empty, calling close will have no side effects
    if (OB_SUCC(ret)) {
      if (OB_FAIL(close_all_ps_stmt())) {
        LOG_WARN("failed to close all stmt", K(ret));
      }
    }

    //close all cursor
    if (pl_cursor_cache_.is_inited()) {
      int temp_ret = pl_cursor_cache_.close_all(*this);
      if (temp_ret != OB_SUCCESS) {
        LOG_WARN("failed to close all cursor", K(ret));
      }
    }

    if (NULL != piece_cache_) {
      int temp_ret = piece_cache_->close_all(*this);
      if (temp_ret != OB_SUCCESS) {
        LOG_WARN("failed to close all piece", K(ret));
      }
      piece_cache_->~ObPieceCache();
      get_session_allocator().free(piece_cache_);
      piece_cache_ = NULL;
    }
    // Non-distributed needs it, distributed also needs it, used for cleaning up the global variable values of package
    reset_all_package_state();
    if (OB_NOT_NULL(btree_iter_cache_)) {
      btree_iter_cache_->destroy();
      btree_iter_cache_->~ObBtreeIterCache();
      get_session_allocator().free(btree_iter_cache_);
      btree_iter_cache_ = nullptr;
    }
    reset(skip_sys_var);
    is_inited_ = false;
  }
}

int ObSQLSessionInfo::close_ps_stmt(ObPsStmtId client_stmt_id)
{
  int ret = OB_SUCCESS;
  ObPsSessionInfo *ps_sess_info = NULL;
  if (OB_FAIL(get_ps_session_info(client_stmt_id, ps_sess_info))) {
    LOG_WARN("fail to get ps session info", K(client_stmt_id), "session_id", get_server_sid(), K(ret));
  } else if (OB_ISNULL(ps_sess_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ps session info is null", K(client_stmt_id), "session_id", get_server_sid(), K(ret));
  } else {
    ObPsStmtId inner_stmt_id = ps_sess_info->get_inner_stmt_id();
    ps_sess_info->dec_ref_count();
    if (ps_sess_info->need_erase()) {
      if (OB_ISNULL(ps_cache_)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("ps cache is null", K(ret));
      } else if (OB_FAIL(ps_cache_->deref_ps_stmt(inner_stmt_id))) {
        LOG_WARN("close ps stmt failed", K(ret), "session_id", get_server_sid(), K(ret));
      }
      // Regardless of whether the above was successful, the session info resource needs to be released
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = remove_ps_session_info(client_stmt_id))) {
        ret = tmp_ret;
        LOG_WARN("remove ps session info failed", K(client_stmt_id),
                  "session_id", get_server_sid(), K(ret));
      }
      LOG_TRACE("close ps stmt", K(ret), K(client_stmt_id), K(inner_stmt_id), K(lbt()));
    }
  }
  return ret;
}

int ObSQLSessionInfo::close_all_ps_stmt()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ps_cache_)) {
    // do nothing, session no ps
  } else if (!ps_session_info_map_.created()) {
    // do nothing, no ps added to map
  } else {
    PsSessionInfoMap::iterator iter = ps_session_info_map_.begin();
    ObPsStmtId inner_stmt_id = OB_INVALID_ID;
    for (; iter != ps_session_info_map_.end(); ++iter) { //ignore ret
      const ObPsStmtId client_stmt_id = iter->first;
      if (OB_FAIL(get_inner_ps_stmt_id(client_stmt_id, inner_stmt_id))) {
        LOG_WARN("get_inner_ps_stmt_id failed", K(ret), K(client_stmt_id), K(inner_stmt_id));
      } else if (OB_FAIL(ps_cache_->deref_ps_stmt(inner_stmt_id))) {
        LOG_WARN("close ps stmt failed", K(ret), K(client_stmt_id), K(inner_stmt_id));
      } else if (OB_ISNULL(iter->second)) {
        // do nothing
      } else {
        iter->second->~ObPsSessionInfo();
        ps_session_info_allocator_.free(iter->second);
        iter->second = NULL;
      }
    }
    ps_session_info_allocator_.reset();
    ps_session_info_map_.reuse();
  }
  return ret;
}
// If the session created temporary tables in direct connection mode, drop them when
// the session disconnects. Commit-time cleanup only clears transaction-level
// temporary tables; disconnect cleanup handles both transaction-level and
// session-level temporary tables. To avoid RS congestion, cleanup is executed by
// SQL proxy for this session's temporary table data.
// For distributed planning, unless ac=1 otherwise hand over to master session for cleanup, deserialized session does nothing
int ObSQLSessionInfo::drop_temp_tables(const bool is_disconn,
                                       const bool is_reset_connection)
{
  int ret = OB_SUCCESS;
  bool ac = false;
  bool is_sess_disconn = is_disconn;
  if (OB_FAIL(get_autocommit(ac))) {
    LOG_WARN("get autocommit error", K(ret), K(ac));
  } else if (!(is_inner() && !is_user_session())
             && (get_has_temp_table_flag()
                 || has_accessed_session_level_temp_table()
                 || has_tx_level_temp_table())
             && (!get_is_deserialized() || ac)) {
    bool need_drop_temp_table = false;
    // Cleanup is needed on direct connection disconnect or reset connection.
    if (OB_SUCC(ret)) {
      if (is_sess_disconn || is_reset_connection) {
        need_drop_temp_table = true;
      }
    }
    if (need_drop_temp_table) {
      LOG_DEBUG("need_drop_temp_table",
               K(get_current_query_string()),
               K(1UL),
               K(1UL),
               K(lbt()));
      obcall::ObDDLRes res;
      obcall::ObDropTableArg drop_table_arg;
      drop_table_arg.if_exist_ = true;
      drop_table_arg.to_recyclebin_ = false;
      drop_table_arg.table_type_ = share::schema::TMP_TABLE;
      drop_table_arg.session_id_ = get_sessid_for_table();
      
      
      
        LOG_INFO("temporary tables dropped due to connection disconnected", K(is_sess_disconn), K(drop_table_arg));
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("fail to drop temp tables", K(ret),
             K(1UL), K(get_server_sid()),
             K(has_accessed_session_level_temp_table()),
             K(lbt()));
  }
  return ret;
}
void ObSQLSessionInfo::set_show_warnings_buf(int error_code)
{
  // if error message didn't insert into THREAD warning buffer,
  //    insert it into SESSION warning buffer
  // if no error at all,
  //    clear err.
  if (OB_SUCCESS != error_code && strlen(warnings_buf_.get_err_msg()) <= 0) {
    warnings_buf_.set_error(ob_errpkt_strerror(error_code), error_code);
  } else if (OB_SUCCESS == error_code) {
    warnings_buf_.reset_err();
  }
  show_warnings_buf_ = warnings_buf_; // show_warnings_buf_ used for show warnings
}

void ObSQLSessionInfo::update_show_warnings_buf()
{
  for (int64_t i = 0; i < warnings_buf_.get_readable_warning_count(); i++) {
    const ObWarningBuffer::WarningItem *item = warnings_buf_.get_warning_item(i);
    if (OB_ISNULL(item)) {
    } else if (item->log_level_ == common::ObLogger::UserMsgLevel::USER_WARN) {
      show_warnings_buf_.append_warning(item->msg_, item->code_);
    } else if (item->log_level_ == common::ObLogger::UserMsgLevel::USER_NOTE) {
      show_warnings_buf_.append_note(item->msg_, item->code_);
    }
  }
}

int ObSQLSessionInfo::get_session_priv_info(share::schema::ObSessionPrivInfo &session_priv) const
{
  int ret = OB_SUCCESS;
  
  session_priv.user_id_ = get_priv_user_id();
  session_priv.user_name_ = get_user_name();
  session_priv.host_name_ = get_host_name();
  session_priv.db_ = get_database_name();
  session_priv.user_priv_set_ = user_priv_set_;
  session_priv.db_priv_set_ = db_priv_set_;
  return ret;
}

ObPlanCache *ObSQLSessionInfo::get_plan_cache()
{
  if (OB_NOT_NULL(plan_cache_)) {
    // do nothing
  } else {
    //release old plancache and get new
    ObPCMemPctConf pc_mem_conf;
    if (OB_SUCCESS != get_pc_mem_conf(pc_mem_conf)) {
      LOG_ERROR_RET(OB_ERR_UNEXPECTED, "fail to get pc mem conf");
      plan_cache_ = NULL;
    } else {
      plan_cache_ = share::g_mp->plan_cache();
      if (OB_ISNULL(plan_cache_)) {
        LOG_WARN_RET(OB_ERR_UNEXPECTED, "failed to get plan cache");
      } else if (plan_cache_->is_inited()) {
        // skip update mem conf
      } else if (OB_SUCCESS != plan_cache_->set_mem_conf(pc_mem_conf)) {
        LOG_ERROR_RET(OB_ERR_UNEXPECTED, "fail to set plan cache memory conf");
      }
    }
  }
  return plan_cache_;
}

ObPsCache *ObSQLSessionInfo::get_ps_cache()
{
  if (OB_NOT_NULL(ps_cache_)) {
    //do nothing
  } else {
    int ret = OB_SUCCESS;
    
    ObPCMemPctConf pc_mem_conf;
    ObMemAttr mem_attr;
    mem_attr.label_ = "PsSessionInfo";
    
    mem_attr.ctx_id_ = ObCtxIds::DEFAULT_CTX_ID;
    if (OB_FAIL(get_pc_mem_conf(pc_mem_conf))) {
      LOG_ERROR("failed to get pc mem conf");
      ps_cache_ = NULL;
    } else {
      ps_cache_ = share::g_mp->ps_cache();
      if (OB_ISNULL(ps_cache_)) {
        // ignore ret
        LOG_WARN("failed to get ps cache");
      } else if (!ps_cache_->is_inited() &&
                  OB_FAIL(ps_cache_->init(common::calculate_scaled_value_by_memory(common::OB_PLAN_CACHE_BUCKET_NUMBER_MIN,
                                          common::OB_PLAN_CACHE_BUCKET_NUMBER)))) {
        LOG_WARN("failed to init ps cache");
      } else {
        ps_session_info_allocator_.set_attr(mem_attr);
      }
    }
  }
  return ps_cache_;
}


//whether the user has the super privilege
bool ObSQLSessionInfo::has_user_super_privilege() const
{
  int ret = false;
  if (OB_PRIV_HAS_ANY(user_priv_set_, OB_PRIV_SUPER)) {
    ret = true;
  }
  return ret;
}

//whether the user has the process privilege
bool ObSQLSessionInfo::has_user_process_privilege() const
{
  int ret = false;
  if (OB_PRIV_HAS_ANY(user_priv_set_, OB_PRIV_PROCESS)) {
    ret = true;
  }
  return ret;
}

// Check the database runtime read-only state.
int ObSQLSessionInfo::check_global_read_only_privilege(const bool read_only,
                                                       const ObSqlTraits &sql_traits)
{
  int ret = OB_SUCCESS;
  if (!has_user_super_privilege() && read_only) {
    /** session1                session2
     *  insert into xxx;
     *                          set @@global.read_only = 1;
     *  update xxx (should fail)
     *  create (should fail)
     *  ... (all write stmt should fail)
     */
    if (!sql_traits.is_readonly_stmt_) {
      ret = OB_ERR_OPTION_PREVENTS_STATEMENT;
      LOG_WARN("the server is running with read_only, cannot execute stmt");
    } else {
      /** session1            session2                    session3
       *  begin                                           begin;
       *  insert into xxx;                                (without write stmt)
       *                      set @@global.read_only = 1;
       *  commit; (should fail)                           commit; (should success)
       */
      if (sql_traits.is_commit_stmt_ && is_in_transaction() && !tx_desc_->is_clean()) {
        ret = OB_ERR_OPTION_PREVENTS_STATEMENT;
        LOG_WARN("the server is running with read_only, cannot execute stmt");
      }
    }
  }
  return ret;
}

int ObSQLSessionInfo::remove_prepare(const ObString &ps_name)
{
  int ret = OB_SUCCESS;
  ObPsStmtId ps_id = OB_INVALID_ID;
  if (OB_UNLIKELY(!ps_name_id_map_.created())) {
    ret = OB_HASH_NOT_EXIST;
    LOG_WARN("map not created before insert any element", K(ret));
  } else if (OB_FAIL(ps_name_id_map_.erase_refactored(ps_name, &ps_id))) {
    LOG_WARN("ps session info not exist", K(ps_name));
  } else if (OB_INVALID_ID == ps_id) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session_info is null", K(ret));
  } else { /*do nothing*/ }
  return ret;
}

int ObSQLSessionInfo::get_prepare_id(const ObString &ps_name, ObPsStmtId &ps_id) const
{
  int ret = OB_SUCCESS;
  ps_id = OB_INVALID_ID;
  if (OB_UNLIKELY(!ps_name_id_map_.created())) {
    ret = OB_HASH_NOT_EXIST;
  } else if (OB_FAIL(ps_name_id_map_.get_refactored(ps_name, ps_id))) {
    LOG_WARN("get ps session info failed", K(ps_name));
  } else if (OB_INVALID_ID == ps_id) {
    ret = OB_HASH_NOT_EXIST;
    LOG_WARN("ps info is null", K(ret), K(ps_name));
  } else { /*do nothing*/ }

  if (ret == OB_HASH_NOT_EXIST) {
    ret = OB_EER_UNKNOWN_STMT_HANDLER;
  }
  return ret;
}

int ObSQLSessionInfo::add_prepare(const ObString &ps_name, ObPsStmtId ps_id)
{
  int ret = OB_SUCCESS;
  ObString stored_name;
  ObPsStmtId exist_ps_id = OB_INVALID_ID;
  if (OB_FAIL(conn_level_name_pool_.write_string(ps_name, &stored_name))) {
    LOG_WARN("failed to copy name", K(ps_name), K(ps_id), K(ret));
  } else if (OB_FAIL(try_create_ps_name_id_map())) {
    LOG_WARN("fail create ps name id map", K(ret));
  } else if (OB_FAIL(ps_name_id_map_.get_refactored(stored_name, exist_ps_id))) {
    if (OB_HASH_NOT_EXIST == ret) {
      if (OB_FAIL(ps_name_id_map_.set_refactored(stored_name, ps_id))) {
        LOG_WARN("fail insert ps id to hash map", K(stored_name), K(ps_id), K(ret));
      }
    } else {
      LOG_WARN("fail to search ps name hash id map", K(stored_name), K(ret));
    }
  } else if (ps_id != exist_ps_id) {
    LOG_DEBUG("exist ps id is diff", K(ps_id), K(exist_ps_id), K(ps_name), K(stored_name));
    if (OB_FAIL(remove_prepare(stored_name))) {
      LOG_WARN("failed to remove prepare", K(stored_name), K(ret));
    } else if (OB_FAIL(remove_ps_session_info(exist_ps_id))) {
      LOG_WARN("failed to remove prepare sesion info", K(exist_ps_id), K(stored_name), K(ret));
    } else if (OB_FAIL(ps_name_id_map_.set_refactored(stored_name, ps_id))) {
      LOG_WARN("fail insert ps id to hash map", K(stored_name), K(ps_id), K(ret));
    }
  }
  return ret;
}

int ObSQLSessionInfo::get_ps_session_info(const ObPsStmtId stmt_id,
                                          ObPsSessionInfo *&ps_session_info) const
{
  int ret = OB_SUCCESS;
  ps_session_info = NULL;
  if (OB_UNLIKELY(!ps_session_info_map_.created())) {
    ret = OB_HASH_NOT_EXIST;
    LOG_WARN("map not created before insert any element", K(ret));
  } else if (OB_FAIL(ps_session_info_map_.get_refactored(stmt_id, ps_session_info))) {
    LOG_WARN("get ps session info failed", K(stmt_id), K(get_server_sid()));
    if (ret == OB_HASH_NOT_EXIST) {
      ret = OB_EER_UNKNOWN_STMT_HANDLER;
    }
  } else if (OB_ISNULL(ps_session_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ps session info is null", K(ret), K(stmt_id));
  }
  return ret;
}

int ObSQLSessionInfo::remove_ps_session_info(const ObPsStmtId stmt_id)
{
  int ret = OB_SUCCESS;
  ObPsSessionInfo *session_info = NULL;
  LOG_TRACE("remove ps session info", K(ret), K(stmt_id), K(get_server_sid()), K(lbt()));
  if (OB_UNLIKELY(!ps_session_info_map_.created())) {
    ret = OB_HASH_NOT_EXIST;
    LOG_WARN("map not created before insert any element", K(ret));
  } else if (OB_FAIL(ps_session_info_map_.erase_refactored(stmt_id, &session_info))) {
    LOG_WARN("ps session info not exist", K(stmt_id));
  } else if (OB_ISNULL(session_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session_info is null", K(ret));
  } else {
    LOG_TRACE("remove ps session info", K(ret), K(stmt_id), K(get_server_sid()));
    session_info->~ObPsSessionInfo();
    ps_session_info_allocator_.free(session_info);
    session_info = NULL;
  }
  return ret;
}

int ObSQLSessionInfo::check_ps_stmt_id_in_use(const ObPsStmtId stmt_id, bool & is_in_use)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!in_use_ps_stmt_id_set_.created())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("map not created before insert any element", K(ret));
  } else if (!in_use_ps_stmt_id_set_.empty() && OB_HASH_EXIST == in_use_ps_stmt_id_set_.exist_refactored(stmt_id)) {
    is_in_use = true;
  } else {
    is_in_use = false;
  }
  return ret;
}

int ObSQLSessionInfo::add_ps_stmt_id_in_use(const ObPsStmtId stmt_id) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!in_use_ps_stmt_id_set_.created())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("set not created before insert any element", K(ret));
  } else if (OB_FAIL(in_use_ps_stmt_id_set_.set_refactored(stmt_id))) {
    LOG_WARN("add ps stmt id failed", K(ret), K(stmt_id));
  }
  return ret;
}

int ObSQLSessionInfo::earse_ps_stmt_id_in_use(const ObPsStmtId stmt_id) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!in_use_ps_stmt_id_set_.created())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("set not created before insert any element", K(ret));
  } else if (OB_FAIL(in_use_ps_stmt_id_set_.erase_refactored(stmt_id))) {
    LOG_WARN("ps stmt id not exist", K(stmt_id));
  }
  return ret;
}

int ObSQLSessionInfo::prepare_ps_stmt(const ObPsStmtId inner_stmt_id,
                                      const ObPsStmtInfo *stmt_info,
                                      ObPsStmtId &client_stmt_id,
                                      bool &already_exists,
                                      bool is_inner_sql)
{
  int ret = OB_SUCCESS;
  ObPsSessionInfo *session_info = NULL;
  // Each client-side prepared statement gets its own statement id. Internal SQL
  // continues to use the engine's statement id directly.
  if (!is_inner_sql) {
    client_stmt_id = ++next_client_ps_stmt_id_;
  } else {
    client_stmt_id = inner_stmt_id;
  }
  already_exists = false;
  if (is_inner_sql) {
    LOG_TRACE("is inner sql no need to add session info", K(inner_stmt_id),
              K(client_stmt_id), K(next_client_ps_stmt_id_), K(is_inner_sql));
  } else {
    LOG_TRACE("will add session info",
              K(inner_stmt_id), K(client_stmt_id), K(next_client_ps_stmt_id_),
              K(ret), K(is_inner_sql));
    if(OB_FAIL(try_create_in_use_ps_stmt_id_set())) {
      LOG_WARN("fail create in use ps stmt id", K(ret));
    } else if (OB_FAIL(try_create_ps_session_info_map())) {
      LOG_WARN("fail create map", K(ret));
    } else {
      ret = ps_session_info_map_.get_refactored(client_stmt_id, session_info);
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(session_info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("session_info is NULL", K(ret), K(inner_stmt_id), K(client_stmt_id));
      } else {
        already_exists = true;
        session_info->inc_ref_count();
      }
    } else if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      char *buf = static_cast<char*>(ps_session_info_allocator_.alloc(sizeof(ObPsSessionInfo)));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory failed", K(ret));
      } else if (OB_ISNULL(stmt_info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("stmt info is null", K(ret), K(stmt_info));
      } else {
        session_info = new (buf) ObPsSessionInfo(stmt_info->get_num_of_param());
        session_info->set_stmt_id(client_stmt_id);
        session_info->set_stmt_type(stmt_info->get_stmt_type());
        session_info->set_ps_stmt_checksum(stmt_info->get_ps_stmt_checksum());
        session_info->set_inner_stmt_id(inner_stmt_id);
        session_info->set_num_of_returning_into(stmt_info->get_num_of_returning_into());
        if (OB_FAIL(session_info->fill_param_types_with_null_type())) {
          LOG_WARN("fill param types failed", K(ret),
                                        K(stmt_info->get_ps_sql()),
                                        K(stmt_info->get_ps_stmt_checksum()),
                                        K(client_stmt_id),
                                        K(inner_stmt_id),
                                        K(get_server_sid()),
                                        K(stmt_info->get_num_of_param()),
                                        K(stmt_info->get_num_of_returning_into()));
        }
        LOG_TRACE("add ps session info", K(stmt_info->get_ps_sql()),
                                        K(stmt_info->get_ps_stmt_checksum()),
                                        K(client_stmt_id),
                                        K(inner_stmt_id),
                                        K(get_server_sid()),
                                        K(stmt_info->get_num_of_param()),
                                        K(stmt_info->get_num_of_returning_into()));
      }

      if (OB_SUCC(ret)) {
        session_info->inc_ref_count();
        if (OB_FAIL(ps_session_info_map_.set_refactored(client_stmt_id, session_info))) {
          // OB_HASH_EXIST cannot be here, no need to handle
          LOG_WARN("push back ps_session info failed", K(ret), K(client_stmt_id));
        } else {
          LOG_TRACE("add ps session info success", K(client_stmt_id), K(get_server_sid()));
        }
      }
      if (OB_FAIL(ret) && OB_NOT_NULL(session_info)) {
        session_info->~ObPsSessionInfo();
        ps_session_info_allocator_.free(session_info);
        session_info = NULL;
        buf = NULL;
      }
    } else {
      LOG_WARN("get ps session failed", K(ret), K(client_stmt_id), K(inner_stmt_id));
    }
  }
  return ret;
}

int ObSQLSessionInfo::get_inner_ps_stmt_id(ObPsStmtId cli_stmt_id, ObPsStmtId &inner_stmt_id)
{
  int ret = OB_SUCCESS;
  ObPsSessionInfo *ps_session_info = NULL;
  if (OB_UNLIKELY(!ps_session_info_map_.created())) {
    ret = OB_HASH_NOT_EXIST;
    LOG_WARN("map not created before insert any element", K(ret));
  } else if (OB_FAIL(ps_session_info_map_.get_refactored(cli_stmt_id, ps_session_info))) {
    LOG_WARN("get inner ps stmt id failed", K(ret), K(cli_stmt_id), K(lbt()));
  } else if (OB_ISNULL(ps_session_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ps session info is null", K(cli_stmt_id), "session_id", get_server_sid(), K(ret));
  } else {
    inner_stmt_id = ps_session_info->get_inner_stmt_id();
  }
  return ret;
}

ObPLCursorInfo *ObSQLSessionInfo::get_cursor(int64_t cursor_id)
{
  ObPLCursorInfo *cursor = NULL;
  if (OB_SUCCESS != pl_cursor_cache_.pl_cursor_map_.get_refactored(cursor_id, cursor)) {
    LOG_TRACE("get cursor info failed", K(cursor_id), K(get_server_sid()));
  }
  return cursor;
}

ObDbmsCursorInfo *ObSQLSessionInfo::get_dbms_cursor(int64_t cursor_id)
{
  int ret = OB_SUCCESS;
  ObPLCursorInfo *cursor = NULL;
  ObDbmsCursorInfo *dbms_cursor = NULL;
  OV (OB_NOT_NULL(cursor = get_cursor(cursor_id)),
      OB_INVALID_ARGUMENT, cursor_id);
  OV (cursor->is_dbms_sql_cursor(), 
      OB_INVALID_ARGUMENT, cursor_id);
  OV (OB_NOT_NULL(dbms_cursor = dynamic_cast<ObDbmsCursorInfo *>(cursor)),
      OB_INVALID_ARGUMENT, cursor_id);
  return dbms_cursor;
}

int ObSQLSessionInfo::add_cursor(pl::ObPLCursorInfo *cursor)
{
// open_cursors is 0 to indicate a special state, no limit is set
#define NEED_CHECK_SESS_OPEN_CURSORS_LIMIT(v) (0 == v ? false : true)
  int ret = OB_SUCCESS;
  bool add_cursor_success = false;
  CK (true);
  CK (OB_NOT_NULL(cursor));
  if (OB_SUCC(ret)) {
    int64_t open_cursors_limit = GCONF.open_cursors;
    if (NEED_CHECK_SESS_OPEN_CURSORS_LIMIT(open_cursors_limit)
        && open_cursors_limit <= pl_cursor_cache_.pl_cursor_map_.size()) {
      ret = OB_ERR_OPEN_CURSORS_EXCEEDED;
      LOG_WARN("maximum open cursors exceeded",
                K(ret), K(open_cursors_limit), K(pl_cursor_cache_.pl_cursor_map_.size()));
    }
  }
  if (OB_SUCC(ret)) {
    int64_t id = cursor->get_id();
    if (OB_INVALID_ID == id) {
      // mysql ps mode, will set cursor id to stmt_id in advance
      id = pl_cursor_cache_.gen_cursor_id();
      // ps cursor: proxy will record server ip, other ops of ps cursor will route by record ip.
    }
    if (OB_FAIL(ret)) {
    } else {
      // when select GV$OPEN_CURSOR, we will add get_thread_data_lock to fetch pl_cursor_map_
      // so we need get_thread_data_lock there
      ObSQLSessionInfo::LockGuard lock_guard(get_thread_data_lock());
      if (OB_FAIL(pl_cursor_cache_.pl_cursor_map_.set_refactored(id, cursor))) {
        LOG_WARN("fail insert ps id to hash map", K(id), K(*cursor), K(ret));
      } else {
        cursor->set_id(id);
        add_cursor_success = true;
        inc_session_cursor();
        LOG_DEBUG("ps cursor: add cursor", K(ret), K(id), K(get_server_sid()));
      }
    }
  }
  if (!add_cursor_success && OB_NOT_NULL(cursor)) {
    int64_t id = cursor->get_id();
    int tmp_ret = close_cursor(cursor);
    ret = OB_SUCCESS == ret ? tmp_ret : ret;
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN("close cursor fail when add cursor to sesssion.", K(ret), K(id), K(get_server_sid()));
    }
  }
  return ret;
}

int ObSQLSessionInfo::close_cursor(ObPLCursorInfo *&cursor)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(cursor)) {
    int64_t id = cursor->get_id();
    OZ (cursor->close(*this));
    cursor->~ObPLCursorInfo();
    get_cursor_allocator().free(cursor);
    cursor = NULL;
    LOG_DEBUG("close cursor", K(ret), K(id), K(get_server_sid()));
  } else {
    LOG_DEBUG("close cursor is null", K(get_server_sid()));
  }
  return ret;
}

int ObSQLSessionInfo::close_cursor(int64_t cursor_id)
{
  int ret = OB_SUCCESS;
  ObPLCursorInfo *cursor = NULL;
  LOG_INFO("ps cursor : remove cursor", K(ret), K(cursor_id), K(get_server_sid()));
  // when select GV$OPEN_CURSOR, we will add get_thread_data_lock to fetch pl_cursor_map_
  // so we need get_thread_data_lock there
  ObSQLSessionInfo::LockGuard lock_guard(get_thread_data_lock());
  if (OB_FAIL(pl_cursor_cache_.pl_cursor_map_.erase_refactored(cursor_id, &cursor))) {
    LOG_WARN("cursor info not exist", K(cursor_id));
  } else if (OB_ISNULL(cursor)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session_info is null", K(ret));
  } else {
    LOG_DEBUG("close cursor", K(ret), K(cursor_id), K(get_server_sid()));
    OZ (cursor->close(*this));
    cursor->~ObPLCursorInfo();
    get_cursor_allocator().free(cursor);
    cursor = NULL;
  }
  return ret;
}

int ObSQLSessionInfo::add_non_session_cursor(pl::ObPLCursorInfo *cursor)
{
  int ret = OB_SUCCESS;
  OZ (init_cursor_cache());
  // when select GV$OPEN_CURSOR, we will add get_thread_data_lock to fetch pl_non_session_cursor_map_
  // so we need get_thread_data_lock there
  ObSQLSessionInfo::LockGuard lock_guard(get_thread_data_lock());
  if (OB_FAIL(pl_cursor_cache_.pl_non_session_cursor_map_.set_refactored((int64_t)cursor, cursor))) {
    LOG_WARN("fail insert non session cursor to hash map", K(cursor), K(*cursor), K(ret));
  } else {
    EVENT_INC(SQL_OPEN_CURSORS_CURRENT);
    EVENT_INC(SQL_OPEN_CURSORS_CUMULATIVE);
  }
  return ret;
}

void ObSQLSessionInfo::del_non_session_cursor(pl::ObPLCursorInfo *cursor)
{
  int ret = OB_SUCCESS;
  // when select GV$OPEN_CURSOR, we will add get_thread_data_lock to fetch pl_non_session_cursor_map_
  // so we need get_thread_data_lock there
  ObSQLSessionInfo::LockGuard lock_guard(get_thread_data_lock());
  if (OB_FAIL(pl_cursor_cache_.pl_non_session_cursor_map_.erase_refactored((int64_t)cursor))) {
#ifdef DEBUG
    LOG_ERROR("fail delete non session cursor from hash map", K(cursor), K(*cursor), K(ret));
#else
    LOG_WARN("fail delete non session cursor from hash map", K(cursor), K(*cursor), K(ret));
#endif
  //ingore ret
  } else {
    EVENT_DEC(SQL_OPEN_CURSORS_CURRENT);
  }
}

int ObSQLSessionInfo::print_all_cursor()
{
  int ret = OB_SUCCESS;
  int64_t open_cnt = 0;
  int64_t unexpected_cnt = 0;
  LOG_DEBUG("CURSOR DEBUG: total cursors in cursor map: ",
            K(pl_cursor_cache_.pl_cursor_map_.size()));
  for (CursorCache::CursorMap::iterator iter = pl_cursor_cache_.pl_cursor_map_.begin();  //ignore ret
      iter != pl_cursor_cache_.pl_cursor_map_.end();  ++iter) {
    pl::ObPLCursorInfo *cursor_info = iter->second;
    if (OB_ISNULL(cursor_info)) {
      // do nothing;
    } else {
      if (cursor_info->isopen()) {
        open_cnt++;
        LOG_DEBUG("CURSOR DEBUG: found open cursor", K(*cursor_info),
                                                    K(cursor_info->get_ref_count()));
      } else {
        if (0 != cursor_info->get_ref_count()) {
          unexpected_cnt++;
          LOG_DEBUG("CURSOR DEBUG: found closed cursor", K(*cursor_info),
                                                      K(cursor_info->get_ref_count()));
        }
      }
    }
  }
  LOG_DEBUG("CURSOR DEBUG: may illegal cursors in cursor map: ",  K(open_cnt), K(unexpected_cnt));
  return ret;
}

int ObSQLSessionInfo::init_cursor_cache()
{
  int ret = OB_SUCCESS;
  if (!pl_cursor_cache_.is_inited()) {
    // when select GV$OPEN_CURSOR, we will add get_thread_data_lock to fetch pl_cursor_map_
    // so we need get_thread_data_lock there
    ObSQLSessionInfo::LockGuard lock_guard(get_thread_data_lock());
    OZ (pl_cursor_cache_.init(),
                              1UL,
                              get_server_sid());
  }
  return ret;
}


int ObSQLSessionInfo::make_cursor(pl::ObPLCursorInfo *&cursor)
{
  int ret = OB_SUCCESS;
  pl::ObPLCursorInfo* tmp_cursor = NULL;
  UNUSED(cursor);
  return ret;
}

int ObSQLSessionInfo::make_dbms_cursor(pl::ObDbmsCursorInfo *&cursor,
                                       uint64_t id)
{
  int ret = OB_SUCCESS;
  void *buf = NULL;
  if (!pl_cursor_cache_.is_inited()) {
    OZ (pl_cursor_cache_.init(),
        1UL, get_server_sid());
  }
  OV (OB_NOT_NULL(buf = get_cursor_allocator().alloc(sizeof(ObDbmsCursorInfo))),
      OB_ALLOCATE_MEMORY_FAILED, sizeof(ObDbmsCursorInfo));
  OX (MEMSET(buf, 0, sizeof(ObDbmsCursorInfo)));
  OV (OB_NOT_NULL(cursor = new (buf) ObDbmsCursorInfo(get_cursor_allocator())));
  OZ (cursor->init());
  OX (cursor->set_id(id));
  OX (cursor->set_dbms_sql_cursor());
  OZ (add_cursor(cursor));
  /*
   * A dbms cursor can be repeatedly parsed after being opened, each time switching to a different statement, so internal different objects need to use different allocators:
   * 1. cursor_id does not change after open_cursor until close_cursor, so its lifecycle is relatively long, allocated from the session's allocator.
   * 2. sql_stmt_ and other properties change every time after parsing, so their lifecycle is shorter, memory is allocated from entity, and a new entity is created each time it is parsed.
   * 3. spi_result and spi_cursor also need to be reset every time it is parsed.
   */
  return ret;
}

int64_t ObSQLSessionInfo::get_plsql_exec_time()
{
  return (NULL == pl_context_ || 0 == pl_context_->get_exec_stack().count()
          || NULL == pl_context_->get_exec_stack().at(pl_context_->get_exec_stack().count()-1))
            ? plsql_exec_time_
            : pl_context_->get_exec_stack().at(pl_context_->get_exec_stack().count()-1)->get_sub_plsql_exec_time();
}

void ObSQLSessionInfo::update_pure_sql_exec_time(int64_t elapsed_time)
{
  if (OB_NOT_NULL(pl_context_)
      && pl_context_->get_exec_stack().count() > 0
      && OB_NOT_NULL(pl_context_->get_exec_stack().at(pl_context_->get_exec_stack().count()-1))) {
    int64_t pos = pl_context_->get_exec_stack().count()-1;
    pl::ObPLExecState *state = pl_context_->get_exec_stack().at(pos);
    state->add_pure_sql_exec_time(elapsed_time - state->get_sub_plsql_exec_time() - state->get_pure_sql_exec_time());
  }
}

int ObSQLSessionInfo::check_read_only_privilege(const bool read_only,
                                                const ObSqlTraits &sql_traits)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_global_read_only_privilege(read_only, sql_traits))) {
    LOG_WARN("failed to check global read_only privilege!", K(ret));
  } else if (OB_FAIL(check_tx_read_only_privilege(sql_traits))){
    LOG_WARN("failed to check tx_read_only privilege!", K(ret));
  }
  return ret;
}
// In session when trace has been opened once, a buffer is allocated, which will be released only when the session is destructed.


OB_DEF_SERIALIZE(ObSQLSessionInfo::ApplicationInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, module_name_, action_name_, client_info_);
  return ret;
}

OB_DEF_DESERIALIZE(ObSQLSessionInfo::ApplicationInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, module_name_, action_name_, client_info_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObSQLSessionInfo::ApplicationInfo)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, module_name_, action_name_, client_info_);
  return len;
}

OB_DEF_SERIALIZE(ObSQLSessionInfo)
{
  int ret = OB_SUCCESS;
  BASE_SER((ObSQLSessionInfo, ObBasicSessionInfo));
  LST_DO_CODE(OB_UNIS_ENCODE,
      thread_data_.cur_query_start_time_,
      user_priv_set_,
      db_priv_set_,
      trans_type_,
      global_sessid_,
      inner_flag_,
      is_max_availability_mode_,
      session_type_,
      has_temp_table_flag_,
      enable_early_lock_release_,
      enable_role_array_,
      in_definer_named_proc_,
      priv_user_id_,
      prelock_,
      thread_data_.is_in_retry_,
      ddl_info_,
      affected_rows_);
  return ret;
}

OB_DEF_DESERIALIZE(ObSQLSessionInfo)
{
  int ret = OB_SUCCESS;
  BASE_DESER((ObSQLSessionInfo, ObBasicSessionInfo));
  LST_DO_CODE(OB_UNIS_DECODE,
      thread_data_.cur_query_start_time_,
      user_priv_set_,
      db_priv_set_,
      trans_type_,
      global_sessid_,
      inner_flag_,
      is_max_availability_mode_,
      session_type_,
      has_temp_table_flag_,
      enable_early_lock_release_,
      enable_role_array_,
      in_definer_named_proc_,
      priv_user_id_,
      prelock_,
      thread_data_.is_in_retry_,
      ddl_info_,
      affected_rows_);
  (void)ObSQLUtils::adjust_time_by_ntp_offset(thread_data_.cur_query_start_time_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObSQLSessionInfo)
{
  int64_t len = 0;
  BASE_ADD_LEN((ObSQLSessionInfo, ObBasicSessionInfo));
  LST_DO_CODE(OB_UNIS_ADD_LEN,
      thread_data_.cur_query_start_time_,
      user_priv_set_,
      db_priv_set_,
      trans_type_,
      global_sessid_,
      inner_flag_,
      is_max_availability_mode_,
      session_type_,
      has_temp_table_flag_,
      enable_early_lock_release_,
      enable_role_array_,
      in_definer_named_proc_,
      priv_user_id_,
      prelock_,
      thread_data_.is_in_retry_,
      ddl_info_,
      affected_rows_);
  return len;
}

int ObSQLSessionInfo::get_collation_type_of_names(
    const ObNameTypeClass type_class,
    ObCollationType &cs_type) const
{
  int ret = OB_SUCCESS;
  ObNameCaseMode case_mode = OB_NAME_CASE_INVALID;
  cs_type = CS_TYPE_INVALID;
  if (OB_TABLE_NAME_CLASS == type_class) {
    if (OB_FAIL(get_name_case_mode(case_mode))) {
      LOG_WARN("fail to get name case mode", K(ret));
    } else if (OB_ORIGIN_AND_SENSITIVE == case_mode) {
      cs_type = CS_TYPE_UTF8MB4_BIN;
    } else if (OB_ORIGIN_AND_INSENSITIVE == case_mode || OB_LOWERCASE_AND_INSENSITIVE == case_mode) {
      cs_type = CS_TYPE_UTF8MB4_GENERAL_CI;
    }
  } else if (OB_COLUMN_NAME_CLASS == type_class) {
    cs_type = CS_TYPE_UTF8MB4_GENERAL_CI;
  } else if (OB_USER_NAME_CLASS == type_class) {
    cs_type = CS_TYPE_UTF8MB4_BIN;
  }
  return ret;
}


int ObSQLSessionInfo::kill_query()
{
  LOG_INFO("kill query", K(get_server_sid()), K(get_current_query_string()));
  ObSQLSessionInfo::LockGuard lock_guard(get_thread_data_lock());
  update_last_active_time();
  set_session_state(QUERY_KILLED);
  return OB_SUCCESS;
}

int ObSQLSessionInfo::set_query_deadlocked()
{
  LOG_INFO("set query deadlocked", K(get_server_sid()), K(get_current_query_string()));
  ObSQLSessionInfo::LockGuard lock_guard(get_thread_data_lock());
  update_last_active_time();
  set_session_state(QUERY_DEADLOCKED);
  return OB_SUCCESS;
}

void ObSQLSessionInfo::update_stat_from_exec_record()
{
  session_stat_.total_logical_read_ += (audit_record_.exec_record_.memstore_read_row_count_
                                        + audit_record_.exec_record_.ssstore_read_row_count_);
//  session_stat_.total_logical_write_ += 0;
//  session_stat_.total_physical_read_ += 0;
//  session_stat_.total_lock_count_ += 0;
}

void ObSQLSessionInfo::update_stat_from_exec_timestamp()
{
  session_stat_.total_cpu_time_us_ += audit_record_.exec_timestamp_.executor_t_;
  session_stat_.total_exec_time_us_ += audit_record_.exec_timestamp_.elapsed_t_;
}


void ObSQLSessionInfo::set_session_type_with_flag()
{
  if (OB_UNLIKELY(INVALID_TYPE == session_type_)) {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "session type is not init, only happen when old server send rpc to new server");
    session_type_ = inner_flag_ ? INNER_SESSION : USER_SESSION;
  }
}

void ObSQLSessionInfo::set_early_lock_release(bool enable)
{
  enable_early_lock_release_ = enable;
  if (enable) {
    SQL_SESSION_LOG(DEBUG, "set early lock release success",
        "sessid", get_server_sid());
  }
}

ObPLCursorInfo *ObSQLSessionInfo::get_pl_implicit_cursor()
{
  return NULL != pl_context_ ? &(pl_context_->get_cursor_info()) : NULL;
}

ObPLSqlCodeInfo *ObSQLSessionInfo::get_pl_sqlcode_info()
{
  return NULL != pl_context_ ? &(pl_context_->get_sqlcode_info()) : NULL;
}

bool ObSQLSessionInfo::has_pl_implicit_savepoint()
{
  return NULL != pl_context_ ? pl_context_->has_implicit_savepoint() : false;
}

void ObSQLSessionInfo::clear_pl_implicit_savepoint()
{
  if (OB_NOT_NULL(pl_context_)) {
    pl_context_->clear_implicit_savepoint();
  }
}

void ObSQLSessionInfo::set_has_pl_implicit_savepoint(bool v)
{
  if (OB_NOT_NULL(pl_context_)) {
    pl_context_->set_has_implicit_savepoint(v);
  }
}

void ObSQLSessionInfo::reset_all_package_state()
{
  if (0 != package_state_map_.size()) {
    FOREACH(it, package_state_map_) {
      it->second->reset(this);
      it->second->~ObPLPackageState();
      get_package_allocator().free(it->second);
      it->second = NULL;
    }
    package_state_map_.clear();
  }
}

int ObSQLSessionInfo::reset_all_package_state_by_dbms_session()
{
  /* its called by dbms_session.reset_package()
   * in this mode
   * if the package is a trigger, we should do nothing
   */
  int ret = OB_SUCCESS;
  if (0 == package_state_map_.size()
      || NULL != get_pl_context()
      || false == need_reset_package()) {
    // do nothing
  } else {

    ObSEArray<int64_t, 4> remove_packages;
    if (0 != package_state_map_.size()) {
      FOREACH(it, package_state_map_) {
        if (!share::schema::ObTriggerInfo::is_trigger_package_id(it->second->get_package_id())) {
          ret = ret != OB_SUCCESS ? ret : remove_packages.push_back(it->first);
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < remove_packages.count(); ++i) {
      ObPLPackageState *package_state = NULL;
      bool need_reset = false;
      OZ (package_state_map_.get_refactored(remove_packages.at(i), package_state));
      CK (OB_NOT_NULL(package_state));
      OZ (package_state_map_.erase_refactored(remove_packages.at(i)));
      OX (need_reset = true);
      if (need_reset && NULL != package_state) {
        package_state->reset(this);
        package_state->~ObPLPackageState();
        get_package_allocator().free(package_state);
      }
    }
    // wether reset succ or not, set need_reset_package to false
    set_need_reset_package(false);
  }
  return ret;
}

int ObSQLSessionInfo::reset_all_serially_package_state()
{
  int ret = OB_SUCCESS;
  ObSEArray<int64_t, 4> serially_packages;
  if (0 != package_state_map_.size()) {
    FOREACH(it, package_state_map_) {
      if (it->second->get_serially_reusable()) {
        it->second->reset(this);
        it->second->~ObPLPackageState();
        get_package_allocator().free(it->second);
        OZ (serially_packages.push_back(it->first));
      }
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < serially_packages.count(); ++i) {
    OZ (package_state_map_.erase_refactored(serially_packages.at(i)));
  }
  return ret;
}

int ObSQLSessionInfo::replace_user_variable(
  const common::ObString &name, const ObSessionVariable &value)
{
  return ObBasicSessionInfo::replace_user_variable(name, value);
}

int ObSQLSessionInfo::replace_user_variable(
  ObExecContext &ctx, const common::ObString &name, const ObSessionVariable &value)
{
  UNUSED(ctx);
  return ObBasicSessionInfo::replace_user_variable(name, value);
}


int ObSQLSessionInfo::replace_user_variables(
  ObExecContext &ctx, const ObSessionValMap &user_var_map)
{
  UNUSED(ctx);
  return ObBasicSessionInfo::replace_user_variables(user_var_map);
}


int ObSQLSessionInfo::set_client_id(const common::ObString &client_identifier)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObBasicSessionInfo::set_client_identifier(client_identifier))) {
    LOG_WARN("failed to set client id", K(ret));
  } else {
  }
  return ret;
}

int ObSQLSessionInfo::save_session(StmtSavedValue &saved_value)
{
  int ret = OB_SUCCESS;
  OZ (save_basic_session(saved_value));
  OZ (save_sql_session(saved_value));
  return ret;
}

int ObSQLSessionInfo::save_sql_session(StmtSavedValue &saved_value)
{
  int ret = OB_SUCCESS;
  OX (saved_value.audit_record_.assign(audit_record_));
  OX (audit_record_.reset());
  OX (saved_value.inner_flag_ = inner_flag_);
  OX (saved_value.session_type_ = session_type_);
  OX (saved_value.read_uncommited_ = read_uncommited_);
  OX (saved_value.is_ignore_stmt_ = is_ignore_stmt_);
  OX (inner_flag_ = true);
  OX (saved_value.db_id_ = get_database_id());
  OZ (saved_value.db_name_.assign(get_database_name()));
  return ret;
}

int ObSQLSessionInfo::restore_sql_session(StmtSavedValue &saved_value)
{
  int ret = OB_SUCCESS;
  OX (session_type_ = saved_value.session_type_);
  OX (inner_flag_ = saved_value.inner_flag_);
  OX (read_uncommited_ = saved_value.read_uncommited_);
  OX (is_ignore_stmt_ = saved_value.is_ignore_stmt_);
  OX (audit_record_.assign(saved_value.audit_record_));
  OX (set_database_id(saved_value.db_id_));
  OZ (set_default_database(saved_value.db_name_.string()));
  return ret;
}

int ObSQLSessionInfo::restore_session(StmtSavedValue &saved_value)
{
  int ret = OB_SUCCESS;
  OZ (restore_sql_session(saved_value));
  OZ (restore_basic_session(saved_value));
  return ret;
}

int ObSQLSessionInfo::begin_nested_session(StmtSavedValue &saved_value, bool skip_cur_stmt_tables)
{
  int ret = OB_SUCCESS;
  OV (nested_count_ >= 0, OB_ERR_UNEXPECTED, nested_count_);
  OZ (ObBasicSessionInfo::begin_nested_session(saved_value, skip_cur_stmt_tables));
  OZ (save_sql_session(saved_value));
  OX (nested_count_++);
  LOG_DEBUG("begin_nested_session", K(ret), K_(nested_count));
  return ret;
}

int ObSQLSessionInfo::end_nested_session(StmtSavedValue &saved_value)
{
  int ret = OB_SUCCESS;
  OV (nested_count_ > 0, OB_ERR_UNEXPECTED, nested_count_);
  OX (nested_count_--);
  OZ (restore_sql_session(saved_value));
  OZ (ObBasicSessionInfo::end_nested_session(saved_value));
  OX (saved_value.reset());
  return ret;
}

int ObSQLSessionInfo::set_enable_role_array(const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  ret = set_enable_role_ids(role_id_array);
  return ret;
}

void ObSQLSessionInfo::ObCachedRuntimeConfig::refresh()
{
  int tmp_ret = OB_SUCCESS;
  int64_t cur_ts = ObClockGenerator::getClock();
  bool disable_cache = false;
  int ret = OB_E(EventTable::EN_ENABLE_RUNTIME_CONFIG_CACHE) OB_SUCCESS;
  if (ret == OB_ERR_UNEXPECTED) {
    disable_cache = true;
  }
  if (OB_ISNULL(session_)) {
    tmp_ret = OB_ERR_UNEXPECTED;
    LOG_WARN_RET(tmp_ret, "session_ is null");
  } else if (cur_ts - last_check_ec_ts_ > 5000000
             || disable_cache) {
    // Cache data version for performance optimization.
    ATOMIC_STORE(&data_version_, DATA_CURRENT_VERSION);
    if (OB_LIKELY(true)) {
      // 1.Is batch_multi_statement allowed
      enable_batched_multi_statement_ = GCONF.ob_enable_batched_multi_statement;
      // 3.Is bloom_filter allowed
      if (GCONF._bloom_filter_enabled) {
        enable_bloom_filter_ = true;
      } else {
        enable_bloom_filter_ = false;
      }
      // 4.sort area size
      ATOMIC_STORE(&sort_area_size_, GCONF._sort_area_size);
      ATOMIC_STORE(&hash_area_size_, GCONF._hash_area_size);
      ATOMIC_STORE(&enable_immediate_row_conflict_check_, GCONF._ob_immediate_row_conflict_check);
      ATOMIC_STORE(&range_optimizer_max_mem_size_, GCONF.range_optimizer_max_mem_size);
      ATOMIC_STORE(&_query_record_size_limit_, GCONF._query_record_size_limit);
      ATOMIC_STORE(&_ob_sqlstat_enable_, GCONF._ob_sqlstat_enable);
      px_join_skew_handling_ = GCONF._px_join_skew_handling;
      px_join_skew_minfreq_ = GCONF._px_join_skew_minfreq;
      enable_decimal_int_type_ = GCONF._enable_decimal_int_type;
      enable_mysql_compatible_dates_ = GCONF._enable_mysql_compatible_dates;
      // 7. print_sample_ppm_ for flt
      ATOMIC_STORE(&print_sample_ppm_, GCONF._print_sample_ppm);
    }
    ATOMIC_STORE(&last_check_ec_ts_, cur_ts);
  }
  UNUSED(tmp_ret);
}

int ObSQLSessionInfo::get_tmp_table_size(uint64_t &size) {
  int ret = OB_SUCCESS;
  const ObBasicSysVar *tmp_table_size = get_sys_var(SYS_VAR_TMP_TABLE_SIZE);
  CK (OB_NOT_NULL(tmp_table_size));
  if (OB_SUCC(ret) &&
      tmp_table_size->get_value().get_uint64() != tmp_table_size->get_max_val().get_uint64()) {
    size = tmp_table_size->get_value().get_uint64();
  } else {
    size = OB_INVALID_SIZE;
  }
  return ret;
}
int ObSQLSessionInfo::ps_use_stream_result_set(bool &use_stream) {
  int ret = OB_SUCCESS;
  uint64_t size = 0;
  use_stream = false;
  OZ (get_tmp_table_size(size));
  if (OB_SUCC(ret) && OB_INVALID_SIZE == size) {
    use_stream = true;
#if !defined(NDEBUG)
    LOG_INFO("cursor use stream result.");
#endif
  }
  return ret;
}

::oceanbase::observer::ObPieceCache* ObSQLSessionInfo::get_piece_cache(bool need_init) {
  if (NULL == piece_cache_ && need_init) {
    void *buf = get_session_allocator().alloc(sizeof(ObPieceCache));
    if (NULL != buf) {
      MEMSET(buf, 0, sizeof(ObPieceCache));
      piece_cache_ = new (buf) ObPieceCache();
      if (OB_SUCCESS != piece_cache_->init()) {
        piece_cache_->~ObPieceCache();
        get_session_allocator().free(piece_cache_);
        piece_cache_ = NULL;
        LOG_WARN_RET(OB_ERR_UNEXPECTED, "init piece cache fail");
      }
    }
  }
  return piece_cache_;
}

template <typename AllocatorT>
static int write_str_reuse_buf(AllocatorT &allocator, const ObString &src, ObString &dst)
{
  int ret = OB_SUCCESS;
  const ObString::obstr_size_t src_len = src.length();
  char *ptr = NULL;
  if (src_len <= dst.size()) {
    MEMCPY(dst.ptr(), src.ptr(), src_len);
    dst.set_length(src_len);
  } else {
    allocator.free(dst.ptr());
    if (OB_ISNULL(src.ptr()) || OB_UNLIKELY(0 >= src_len)) {
      dst.assign(NULL, 0);
    } else if (NULL == 
                (ptr = static_cast<char *>(allocator.alloc(src_len)))) {
      dst.assign(NULL, 0);
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory failed", K(ret), "size", src_len);
    } else {
      MEMCPY(ptr, src.ptr(), src_len);
      dst.assign_buffer(ptr, src_len);
      dst.set_length(src_len);
    }
  }
  return ret;
}

int ObSQLSessionInfo::set_login_info(const share::schema::ObUserLoginInfo &login_info)
{
  int ret = OB_SUCCESS;
  OZ (write_str_reuse_buf(get_session_allocator(), login_info.runtime_name_, login_info_.runtime_name_));
  OZ (write_str_reuse_buf(get_session_allocator(), login_info.user_name_, login_info_.user_name_));
  OZ (write_str_reuse_buf(get_session_allocator(), login_info.client_ip_, login_info_.client_ip_));
  OZ (write_str_reuse_buf(get_session_allocator(), login_info.passwd_, login_info_.passwd_));
  OZ (write_str_reuse_buf(get_session_allocator(), login_info.db_, login_info_.db_));
  OZ (write_str_reuse_buf(get_session_allocator(), login_info.scramble_str_, login_info_.scramble_str_));
  return ret;
}

int ObSQLSessionInfo::set_login_auth_data(const ObString &auth_data) {
  int ret = OB_SUCCESS;
  OZ (write_str_reuse_buf(get_session_allocator(), auth_data, login_info_.passwd_));
  return ret;
}



int ObSQLSessionInfo::on_user_connect(share::schema::ObSessionPrivInfo &priv_info,
                                      const ObUserInfo *user_info)
{
  int ret = OB_SUCCESS;
  ObConnectResourceMgr *conn_res_mgr = GCTX.conn_res_mgr_;
  if (get_is_deserialized()) {
    // do nothing
  } else if (OB_ISNULL(conn_res_mgr) || OB_ISNULL(user_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("connect resource mgr or user info is null", K(ret), K(conn_res_mgr));
  } else {
    const ObPrivSet &priv = priv_info.user_priv_set_;
    const ObString &user_name = priv_info.user_name_;
    
    const uint64_t user_id = priv_info.user_id_;
    uint64_t max_connections_per_hour = user_info->get_max_connections();
    uint64_t max_user_connections = user_info->get_max_user_connections();
    uint64_t max_server_connections = 0;
    if (OB_FAIL(get_sys_variable(SYS_VAR_MAX_CONNECTIONS, max_server_connections))) {
      LOG_WARN("get system variable SYS_VAR_MAX_CONNECTIONS failed", K(ret));
    } else if (0 == max_user_connections) {
      if (OB_FAIL(get_sys_variable(SYS_VAR_MAX_USER_CONNECTIONS, max_user_connections))) {
        LOG_WARN("get system variable SYS_VAR_MAX_USER_CONNECTIONS failed", K(ret));
      }
    } else {
      ObObj val;
      val.set_uint64(max_user_connections);
      if (OB_FAIL(update_sys_variable(SYS_VAR_MAX_USER_CONNECTIONS, val))) {
        LOG_WARN("set system variable SYS_VAR_MAX_USER_CONNECTIONS failed", K(ret), K(val));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(conn_res_mgr->on_user_connect(
                user_id, priv, user_name,
                max_connections_per_hour,
                max_user_connections,
                max_server_connections, *this))) {
      LOG_WARN("create user connection failed", K(ret));
    }
  }
  return ret;
}

int ObSQLSessionInfo::on_user_disconnect()
{
  int ret = OB_SUCCESS;
  ObConnectResourceMgr *conn_res_mgr = GCTX.conn_res_mgr_;
  if (OB_ISNULL(conn_res_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("connect resource mgr is null", K(ret));
  } else if (OB_FAIL(conn_res_mgr->on_user_disconnect(*this))) {
    LOG_WARN("user disconnect failed", K(ret));
  }
  return ret;
}

void ObSQLSessionInfo::reset_tx_variable(bool reset_next_scope)
{
  ObBasicSessionInfo::reset_tx_variable(reset_next_scope);
  set_early_lock_release(false);
}
int ObSQLSessionInfo::set_module_name(const common::ObString &mod) {
  int ret = OB_SUCCESS;
  int64_t size = min(common::OB_MAX_MOD_NAME_LENGTH, mod.length());
  MEMSET(module_buf_, 0x00, common::OB_MAX_MOD_NAME_LENGTH);
  MEMCPY(module_buf_, mod.ptr(), size);
  client_app_info_.module_name_.assign(&module_buf_[0], size);
  return OB_SUCCESS;
}

int ObSQLSessionInfo::set_action_name(const common::ObString &act)
{
  int64_t size = min(common::OB_MAX_ACT_NAME_LENGTH, act.length());
  MEMSET(action_buf_, 0x00, common::OB_MAX_ACT_NAME_LENGTH);
  MEMCPY(action_buf_, act.ptr(), size);
  client_app_info_.action_name_.assign(&action_buf_[0], size);
  return OB_SUCCESS;
}

int ObSQLSessionInfo::set_client_info(const common::ObString &client_info)
{
  int64_t size = min(common::OB_MAX_CLIENT_INFO_LENGTH, client_info.length());
  MEMSET(client_info_buf_, 0x00, common::OB_MAX_CLIENT_INFO_LENGTH);
  MEMCPY(client_info_buf_, client_info.ptr(), size);
  client_app_info_.client_info_.assign(&client_info_buf_[0], size);
  return OB_SUCCESS;
}

int ObSQLSessionInfo::sql_sess_record_sql_stat_start_value(ObExecutingSqlStatRecord& executing_sqlstat)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(executing_sql_stat_record_.assign(executing_sqlstat))) {
    LOG_WARN("failed to assign executing sql stat record");
  }
  return ret;
}
