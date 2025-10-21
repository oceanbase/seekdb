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

#ifndef _OB_OBPROXY_BASIC_SESSION_INFO_H
#define _OB_OBPROXY_BASIC_SESSION_INFO_H 1

#include "share/ob_define.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/allocator/ob_pooled_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/list/ob_list.h"
#include "lib/lock/ob_recursive_mutex.h"
#include "lib/lock/ob_lock_guard.h"
#include "lib/objectpool/ob_pool.h"
#include "lib/oblog/ob_warning_buffer.h"
#include "lib/timezone/ob_timezone_info.h"
#include "lib/ash/ob_active_session_guard.h"
#include "rpc/ob_sql_request_operator.h"
#include "share/ob_compatibility_control.h"
#include "share/ob_debug_sync.h"
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/ob_time_zone_info_manager.h"
#include "storage/tx/ob_trans_define.h"
#include "rpc/obmysql/ob_mysql_packet.h"
#include "share/system_variable/ob_system_variable_factory.h"
#include "share/system_variable/ob_system_variable_alias.h"
#include "share/client_feedback/ob_client_feedback_manager.h"
#include "sql/session/ob_session_val_map.h"
#include "sql/ob_sql_mode_manager.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/ob_sql_context.h"
#include "sql/ob_sql_trans_util.h"
#include "share/partition_table/ob_partition_location.h"
#include "common/sql_mode/ob_sql_mode_utils.h"
#include "sql/monitor/flt/ob_flt_extra_info.h"
#include "sql/monitor/flt/ob_flt_utils.h"
#include "sql/parser/ob_parser_utils.h"

namespace oceanbase
{
namespace observer {
class ObSMConnection;
}
namespace common {
class ObDiagnosticInfo;
}
namespace share {
class ObSwitchCatalogHelper;
}
using sql::FLTControlInfo;
namespace sql
{
class ObExprRegexpSessionVariables;
class ObPCMemPctConf;
class ObPartitionHitInfo
{
public:
  ObPartitionHitInfo() : value_(true), freeze_(false) {}
  ~ObPartitionHitInfo() {}
  bool get_bool() { return value_; }
  void try_set_bool(bool v) { if (!freeze_) value_ = v; }
  void freeze() { freeze_ = true; }
  void reset() { value_ = true; freeze_ = false; }
private:
  bool value_;
  bool freeze_;
};
struct ObSessionNLSParams //oracle nls parameters
{
  ObLengthSemantics nls_length_semantics_;
  ObCollationType nls_collation_; //for oracle char and varchar2
  ObCollationType nls_nation_collation_; //for oracle nchar and nvarchar2

  TO_STRING_KV(K(nls_length_semantics_), K(nls_collation_), K(nls_nation_collation_));
};


#define TZ_INFO(session) \
  (NULL != (session) ? (session)->get_timezone_info() : NULL)

#define CREATE_OBJ_PRINT_PARAM(session) \
  (NULL != (session) ? (session)->create_obj_print_params() : ObObjPrintParams())

// flag is a single bit, but marco(e.g., IS_NO_BACKSLASH_ESCAPES) compare two 64-bits int using '&';
// if we directly assign the result to flag(single bit), only the last bit of the result is used,
// which is equal to 'flag = result & 1;'.
// So we first convert the result to bool(tmp_flag) and assign the bool to flag, which is equal to
// 'flag = result!=0;'.
#define GET_SQL_MODE_BIT(marco, sql_mode, flag) \
  do {                                          \
    bool tmp_flag=false;                        \
    marco(sql_mode, tmp_flag);                  \
    flag = tmp_flag;                            \
  } while(0)

#define CHECK_COMPATIBILITY_MODE(session) UNUSED(session)


class ObExecContext;
class ObSysVarInPC;
class ObBasicSessionInfo;

enum ObDisconnectState
{
  DIS_INIT,                 // INIT
  NORMAL_QUIT,              // QUIT.
  NORMAL_KILL_SESSION,      // KILL SESSION.
  SERVER_FORCE_DISCONNECT,  // force_disconnect.
  CLIENT_FORCE_DISCONNECT,  // TCP disconnect.
};

enum ObSQLSessionState
{
  SESSION_INIT,
  SESSION_SLEEP,
  QUERY_ACTIVE,
  QUERY_KILLED,
  SESSION_KILLED,
  QUERY_DEADLOCKED,
};

enum ObTransSpecfiedStatus
{
  TRANS_SPEC_NOT_SET,
  TRANS_SPEC_SET,
  TRANS_SPEC_COMMIT,
};

enum ObSessionRetryStatus
{
  SESS_NOT_IN_RETRY,  // session not in retry
  SESS_IN_RETRY,  // session retrying, and not related to replication tables
  SESS_IN_RETRY_FOR_DUP_TBL, // retry caused by errors related to duplicate table
};
/// ObBasicSessionInfo stores system variables and related variables, and stores the state that needs to be serialized to the remote when executing SQL tasks remotely
/// ObSQLSessionInfo stores other state information, such as prepared statement related information, etc.
/// @note All the system variables are stored in sys_var_val_map_, simultaneously, frequently used variables are also stored in this data structure as independent
/// Member storage for easy access. Note to keep the two sets of data consistent when updating system variables and serializing.
class ObBasicSessionInfo
{
  OB_UNIS_VERSION_V(1);
public:
  // 256KB ~= 4 * OB_COMMON_MEM_BLOCK_SIZE
  static const int64_t APPROX_MEM_USAGE_PER_SESSION = 256 * 1024L;
  static const uint64_t VALID_PROXY_SESSID = 0;
  static const uint32_t INVALID_SESSID = common::INVALID_SESSID;

  enum SafeWeakReadSnapshotSource
  {
    UNKOWN,
    AUTO_PLAN,
    PROXY_INPUT,
    LAST_STMT,
  };
  typedef common::ObPooledAllocator<common::hash::HashMapTypes<common::ObString,
          share::ObBasicSysVar*>::AllocType, common::ObWrapperAllocator> SysVarNameValMapAllocer;
  typedef common::hash::ObHashMap<common::ObString,
                                  share::ObBasicSysVar*,
                                  common::hash::NoPthreadDefendMode,
                                  common::hash::hash_func<common::ObString>,
                                  common::hash::equal_to<common::ObString>,
                                  SysVarNameValMapAllocer,
                                  common::hash::NormalPointer,
                                  common::ObWrapperAllocator> SysVarNameValMap;
  typedef lib::ObLockGuard<common::ObRecursiveMutex> LockGuard;

  class TableStmtType
  {
    OB_UNIS_VERSION_V(1);
  public:
    TableStmtType()
      : table_id_(common::OB_INVALID_ID),
        stmt_type_(stmt::T_NONE)
    {}
    TableStmtType(uint64_t table_id)
      : table_id_(table_id),
        stmt_type_(stmt::T_NONE)
    {}
    TableStmtType(uint64_t table_id, stmt::StmtType stmt_type)
      : table_id_(table_id),
        stmt_type_(stmt_type)
    {}
    virtual ~TableStmtType()
    {}
    inline bool operator==(const TableStmtType &rv) const
    {
      return table_id_ == rv.table_id_;
    }
    inline bool is_mutating() const
    {
      return stmt_type_ != stmt::T_SELECT;
    }
    inline void skip_mutating(stmt::StmtType &saved_stmt_type)
    {
      saved_stmt_type = get_stmt_type();
      set_stmt_type(stmt::T_SELECT);
    }
    inline void restore_mutating(stmt::StmtType saved_stmt_type)
    {
      set_stmt_type(saved_stmt_type);
    }
    inline stmt::StmtType get_stmt_type() const
    {
      return stmt_type_;
    }
    inline void set_stmt_type(stmt::StmtType stmt_type)
    {
      GET_DIAGNOSTIC_INFO->get_ash_stat().stmt_type_ = stmt_type;
      stmt_type_ = stmt_type;
    }
    TO_STRING_KV(K(table_id_),
                 K(stmt_type_));
  private:
    uint64_t table_id_;
    stmt::StmtType stmt_type_;
  };

  static const int64_t MIN_CUR_QUERY_LEN = 512;
  static const int64_t MAX_CUR_QUERY_LEN = 16 * 1024;
  static const int64_t MAX_QUERY_STRING_LEN = 64 * 1024;
  class TransFlags
  {
  public:
    TransFlags() : flags_(0), changed_(false) {}
    virtual ~TransFlags() {}
    inline void reset() { flags_ = 0; }
    inline uint64_t get_flags() const { return flags_; }
    void set_has_exec_inner_dml(bool v) { has_exec_inner_dml_ = v; }
    bool has_exec_inner_dml() const { return has_exec_inner_dml_; }
  private:
    // NOTICE:
    // after 4.1, txn support executed on multiple node
    // if use TransFlags, please add it into session_sync
    // in order to sync it to txn execution node
    union {
      uint64_t flags_;
      struct {
        // has executed dml stmt via inner connection
        // used by PL detect autonomous trasnaction missing commit or rollback
        // will not cross server, do not required to be synced
        bool has_exec_inner_dml_ : 1;
      };
    };
    bool changed_;
  };
  class SqlScopeFlags
  {
  public:
    SqlScopeFlags() : flags_(0) {}
    virtual ~SqlScopeFlags() {}
    inline void reset() { flags_ = 0; }
    inline void set_is_in_user_scope(bool value) { set_flag(value, IS_IN_USER_SCOPE); }
    inline bool is_in_user_scope() const { return flags_ & IS_IN_USER_SCOPE; }
    inline void set_flags(uint64_t value) { flags_ = value; }
    inline uint64_t get_flags() const { return flags_; }
  private:
    inline void set_flag(bool value, uint64_t flag)
    {
      if (value) {
        flags_ |= flag;
      } else {
        flags_ &= ~flag;
      }
      return;
    }
  private:
    // create table as select divided into create table and insert select,
    // This scene is not inner_sql, adding this flag is to distinguish whether insert select is generated by other user sql,
    // For generated always as identity column, external sql is prohibited from doing insert
    // Currently only one scenario uses it, can be extended in the future
    static const uint64_t IS_IN_USER_SCOPE = 1ULL << 0;
    uint64_t flags_;
  };
  class UserScopeGuard
  {
  public:
    UserScopeGuard(SqlScopeFlags &sql_scope_flags) : sql_scope_flags_(sql_scope_flags)
    {
      sql_scope_flags_.set_is_in_user_scope(true);
    }
    ~UserScopeGuard() { sql_scope_flags_.set_is_in_user_scope(false); }
    SqlScopeFlags &sql_scope_flags_;
  };
  enum class ForceRichFormatStatus
  {
    Disable = 0,
    FORCE_ON,
    FORCE_OFF
  };
  // Switching autonomous transactions must switch nested statements, otherwise the context information of statement execution may have changed when switching back to the main transaction, for example:
  // 
  // So in principle TransSavedValue should contain all attributes of StmtSavedValue, consider making the former a subclass of the latter,
  // but there are several attributes that exist in both, but the operations to be performed are different, finally decided to extract the common attributes for processing into
  // Public base class BaseSavedValue, convenient for maximum code reuse, when adding new attributes in the future, similar principles should also be referred to determine which class to place them in.
  class BaseSavedValue
  {
  public:
    BaseSavedValue() : cur_query_(NULL)
    {
      reset();
    }
    ~BaseSavedValue()
    {
      reset();
    }
    inline void reset()
    {
      if (cur_query_ != nullptr) {
        ob_free(cur_query_);
      }
      cur_phy_plan_ = NULL;
      cur_query_len_ = 0;
      cur_query_buf_len_ = 0;
      cur_query_ = NULL;
      total_stmt_tables_.reset();
      cur_stmt_tables_.reset();
      read_uncommited_ = false;
      inc_autocommit_ = false;
      need_serial_exec_ = false;
    }
  public:
    // Original properties of StmtSavedValue
    const ObPhysicalPlan *cur_phy_plan_;
    volatile int64_t cur_query_len_;
//  int64_t cur_query_start_time_;          // used to calculate transaction timeout, if operation in base_save_session interface
                                            // Will cause start_trans to report a transaction timeout failure, not placed in the base class.
    common::ObSEArray<TableStmtType, 4> total_stmt_tables_;
    common::ObSEArray<TableStmtType, 2> cur_stmt_tables_;
//  bool in_transaction_;                   // Corresponds to TransSavedValue's trans_flags_, not placed in the base class.
    bool read_uncommited_;
    bool inc_autocommit_;
    bool need_serial_exec_;
    int64_t cur_query_buf_len_;
    char *cur_query_;
    ForceRichFormatStatus force_rich_format_status_;
  public:
    // Original TransSavedValue properties
//  transaction::ObTxDesc trans_desc_;   // Both have trans_desc, but the operations performed are completely different, so it is not placed in the base class.
//  TransFlags trans_flags_;                // Corresponds to StmtSavedValue's in_transaction_, not placed in the base class.
//  TransResult tx_result_;              // Both have tx_result_, but the operations performed are completely different, so it is not placed in the base class.
//  int64_t nested_count_;                  // Specific attribute, not placed in the base class.
  };
  // for switch stmt.
  class StmtSavedValue : public BaseSavedValue
  {
  public:
    StmtSavedValue()
    {
      reset();
    }
    ~StmtSavedValue()
    {
      reset();
    }
    inline void reset()
    {
      BaseSavedValue::reset();
      tx_result_.reset();
      cur_query_start_time_ = 0;
      in_transaction_ = false;
      stmt_type_ = sql::stmt::StmtType::T_NONE;
    }
  public:
    transaction::ObTxExecResult tx_result_;
    int64_t cur_query_start_time_;
    bool in_transaction_;
    sql::stmt::StmtType stmt_type_;
  };
  // for switch trans.
  class TransSavedValue : public BaseSavedValue
  {
  public:
    TransSavedValue()
    {
      reset();
    }
    void reset()
    {
      BaseSavedValue::reset();
      tx_desc_ = NULL;
      trans_flags_.reset();
      tx_result_.reset();
      nested_count_ = -1;
      xid_.reset();
    }
  public:
    transaction::ObTxDesc *tx_desc_;
    TransFlags trans_flags_;
    transaction::ObTxExecResult tx_result_;
    int64_t nested_count_;
    transaction::ObXATransID xid_;
  };

public:
  ObBasicSessionInfo(const uint64_t tenant_id);
  virtual ~ObBasicSessionInfo();

  virtual int init(uint32_t sessid, uint64_t proxy_sessid,
                   common::ObIAllocator *bucket_allocator, const ObTZInfoMap *tz_info);
  //for test
  virtual int test_init(uint32_t sessid, uint64_t proxy_sessid,
                   common::ObIAllocator *bucket_allocator);
  virtual void destroy();
  //called before put session to freelist: unlock/set invalid
  virtual void reset(bool skip_sys_var = false);
  void reset_user_var();
  void set_tenant_session_mgr(ObTenantSQLSessionMgr *tenant_session_mgr)
  {
    tenant_session_mgr_ = tenant_session_mgr;
  }
  ObTenantSQLSessionMgr *get_tenant_session_mgr() { return tenant_session_mgr_; }
  virtual void clean_status();
  //setters
  int reset_timezone();
  int init_tenant(const common::ObString &tenant_name, const uint64_t tenant_id);
  int set_tenant(const common::ObString &tenant_name, const uint64_t tenant_id);
  int switch_tenant(uint64_t effective_tenant_id);
  int switch_tenant_with_name(uint64_t effective_tenant_id, const common::ObString &tenant_name);
  int set_default_catalog_db(uint64_t catalog_id,
                             uint64_t db_id,
                             const common::ObString &database_name,
                             share::ObSwitchCatalogHelper* switch_catalog_helper = NULL);
  int set_internal_catalog_db(share::ObSwitchCatalogHelper* switch_catalog_helper = NULL);
  bool is_in_external_catalog();
  int set_default_database(const common::ObString &database_name,
                           common::ObCollationType coll_type = common::CS_TYPE_INVALID);
  int reset_default_database() { return set_default_database(""); }
  int update_database_variables(share::schema::ObSchemaGetterGuard *schema_guard);
  int update_max_packet_size();
  int64_t get_thread_id() const { return thread_id_; }
  void set_thread_id(int64_t t) { thread_id_ = t; }
  const char* get_thread_name() const { return thread_name_; }
  void set_valid(const bool valid) {is_valid_ = valid;};
  int64_t get_sys_vars_encode_max_size() { return sys_vars_encode_max_size_; }
  void set_sys_vars_encode_max_size(int64_t size) { sys_vars_encode_max_size_ = size; }
  void set_sql_mode(const ObSQLMode sql_mode)
  {
    // Compatibility mode store in sql_mode_ but controlled by ob_compatibility_mode variable,
    // we can not overwrite it.
    ObSQLMode real_sql_mode = (sql_mode & (~ALL_SMO_COMPACT_MODE)) |
                              (sys_vars_cache_.get_sql_mode() & ALL_SMO_COMPACT_MODE);
    sys_vars_cache_.set_sql_mode(real_sql_mode);
  }
  void set_compatibility_mode(const common::ObCompatibilityMode compat_mode)
  {
    ObSQLMode sql_mode = ob_compatibility_mode_to_sql_mode(compat_mode) |
                         (sys_vars_cache_.get_sql_mode() & ~ALL_SMO_COMPACT_MODE);
    sys_vars_cache_.set_sql_mode(sql_mode);
  }
  void set_global_vars_version(const int64_t modify_time) { global_vars_version_ = modify_time; }
  void set_is_deserialized() { is_deserialized_ = true; }
  bool get_is_deserialized() { return is_deserialized_; }
  void set_exec_min_cluster_version() { exec_min_cluster_version_ = GET_MIN_CLUSTER_VERSION(); }
  uint64_t get_exec_min_cluster_version() const { return exec_min_cluster_version_; }
  // local sys var getters
  inline ObCollationType get_local_collation_connection() const;
  inline ObCollationType get_nls_collation() const;
  inline ObCollationType get_nls_collation_nation() const;
  inline const ObString &get_ob_trace_info() const;
  inline const ObString &get_plsql_ccflags() const;
  inline const ObString &get_iso_nls_currency() const;
  inline const ObString &get_log_row_value_option() const;
  int64_t get_default_lob_inrow_threshold() const;
  bool get_local_autocommit() const;
  uint64_t get_local_auto_increment_increment() const;
  uint64_t get_local_auto_increment_offset() const;
  uint64_t get_local_last_insert_id() const;
  void set_local_ob_enable_pl_cache(bool v) { sys_vars_cache_.set_ob_enable_pl_cache(v); }
  bool get_local_ob_enable_pl_cache() const;
  bool get_local_ob_enable_plan_cache() const;
  bool get_local_ob_enable_sql_audit() const;
  bool get_local_ob_enable_parameter_anonymous_block() const;
  bool get_local_ob_enable_ps_parameter_anonymous_block() const;
  bool get_local_cursor_sharing_mode() const;
  ObLengthSemantics get_local_nls_length_semantics() const;
  ObLengthSemantics get_actual_nls_length_semantics() const;
  int64_t get_local_ob_org_cluster_id() const;
  int64_t get_local_timestamp() const;
  const common::ObString get_local_nls_date_format() const;
  const common::ObString get_local_nls_timestamp_format() const;
  const common::ObString get_local_nls_timestamp_tz_format() const;
  int get_local_nls_format(const ObObjType type, ObString &format_str) const;
  int set_time_zone(const common::ObString &str_val, const bool is_oralce_mode,
                    const bool need_check_valid /* true */);
  void init_use_rich_format()
  {
    config_use_rich_format_ = GCONF._global_enable_rich_vector_format;
    if (!config_use_rich_format_) {
      use_rich_vector_format_ = false;
      force_rich_vector_format_ = ForceRichFormatStatus::FORCE_OFF;
    } else {
      use_rich_vector_format_ = sys_vars_cache_.get_enable_rich_vector_format();
      force_rich_vector_format_ = ForceRichFormatStatus::Disable;
    }
  }
  bool is_force_off_rich_format() {
    return force_rich_vector_format_ == ForceRichFormatStatus::FORCE_OFF;
  }
  bool use_rich_format() const {
    if (force_rich_vector_format_ != ForceRichFormatStatus::Disable) {
      return force_rich_vector_format_ == ForceRichFormatStatus::FORCE_ON;
    } else {
      return use_rich_vector_format_;
    }
  }

  bool config_use_rich_format() { return config_use_rich_format_; }

  bool initial_use_rich_format() const {
    return use_rich_vector_format_;
  }

  ObBasicSessionInfo::ForceRichFormatStatus get_force_rich_format_status() const
  {
    return force_rich_vector_format_;
  }

  void set_force_rich_format(ObBasicSessionInfo::ForceRichFormatStatus status)
  {
    force_rich_vector_format_ = status;
  }
  //getters
  const common::ObString get_tenant_name() const;
  uint64_t get_priv_tenant_id() const { return tenant_id_; }
  const common::ObString get_effective_tenant_name() const;
  // About the usage of various tenant_id, refer to
  uint64_t get_effective_tenant_id() const { return effective_tenant_id_; }
  // RPC framework use rpc_tenant_id() to deliver remote/distribute tasks.
  void set_rpc_tenant_id(uint64_t tenant_id) { rpc_tenant_id_ = tenant_id; }
  uint64_t get_rpc_tenant_id() const
  {
    return rpc_tenant_id_ != 0 ? rpc_tenant_id_ : effective_tenant_id_;
  }
  uint64_t get_login_tenant_id() const { return tenant_id_; }
  void set_login_tenant_id(uint64_t tenant_id) { tenant_id_ = tenant_id; }
  bool is_tenant_changed() const { return tenant_id_ != effective_tenant_id_; }
  int set_autocommit(bool autocommit);
  int get_autocommit(bool &autocommit) const
  {
    autocommit = sys_vars_cache_.get_autocommit();
    return common::OB_SUCCESS;
  }
  int get_explicit_defaults_for_timestamp(bool &explicit_defaults_for_timestamp) const;
  int get_sql_auto_is_null(bool &sql_auto_is_null) const;
  int get_is_result_accurate(bool &is_result_accurate) const
  {
    is_result_accurate = sys_vars_cache_.get_is_result_accurate();
    return common::OB_SUCCESS;
  }
  common::ObIArray<uint64_t>& get_enable_role_ids() { return enable_role_ids_; }
  const common::ObIArray<uint64_t>& get_enable_role_ids() const { return enable_role_ids_; }
  int get_show_ddl_in_compat_mode(bool &show_ddl_in_compat_mode) const;
  int get_ob_hnsw_ef_search(uint64_t &ob_hnsw_ef_search) const;
  int get_ob_ivf_nprobes(uint64_t &ob_ivf_nprobes) const;
  int get_ob_sparse_drop_ratio_search(uint64_t &ob_sparse_drop_ratio_search) const;
  int get_sql_quote_show_create(bool &sql_quote_show_create) const;
  common::ObConsistencyLevel get_consistency_level() const { return consistency_level_; };
  bool is_zombie() const { return SESSION_KILLED == get_session_state();}
  bool is_query_killed() const;
  bool is_valid() const { return is_valid_; };
  uint64_t get_user_id() const { return user_id_; }
  bool is_auditor_user() const { return is_ora_auditor_user(user_id_); };
  bool is_lbacsys_user() const { return is_ora_lbacsys_user(user_id_); };
  bool is_oracle_sys_user() const { return is_ora_sys_user(user_id_); };
  bool is_mysql_root_user() const { return is_root_user(user_id_); };
  bool is_restore_user() const { return  0 == thread_data_.user_name_.case_compare(common::OB_RESTORE_USER_NAME); };
  bool is_proxy_sys_user() const
  {
    return OB_SYS_TENANT_ID == tenant_id_;
  };
  const common::ObString get_database_name() const;
  inline int get_database_id(uint64_t &db_id) const { db_id = database_id_; return common::OB_SUCCESS; }
  inline uint64_t get_database_id() const { return database_id_; }
  inline void set_database_id(uint64_t db_id) { database_id_ = db_id; }
  inline const ObQueryRetryInfo &get_retry_info() const { return retry_info_; }
  inline ObQueryRetryInfo &get_retry_info_for_update() { return retry_info_; }
  inline const common::ObCurTraceId::TraceId &get_last_query_trace_id() const
  { return last_query_trace_id_; }
  int check_and_init_retry_info(const common::ObCurTraceId::TraceId &cur_trace_id,
                                const common::ObString &sql);
  void check_and_reset_retry_info(const common::ObCurTraceId::TraceId &cur_trace_id,
                                  bool is_packet_retry)
  {
    // 1.If it is a local retry, by the time it reaches here, all retries have been completed, so just reset the retry info in the session;
    // 2.If it is retried by putting back into the queue (including the case of large queries being put back into the queue), the retry info in the session should not be reset;
    // 3.If it is not a retry, the retry info in the session needs to be reset here.
    // Note, here we need to reset the retry info to not init state, so we should call reset, not clear.
    if (!is_packet_retry) {
      retry_info_.reset();
    }
    last_query_trace_id_.set(cur_trace_id);
  }
  const common::ObLogIdLevelMap *get_log_id_level_map() const;
  const common::ObString &get_client_version() const { return client_version_; }
  const common::ObString &get_driver_version() const { return driver_version_; }
  int get_tx_timeout(int64_t &tx_timeout) const
  {
    tx_timeout = sys_vars_cache_.get_ob_trx_timeout();
    return common::OB_SUCCESS;
  }
  int get_query_timeout(int64_t &query_timeout) const
  {
    query_timeout = sys_vars_cache_.get_ob_query_timeout();
    return common::OB_SUCCESS;
  }
  int64_t get_query_timeout_ts() const; // Get the absolute time for current query timeout
  int64_t get_trx_lock_timeout() const
  {
    return sys_vars_cache_.get_ob_trx_lock_timeout();
  }
  int64_t get_ob_max_read_stale_time() {
    return sys_vars_cache_.get_ob_max_read_stale_time();
  }
  int get_sql_throttle_current_priority(int64_t &sql_throttle_current_priority)
  {
    sql_throttle_current_priority = sys_vars_cache_.get_sql_throttle_current_priority();
    return common::OB_SUCCESS;
  }
  int get_ob_last_schema_version(int64_t &ob_last_schema_version)
  {
    ob_last_schema_version = sys_vars_cache_.get_ob_last_schema_version();
    return common::OB_SUCCESS;
  }
  int get_pl_block_timeout(int64_t &pl_block_timeout) const;
  int get_binlog_row_image(int64_t &binlog_row_image) const
  {
    binlog_row_image = sys_vars_cache_.get_binlog_row_image();
    return common::OB_SUCCESS;
  }
  int get_sql_select_limit(int64_t &sql_select_limit) const
  {
    sql_select_limit = sys_vars_cache_.get_sql_select_limit();
    return common::OB_SUCCESS;
  }
  int get_oracle_sql_select_limit(int64_t &oracle_sql_select_limit) const
  {
    oracle_sql_select_limit = sys_vars_cache_.get_oracle_sql_select_limit();
    return common::OB_SUCCESS;
  }
  // session retains compatible mode, mainly used for passing mode, convenient for subsequent guard switch, such as inner sql connection etc
  // Other places where mode is needed please use is_oracle|mysql_mode on the thread
  // At the same time, you can use check_compatibility_mode to check if the mode on the thread is consistent with the mode on the session
  ObCompatibilityMode get_compatibility_mode() const
  {
    return ob_sql_mode_to_compatibility_mode(get_sql_mode());
  }
  bool is_oracle_compatible() const { return ORACLE_MODE == get_compatibility_mode(); }
  ObSQLMode get_sql_mode() const { return sys_vars_cache_.get_sql_mode(); }
  int get_div_precision_increment(int64_t &div_precision_increment) const;
  int get_character_set_client(common::ObCharsetType &character_set_client) const;
  int get_character_set_connection(common::ObCharsetType &character_set_connection) const;
  int get_ncharacter_set_connection(common::ObCharsetType &ncharacter_set_connection) const;
  int get_character_set_results(common::ObCharsetType &character_set_results) const;
  inline int get_collation_connection(common::ObCollationType &collation_connection) const
  {
    collation_connection = get_local_collation_connection();
    return common::OB_SUCCESS;
  }
  int get_collation_database(common::ObCollationType &collation_database) const;
  int get_collation_server(common::ObCollationType &collation_server) const;
  int get_foreign_key_checks(int64_t &foreign_key_checks) const
  {
    foreign_key_checks = sys_vars_cache_.get_foreign_key_checks();
    return common::OB_SUCCESS;
  }
  int get_default_password_lifetime(uint64_t &default_password_lifetime) const
  {
    default_password_lifetime = sys_vars_cache_.get_default_password_lifetime();
    return common::OB_SUCCESS;
  }
  int get_use_plan_baseline(bool &v) const
  {
    v = sys_vars_cache_.get_optimizer_use_sql_plan_baselines();
    return common::OB_SUCCESS;
  }
  int get_nlj_batching_enabled(bool &v) const;
  int get_optimizer_features_enable_version(uint64_t &version) const;
  int get_enable_parallel_dml(bool &v) const;
  int get_enable_parallel_query(bool &v) const;
  int get_enable_parallel_ddl(bool &v) const;
  int get_force_parallel_query_dop(uint64_t &v) const;
  int get_parallel_degree_policy_enable_auto_dop(bool &v) const;
  int get_force_parallel_dml_dop(uint64_t &v) const;
  int get_force_parallel_ddl_dop(uint64_t &v) const;
  int get_partial_rollup_pushdown(int64_t &partial_rollup) const;
  int get_px_shared_hash_join(bool &shared_hash_join) const;
  int get_secure_file_priv(common::ObString &v) const;
  int get_sql_safe_updates(bool &v) const;
  int get_opt_dynamic_sampling(uint64_t &v) const;
  int get_regexp_stack_limit(int64_t &v) const;
  int get_regexp_time_limit(int64_t &v) const;
  int get_regexp_session_vars(ObExprRegexpSessionVariables &vars) const;
  int get_activate_all_role_on_login(bool &v) const;
  int get_mview_refresh_dop(uint64_t &v) const;
  int update_timezone_info();
  const common::ObTimeZoneInfo *get_timezone_info() const { return tz_info_wrap_.get_time_zone_info(); }
  const common::ObTimeZoneInfoWrap &get_tz_info_wrap() const { return tz_info_wrap_; }
  inline int set_tz_info_wrap(const common::ObTimeZoneInfoWrap &other) { return tz_info_wrap_.deep_copy(other); }
  inline void set_nls_formats(const common::ObString *nls_formats)
  {
    sys_vars_cache_.set_nls_date_format(nls_formats[ObNLSFormatEnum::NLS_DATE]);
    sys_vars_cache_.set_nls_timestamp_format(nls_formats[ObNLSFormatEnum::NLS_TIMESTAMP]);
    sys_vars_cache_.set_nls_timestamp_tz_format(nls_formats[ObNLSFormatEnum::NLS_TIMESTAMP_TZ]);
  }
  int get_influence_plan_sys_var(ObSysVarInPC &sys_vars) const;
  const common::ObString &get_sys_var_in_pc_str() const { return sys_var_in_pc_str_; }
  const common::ObString &get_config_in_pc_str() const { return config_in_pc_str_; }
  uint64_t get_sys_var_config_hash_val() const { return sys_var_config_hash_val_; }
  void eval_sys_var_config_hash_val();
  int gen_sys_var_in_pc_str();
  int gen_configs_in_pc_str();
  uint32_t get_server_sid() const { return sessid_; }
  // Used for view or function compatibility display.
  uint32_t get_sid() const
  {
    return client_sessid_ == INVALID_SESSID ? sessid_ : client_sessid_;
  }
  uint32_t get_client_sid() const { return client_sessid_; }
  inline void set_client_sessid(uint32_t client_sessid)
  {
    client_sessid_ = client_sessid;
  }
  uint64_t get_client_create_time() const { return client_create_time_; }
  inline void set_client_create_time(uint64_t client_create_time)
  {
    client_create_time_ = client_create_time;
  }
  static int get_client_sid(uint32_t server_sid, uint32_t& client_sid); // get client sid by server sid
  uint64_t get_proxy_sessid() const { return proxy_sessid_; }
  uint64_t get_sessid_for_table() const { return is_obproxy_mode()? get_proxy_sessid() : (is_master_session() ? get_sid() : get_master_sessid()); } // used for temporary table, query create table when session id acquisition
  uint32_t get_master_sessid() const { return master_sessid_; }
  inline const common::ObString get_sess_bt() const { return ObString::make_string(sess_bt_buff_); }
  inline int32_t get_sess_ref_cnt() const { return sess_ref_cnt_; }
  void on_get_session();
  void on_revert_session();
  common::ObString get_ssl_cipher() const { return ObString::make_string(ssl_cipher_buff_); }
  void set_ssl_cipher(const char *value)
  {
    const size_t min_len = std::min(sizeof(ssl_cipher_buff_) - 1, strlen(value));
    MEMCPY(ssl_cipher_buff_, value, min_len);
    ssl_cipher_buff_[min_len] = '\0';
  }
  // Master session: receiving user SQL text sessions.
  // Slave session: receiving sql plan sessions, e.g.: remote executing,
  // distribute executing sessions.
  bool is_master_session() const { return INVALID_SESSID == master_sessid_; }
  common::ObDSSessionActions &get_debug_sync_actions() { return debug_sync_actions_; }
  int64_t get_global_vars_version() const { return global_vars_version_; }
  inline common::ObIArray<int64_t> &get_influence_plan_var_indexs() { return influence_plan_var_indexs_; }
  int64_t get_influence_plan_var_count() const { return influence_plan_var_indexs_.count(); }
  int get_pc_mem_conf(ObPCMemPctConf &pc_mem_conf);


  /// @{ thread_data_ related: }
  int set_user(const common::ObString &user_name, const common::ObString &host_name, const uint64_t user_id);
  int set_real_client_ip(const common::ObString &client_ip);
  int set_real_client_ip_and_port(const common::ObString &client_ip, int32_t client_addr_port);
  const common::ObString &get_user_name() const { return thread_data_.user_name_;}
  const common::ObString &get_host_name() const { return thread_data_.host_name_;}
  const common::ObString &get_client_ip() const { return thread_data_.client_ip_;}
  const common::ObString &get_user_at_host() const { return thread_data_.user_at_host_name_;}
  const common::ObString &get_user_at_client_ip() const { return thread_data_.user_at_client_ip_;}
  void set_client_addr_port(const int32_t client_addr_port)
  {
    thread_data_.client_addr_port_ = client_addr_port;
  };
  int32_t get_client_addr_port() const { return thread_data_.client_addr_port_; };
  rpc::ObSqlSockDesc& get_sock_desc() { return thread_data_.sock_desc_;}
  observer::ObSMConnection *get_sm_connection();
  void set_peer_addr(common::ObAddr peer_addr)
  {
    LockGuard lock_guard(thread_data_mutex_);
    thread_data_.peer_addr_ = peer_addr;
  }
  void set_client_addr(common::ObAddr client_addr)
  {
    LockGuard lock_guard(thread_data_mutex_);
    thread_data_.client_addr_ = client_addr;
  }
  const common::ObAddr &get_peer_addr() const {return thread_data_.peer_addr_;}
  const common::ObAddr &get_client_addr() const {return thread_data_.client_addr_;}
  const common::ObAddr &get_user_client_addr() const {return thread_data_.user_client_addr_;}
  common::ObAddr get_proxy_addr() const
  {
    const int32_t ip = static_cast<int32_t>((proxy_sessid_ >> 32) & 0xFFFFFFFF);
    const int32_t port = static_cast<int32_t>((proxy_sessid_ >> 16) & 0xFFFF);
    return ObAddr(ip, port);
  }
  void set_query_start_time(int64_t time)
  {
    LockGuard lock_guard(thread_data_mutex_);
    thread_data_.cur_query_start_time_ = time;
  }
  int64_t get_query_start_time() const { return thread_data_.cur_query_start_time_; }
  int64_t get_cur_state_start_time() const { return thread_data_.cur_state_start_time_; }
  void set_interactive(bool is_interactive)
  {
    LockGuard lock_guard(thread_data_mutex_);
    thread_data_.is_interactive_ = is_interactive;
  }
  bool get_interactive() const { return thread_data_.is_interactive_; }
  void update_last_active_time()
  {
    LockGuard lock_guard(thread_data_mutex_);
    thread_data_.last_active_time_ = ::oceanbase::common::ObTimeUtility::current_time();
  }
  int64_t get_last_active_time() const { return thread_data_.last_active_time_; }
  void set_disconnect_state(ObDisconnectState dis_state)
  {
    LockGuard lock_guard(thread_data_mutex_);
    thread_data_.dis_state_ = dis_state;
  }
  int set_session_state(ObSQLSessionState state);
  int check_session_status();
  ObDisconnectState get_disconnect_state() const { return thread_data_.dis_state_;}
  ObSQLSessionState get_session_state() const { return thread_data_.state_;}
  const char *get_session_state_str()const;
  void set_mysql_cmd(obmysql::ObMySQLCmd mysql_cmd)
  {
    LockGuard lock_guard(thread_data_mutex_);
    thread_data_.mysql_cmd_ = mysql_cmd;
  }
  void set_session_in_retry(ObSessionRetryStatus is_retry)
  {
    LockGuard lock_guard(thread_data_mutex_);
    if (OB_LIKELY(SESS_NOT_IN_RETRY == is_retry ||
                  SESS_IN_RETRY_FOR_DUP_TBL != thread_data_.is_in_retry_)) {
      thread_data_.is_in_retry_ = is_retry;
    } else {
      // if the last retry is for duplicate table
      // and the SQL is retried again
      // we still keep the retry for dup table status.
      thread_data_.is_in_retry_ = SESS_IN_RETRY_FOR_DUP_TBL;
    }
  }

  void set_session_in_retry(bool is_retry, int ret)
  {
    ObSessionRetryStatus status;
    if (!is_retry) {
      status = sql::SESS_NOT_IN_RETRY;
    } else if (is_select_dup_follow_replic_err(ret) ||
               OB_NOT_MASTER == ret) {
      status = SESS_IN_RETRY_FOR_DUP_TBL;
    } else {
      status = SESS_IN_RETRY;
    }
    set_session_in_retry(status);
  }
  bool get_is_in_retry() {
    return SESS_NOT_IN_RETRY != thread_data_.is_in_retry_;
  }
  bool get_is_in_retry() const {
    return SESS_NOT_IN_RETRY != thread_data_.is_in_retry_;
  }
  bool get_is_in_retry_for_dup_tbl() {
    return SESS_IN_RETRY_FOR_DUP_TBL == thread_data_.is_in_retry_;
  }
  void set_retry_active_time(int64_t time)
  {
    LockGuard lock_guard(thread_data_mutex_);
    thread_data_.retry_active_time_ = time;
  }
  int64_t get_retry_active_time() const { return thread_data_.retry_active_time_; }
  void set_is_request_end(bool is_request_end)
  {
    LockGuard lock_guard(thread_data_mutex_);
    thread_data_.is_request_end_ = is_request_end;
  }
  bool get_is_request_end() const { return thread_data_.is_request_end_; }
  obmysql::ObMySQLCmd get_mysql_cmd() const { return thread_data_.mysql_cmd_; }
  char const *get_mysql_cmd_str() const { return obmysql::get_mysql_cmd_str(thread_data_.mysql_cmd_); }
  int store_query_string(const common::ObString &stmt);
  int store_top_query_string(const common::ObString &stmt);
  void reset_query_string();
  void reset_top_query_string();
  void set_session_sleep();
  // for SQL entry point
  int set_session_active(const ObString &sql,
                         const int64_t query_receive_ts,
                         const int64_t last_active_time_ts,
                         obmysql::ObMySQLCmd cmd = obmysql::ObMySQLCmd::COM_QUERY);
  // for remote / px task
  int set_session_active(const ObString &label,
                         obmysql::ObMySQLCmd cmd);
  int set_session_active();
  const common::ObString get_current_query_string() const;
  const common::ObString get_top_query_string() const;
  uint64_t get_current_statement_id() const { return thread_data_.cur_statement_id_; }
  int is_timeout(bool &is_timeout);
  int is_trx_commit_timeout(transaction::ObITxCallback *&callback, int &retcode);
  int is_trx_idle_timeout(bool &is_timeout);
  int64_t get_wait_timeout() { return thread_data_.wait_timeout_; }
  int64_t get_interactive_timeout() { return thread_data_.interactive_timeout_; }
  int64_t get_max_packet_size() {return thread_data_.max_packet_size_; }
  // lock
  common::ObRecursiveMutex &get_query_lock() { return query_mutex_; }
  common::ObRecursiveMutex &get_thread_data_lock() { return thread_data_mutex_; }
  int try_lock_query() { return query_mutex_.trylock(); }
  int try_lock_thread_data() { return thread_data_mutex_.trylock(); }
  int unlock_query() { return query_mutex_.unlock(); }
  int unlock_thread_data() { return thread_data_mutex_.unlock(); }
  /// @{ system variables related:
  static int get_global_sys_variable(const ObBasicSessionInfo *session,
                                     common::ObIAllocator &calc_buf,
                                     const common::ObString &var_name,
                                     common::ObObj &val);
  static int get_global_sys_variable(uint64_t actual_tenant_id,
                                     common::ObIAllocator &calc_buf,
                                     const common::ObDataTypeCastParams &dtc_params,
                                     const common::ObString &var_name,
                                     common::ObObj &val);
  share::ObBasicSysVar *get_sys_var(const int64_t idx);
  int64_t get_sys_var_count() const { return share::ObSysVarFactory::ALL_SYS_VARS_COUNT; }
  // deserialized scene need use base_value as baseline.
  int load_default_sys_variable(const bool print_info_log, const bool is_sys_tenant, bool is_deserialized = false);
  int load_default_configs_in_pc();
  int update_query_sensitive_system_variable(share::schema::ObSchemaGetterGuard &schema_guard);
  int process_variable_for_tenant(const common::ObString &var, common::ObObj &val);
  int load_sys_variable(common::ObIAllocator &calc_buf,
                        const common::ObString &name,
                        const common::ObObj &type,
                        const common::ObObj &value,
                        const common::ObObj &min_val,
                        const common::ObObj &max_val,
                        const int64_t flags,
                        bool is_from_sys_table,
                        int64_t store_idx = -1);
  int load_sys_variable(common::ObIAllocator &calc_buf,
                        const common::ObString &name,
                        const int64_t dtype,
                        const common::ObString &value,
                        const common::ObString &min_val,
                        const common::ObString &max_val,
                        const int64_t flags,
                        bool is_from_sys_table);
  // Convert varchar type value, max_val, min_val to the corresponding type ObObj
  int cast_sys_variable(common::ObIAllocator &calc_buf,
                        bool is_range_value,
                        const share::ObSysVarClassType sys_var_id,
                        const common::ObObj &type,
                        const common::ObObj &value,
                        int64_t flags,
                        common::ObObj &out_type,
                        common::ObObj &out_value);
  ///@{ Update the value of system variables
  // Update system variables according to kv pairs
  int update_sys_variable(const common::ObString &var, const common::ObString &val);
  int update_sys_variable(const share::ObSysVarClassType sys_var_id, const common::ObObj &val);
  int update_sys_variable(const share::ObSysVarClassType sys_var_id, const common::ObString &val);
  int update_sys_variable(const share::ObSysVarClassType sys_var_id, int64_t val);
  /// @note get system variables by id is prefered
  int update_sys_variable_by_name(const common::ObString &var, const common::ObObj &val);
  int update_sys_variable_by_name(const common::ObString &var, int64_t val);
  //int update_sys_variable(const char* const var, const common::ObString &v);
  ///@}
  ///@{ Get the value of the system variable
  int get_sys_variable(const share::ObSysVarClassType sys_var_id, common::ObObj &val) const;
  int get_sys_variable(const share::ObSysVarClassType sys_var_id, common::ObString &val) const;
  int get_sys_variable(const share::ObSysVarClassType sys_var_id, int64_t &val) const;
  int get_sys_variable(const share::ObSysVarClassType sys_var_id, uint64_t &val) const;
  int get_sys_variable(const share::ObSysVarClassType sys_var_id, bool &val) const;
  int get_sys_variable(const share::ObSysVarClassType sys_var_id, share::ObBasicSysVar *&val) const;
  /// @note get system variables by id is prefered
  int get_sys_variable_by_name(const common::ObString &var, common::ObObj &val) const;
  int get_sys_variable_by_name(const common::ObString &var, share::ObBasicSysVar *&val) const;
  int get_sys_variable_by_name(const common::ObString &var, int64_t &val) const;
  ///@}

  int reset_sys_vars();

  /// check the existence of the system variable
  int sys_variable_exists(const common::ObString &var, bool &is_exist) const;
  int sys_variable_exists(const share::ObSysVarClassType sys_var_id, bool &is_exist) const;

  int set_client_identifier(const common::ObString &client_identifier);
  const common::ObString& get_client_identifier() const { return client_identifier_; }
  common::ObString &get_client_identifier_for_update() { return client_identifier_; }
  int init_client_identifier();
  // session serialization optimization
  typedef common::ObSEArray<share::ObSysVarClassType, 64> SysVarIds;
  class SysVarIncInfo
  {
    OB_UNIS_VERSION_V(1);
  public:
    SysVarIncInfo();
    virtual ~SysVarIncInfo();
    int add_sys_var_id(share::ObSysVarClassType sys_var_id);
    bool all_has_sys_var_id(share::ObSysVarClassType sys_var_id) const;
    const SysVarIds &get_all_sys_var_ids() const;
    int assign(const SysVarIncInfo &other);
    int reset();
    TO_STRING_KV(K(all_sys_var_ids_));
  private:
    SysVarIds all_sys_var_ids_;
  };
  static int init_sys_vars_cache_base_values();
  int load_all_sys_vars_default();
  int load_all_sys_vars(share::schema::ObSchemaGetterGuard &schema_guard);
  int load_all_sys_vars(const share::schema::ObSysVariableSchema &sys_var_schema, bool sys_var_created);
  int clean_all_sys_vars();
  SysVarIncInfo sys_var_inc_info_;
  const ObString get_cur_sql_id() const { return ObString(sql_id_); }
  void get_cur_sql_id(char *sql_id_buf, int64_t sql_id_buf_size) const;
  void set_cur_sql_id(char *sql_id);
  void reset_cur_sql_id() { sql_id_[0] = '\0'; }
  int set_cur_phy_plan(const ObPhysicalPlan *cur_phy_plan);
  virtual void set_ash_stat_value(ObActiveSessionStat &ash_stat);
  void reset_cur_phy_plan_to_null();

  void get_flt_span_id(ObString &span_id) const;
  void get_flt_trace_id(ObString &trace_id) const;
  int set_flt_span_id(ObString span_id);
  int set_flt_trace_id(ObString trace_id);
  const ObString &get_last_flt_trace_id() const;
  int set_last_flt_trace_id(const common::ObString &trace_id);
  const ObString &get_last_flt_span_id() const;
  int set_last_flt_span_id(const common::ObString &span_id);
  bool is_row_traceformat() const { return flt_vars_.row_traceformat_; }
  void set_is_row_traceformat(bool v) { flt_vars_.row_traceformat_ = v; }
  bool is_query_trc_granuality() const { return sys_vars_cache_.get_ob_enable_trace_log()?
                                            true:flt_vars_.trc_granuality_ == ObTraceGranularity::QUERY_LEVEL; }
  void set_trc_granuality(ObTraceGranularity trc_gra) { flt_vars_.trc_granuality_ = trc_gra; }
  // @pre system variable existsofcaseunder
  // @synopsis Get the type of this variable based on the variable name
  // @param var_name
  // @returns
  common::ObObjType get_sys_variable_type(const common::ObString &var_name) const;
  // The following helper function is for conveniently viewing the value of a system variable
  int if_aggr_pushdown_allowed(bool &aggr_pushdown_allowed) const;
  int is_transformation_enabled(bool &transformation_enabled) const;
  int get_query_rewrite_enabled(int64_t &query_rewrite_enabled) const;
  int get_query_rewrite_integrity(int64_t &query_rewrite_integrity) const;
  int is_serial_set_order_forced(bool &force_set_order, bool is_oracle_mode) const;
  int is_old_charset_aggregation_enabled(bool &is_enable) const;
  int is_storage_estimation_enabled(bool &storage_estimation_enabled) const;
  bool is_use_trace_log() const
  {
    return sys_vars_cache_.get_ob_enable_trace_log();
  }
  int is_use_transmission_checksum(bool &use_transmission_checksum) const;
  int is_select_index_enabled(bool &select_index_enabled) const;
  int get_name_case_mode(common::ObNameCaseMode &case_mode) const;
  int get_init_connect(common::ObString &str) const;
  int get_locale_name(common::ObString &str) const;
  int get_optimizer_cost_based_transformation(int64_t &cbqt_policy) const;
  int is_push_join_predicate_enabled(bool &push_join_predicate_enabled) const;
  /// @}

  ///@{ user variables related:
  sql::ObSessionValMap &get_user_var_val_map() {return user_var_val_map_;}
  const sql::ObSessionValMap &get_user_var_val_map() const {return user_var_val_map_;}
  int replace_user_variable(const common::ObString &var, const ObSessionVariable &val, bool need_track = true);
  int replace_user_variables(const ObSessionValMap &user_var_map);
  int remove_user_variable(const common::ObString &var);
  int get_user_variable(const common::ObString &var, ObSessionVariable &val) const;
  int get_user_variable_value(const common::ObString &var, common::ObObj &val) const;
  const ObSessionVariable *get_user_variable(const common::ObString &var) const;
  const common::ObObj *get_user_variable_value(const common::ObString &var) const;
  bool user_variable_exists(const common::ObString &var) const;
  inline void set_need_reset_package(bool need_reset) { need_reset_package_ = need_reset; }
  bool need_reset_package() { return need_reset_package_; }
  /// @}

  inline static ObDataTypeCastParams create_dtc_params(const ObBasicSessionInfo *session_info)
  {
    return OB_NOT_NULL(session_info) ? session_info->get_dtc_params()
                                     : ObDataTypeCastParams();
  }

  inline ObDataTypeCastParams get_dtc_params() const
  {
    return ObDataTypeCastParams(get_timezone_info(),
                                get_local_nls_date_format(),
                                get_local_nls_timestamp_format(),
                                get_local_nls_timestamp_tz_format(),
                                get_nls_collation(),
                                get_nls_collation_nation(),
                                get_local_collation_connection());
  }

  inline ObCharsets4Parser get_charsets4parser() const {
    ObCharsets4Parser charsets4parser;
    charsets4parser.string_collation_ = get_local_collation_connection();
    charsets4parser.nls_collation_ = get_nls_collation();
    return charsets4parser;
  }

  inline ObSessionNLSParams get_session_nls_params() const
  {
    ObSessionNLSParams session_nls_params;
    session_nls_params.nls_length_semantics_ = get_actual_nls_length_semantics();
    session_nls_params.nls_collation_ = get_nls_collation();
    session_nls_params.nls_nation_collation_ = get_nls_collation_nation();
    return session_nls_params;
  }

  inline ObObjPrintParams create_obj_print_params() const
  {
    ObObjPrintParams res(get_timezone_info(), get_local_collation_connection());
    res.print_origin_stmt_ = true;
    return res;
  }

  // client mode related
  void set_client_mode(const common::ObClientMode mode) { client_mode_ = mode; }
  common::ObClientMode get_client_mode() const { return client_mode_; }
  bool is_java_client_mode() const { return common::OB_JAVA_CLIENT_MODE == client_mode_; }
  bool is_obproxy_mode() const { return common::OB_PROXY_CLIENT_MODE == client_mode_; }

  int64_t to_string(char *buffer, const int64_t length) const;

  static const char* source_to_string(const int64_t type)
  {
    const char* res = "UNKOWN";
    switch (type) {
      case UNKOWN:
	res = "UNKOWN";
	break;
      case AUTO_PLAN:
	res = "AUTO_PLAN";
	break;
      case PROXY_INPUT:
	res = "PROXY_INPUT";
	break;
      case LAST_STMT:
	res = "LAST_STMT";
	break;
      default:
	res = "UNKOWN";
	break;
    }
    return res;
  }

  /// @{ TRACE_SESSION_INFO related:
  struct ChangedVar {
    ChangedVar() : id_(), old_val_() {}
    ChangedVar(share::ObSysVarClassType id, const ObObj& val) :
      id_(id), old_val_(val) {}
    share::ObSysVarClassType id_;
    ObObj old_val_;   // record the old val, used to compare if the final value has changed
    TO_STRING_KV(K(id_), K(old_val_));
  };
  void reset_session_changed_info();
  bool is_already_tracked(
    const share::ObSysVarClassType &sys_var_id, const common::ObIArray<ChangedVar> &array) const;
  bool is_already_tracked(
    const common::ObString &name, const common::ObIArray<common::ObString> &array) const;
  int add_changed_sys_var(const share::ObSysVarClassType &sys_var_id, const common::ObObj &old_val,
                          common::ObIArray<ChangedVar> &array);
  int add_changed_user_var(const common::ObString &name, common::ObIArray<common::ObString> &array);
  int track_sys_var(const share::ObSysVarClassType &sys_var_id, const common::ObObj &old_val);
  int track_user_var(const common::ObString &user_var);
  int remove_changed_user_var(const common::ObString &user_var);
  int is_sys_var_actully_changed(const share::ObSysVarClassType &sys_var_id,
                                 const common::ObObj &old_val,
                                 common::ObObj &new_val,
                                 bool &changed);
  inline bool is_sys_var_changed() const { return !changed_sys_vars_.empty(); }
  inline bool is_user_var_changed() const { return !changed_user_vars_.empty(); }
  inline bool is_database_changed() const { return is_database_changed_; }
  inline bool exist_client_feedback() const { return !feedback_manager_.is_empty(); }
  inline bool is_session_var_changed() const { return (is_sys_var_changed() || is_user_var_changed() || exist_client_feedback()); }
  inline bool is_session_info_changed() const { return (is_session_var_changed() || is_database_changed()); }
  const inline common::ObIArray<ChangedVar> &get_changed_sys_var() const { return changed_sys_vars_; }
  const inline common::ObIArray<common::ObString> &get_changed_user_var() const { return changed_user_vars_; }

  inline void set_capability(const obmysql::ObMySQLCapabilityFlags cap) { capability_ = cap; }
  inline void set_client_attrbuite_capability(const uint64_t cap) { client_attribute_capability_.capability_ = cap; }
  inline uint64_t get_client_attrbuite_capability() { return client_attribute_capability_.capability_; }
  inline obmysql::ObMySQLCapabilityFlags get_capability() const { return capability_; }
  inline bool is_track_session_info() const { return capability_.cap_flags_.OB_CLIENT_SESSION_TRACK; }

  inline bool is_client_return_rowid() const
  {
    return capability_.cap_flags_.OB_CLIENT_RETURN_HIDDEN_ROWID;
  }

  inline void set_client_return_rowid(bool flag)
  {
    capability_.cap_flags_.OB_CLIENT_RETURN_HIDDEN_ROWID = (flag ? 1 : 0);
  }

  inline bool is_client_use_lob_locator() const
  {
    return capability_.cap_flags_.OB_CLIENT_USE_LOB_LOCATOR;
  }

  // NOTICE: Don't use this function, this is only used in pl clob/blob for oracle tenant
  inline void set_client_use_lob_locator(bool flag)
  {
    capability_.cap_flags_.OB_CLIENT_USE_LOB_LOCATOR = (flag ? 1 : 0);
  }

  inline bool is_client_support_lob_locatorv2() const
  {
    return client_attribute_capability_.cap_flags_.OB_CLIENT_CAP_OB_LOB_LOCATOR_V2;
  }

  // NOTICE: Don't use this function, this is only used in pl clob/blob for oracle tenant
  inline void set_client_support_lob_locatorv2(bool flag)
  {
    client_attribute_capability_.cap_flags_.OB_CLIENT_CAP_OB_LOB_LOCATOR_V2 = (flag ? 1 : 0);
  }

  inline bool need_return_lob_locator() const
  {
    return is_client_use_lob_locator() || is_client_support_lob_locatorv2();
  }

  inline bool is_support_new_result_meta_data() const
  {
    return client_attribute_capability_.cap_flags_.OB_CLIENT_CAP_NEW_RESULT_META_DATA;
  }

  inline bool is_support_jdbc_binary_double() const
  {
    return client_attribute_capability_.cap_flags_.OB_CLIENT_SUPPORT_JDBC_BINARY_DOUBLE;
  }

  void set_proxy_cap_flags(const obmysql::ObProxyCapabilityFlags &proxy_capability)
  {
    proxy_capability_ = proxy_capability;
  }
  obmysql::ObProxyCapabilityFlags get_proxy_cap_flags() const { return proxy_capability_; }
  inline bool is_abundant_feedback_support() const
  {
    return is_track_session_info() && proxy_capability_.is_abundant_feedback_support();
  }

  //TODO::@yuming, as enable_transmission_checksum is global variables,
  //here we no need get_session for is_enable_transmission_checksum()
  inline bool is_enable_transmission_checksum() const { return true; }

  inline share::ObFeedbackManager &get_feedback_manager () { return feedback_manager_; }
  inline int set_follower_first_feedback(const share::ObFollowerFirstFeedbackType type);

  inline common::ObIAllocator &get_allocator() { return changed_var_pool_; }
  // TODO: piece cache use this allocator for now, not property, need remove later.
  inline common::ObIAllocator &get_session_allocator() { return block_allocator_; }
  inline common::ObIAllocator &get_extra_info_alloc() { return extra_info_allocator_; }

  inline common::ObIAllocator &get_cursor_allocator() { return cursor_info_allocator_; }
  inline common::ObIAllocator &get_package_allocator() { return package_info_allocator_; }

  int set_partition_hit(const bool is_hit);
  int set_proxy_user_privilege(const int64_t user_priv_set);
  int set_proxy_capability(const uint64_t proxy_cap);
  int set_client_capability();
  int set_trans_specified(const bool is_spec);
  int save_trans_status();
  // Reset transaction-related variables
  virtual void reset_tx_variable(bool reset_next_scope = true);
  transaction::ObTxIsolationLevel get_tx_isolation() const;
  void set_tx_isolation(transaction::ObTxIsolationLevel isolation);
  bool get_tx_read_only() const;
  void set_tx_read_only(const bool last_tx_read_only, const bool cur_tx_read_only);
  bool enable_mysql_compatible_dates() const { return enable_mysql_compatible_dates_; }
  void set_enable_mysql_compatible_dates(const bool enable_mysql_compatible_dates) {
    enable_mysql_compatible_dates_ = enable_mysql_compatible_dates;
  }
  bool is_diagnosis_enabled() const { return is_diagnosis_enabled_; }
  void set_diagnosis_enabled(const bool is_diagnosis_enabled) {
    is_diagnosis_enabled_ = is_diagnosis_enabled;
  }
  void set_diagnosis_limit_num(const int64_t diagnosis_limit_num) {
    diagnosis_limit_num_ = diagnosis_limit_num;
  }
  int64_t get_diagnosis_limit_num() const { return diagnosis_limit_num_; }
  int reset_tx_variable_if_remote_trans(const ObPhyPlanType& type);
  int check_tx_read_only_privilege(const ObSqlTraits &sql_traits);
  int get_group_concat_max_len(uint64_t &group_concat_max_len) const;
  int get_ob_org_cluster_id(int64_t &ob_org_cluster_id) const
  {
    ob_org_cluster_id = sys_vars_cache_.get_ob_org_cluster_id();
    return common::OB_SUCCESS;
  }
  // The parameters max_allowed_pkt and net_buffer_len are named this way instead of max_allowed_packet and net_buffer_length,
  // is to avoid naming conflicts in lib/regex/include/mysql.h so that the compilation can pass
  int get_max_allowed_packet(int64_t &max_allowed_pkt) const;
  int get_net_buffer_length(int64_t &net_buffer_len) const;
  /// @}
  int64_t get_session_info_mem_size() const { return block_allocator_.get_total_mem_size(); }
  ObPartitionHitInfo &partition_hit() { return partition_hit_; } // and above set_partition_hit has nothing to do with
  bool get_err_final_partition_hit(int err_ret)
  {
    bool is_partition_hit = partition_hit().get_bool();
    if (is_proxy_refresh_location_ret(err_ret)) {
      is_partition_hit = false;
    } else if (get_is_in_retry()
               && is_proxy_refresh_location_ret(retry_info_.get_last_query_retry_err())) {
      is_partition_hit = false;
    }
    return is_partition_hit;
  };
  bool is_proxy_refresh_location_ret(int err_ret) {
    return common::OB_NOT_MASTER == err_ret;
  }
  void set_shadow(bool is_shadow) { ATOMIC_STORE(&thread_data_.is_shadow_, is_shadow); }
  bool is_shadow() { return ATOMIC_LOAD(&thread_data_.is_shadow_);  }
  void set_mark_killed(bool is_mark_killed) { ATOMIC_STORE(&thread_data_.is_mark_killed_, is_mark_killed); }
  bool is_mark_killed() { return ATOMIC_LOAD(&thread_data_.is_mark_killed_);  }
  uint32_t get_magic_num() {return magic_num_;}
  int64_t get_current_execution_id() const { return current_execution_id_; }
  const common::ObCurTraceId::TraceId &get_last_trace_id() const { return last_trace_id_; }
  const common::ObCurTraceId::TraceId &get_current_trace_id() const { return curr_trace_id_; }
  uint64_t get_current_plan_id() const { return plan_id_; }
  void reset_current_plan_id()
  {
    plan_id_ = 0;
    GET_DIAGNOSTIC_INFO->get_ash_stat().plan_id_ = 0;
  }
  uint64_t get_current_plan_hash() const { return plan_hash_; }
  void reset_current_plan_hash()
  {
    plan_hash_ = 0;
    GET_DIAGNOSTIC_INFO->get_ash_stat().plan_hash_ = 0;
  }
  uint64_t get_last_plan_id() const { return last_plan_id_; }
  void set_last_plan_id(uint64_t plan_id) { last_plan_id_ = plan_id; }
  void set_current_execution_id(int64_t execution_id) { current_execution_id_ = execution_id; }
  void set_last_trace_id(common::ObCurTraceId::TraceId *trace_id)
  {
    if (OB_ISNULL(trace_id)) {
    } else {
      last_trace_id_ = *trace_id;
    }
  }
  void set_current_trace_id(common::ObCurTraceId::TraceId *trace_id);
  // forbid use jit
  int get_jit_enabled_mode(ObJITEnableMode &jit_mode) const
  {
    jit_mode = ObJITEnableMode::OFF;
    return common::OB_SUCCESS;
  }

  bool get_enable_exact_mode() const
  {
    return sys_vars_cache_.get_cursor_sharing_mode() == ObCursorSharingMode::EXACT_MODE;
  }

  int64_t get_runtime_filter_type() const { return sys_vars_cache_.get_runtime_filter_type(); }
  int64_t get_runtime_filter_wait_time_ms() const { return sys_vars_cache_.get_runtime_filter_wait_time_ms(); }
  int64_t get_runtime_filter_max_in_num() const { return sys_vars_cache_.get_runtime_filter_max_in_num(); }
  int64_t get_runtime_bloom_filter_max_size() const { return sys_vars_cache_.get_runtime_bloom_filter_max_size(); }

  bool get_enable_sql_plan_monitor() const { return sys_vars_cache_.get_enable_sql_plan_monitor(); }

  const ObString &get_app_trace_id() const { return app_trace_id_; }
  void set_app_trace_id(common::ObString trace_id) {
    app_trace_id_.assign_ptr(trace_id.ptr(), trace_id.length());
  }
  // update trace_id in sys variables and  will bing to client
  int update_last_trace_id(const ObCurTraceId::TraceId &trace_id);

  int set_partition_location_feedback(const share::ObFBPartitionParam &param);
  int get_auto_increment_cache_size(int64_t &auto_increment_cache_size);
  void set_curr_trans_last_stmt_end_time(int64_t t) { curr_trans_last_stmt_end_time_ = t; }
  int64_t get_curr_trans_last_stmt_end_time() const { return curr_trans_last_stmt_end_time_; }

  // for SESSION_SYNC_SYS_VAR serialize and deserialize.
  int serialize_sync_sys_vars(common::ObIArray<share::ObSysVarClassType> &sys_var_delta_ids, char *buf, const int64_t &buf_len, int64_t &pos);
  int deserialize_sync_sys_vars(int64_t &deserialize_sys_var_count, const char *buf, const int64_t &data_len, int64_t &pos, bool is_error_sync = false);
  int deserialize_sync_error_sys_vars(int64_t &deserialize_sys_var_count, const char *buf, const int64_t &data_len, int64_t &pos);
  int sync_default_sys_vars(SysVarIncInfo &tmp_sys_var_inc_info, bool &is_influence_plan_cache_sys_var);
  int get_sync_sys_vars(common::ObIArray<share::ObSysVarClassType> &sys_var_delta_ids) const;
  int get_error_sync_sys_vars(ObIArray<share::ObSysVarClassType> &sys_var_delta_ids) const;
  int get_sync_sys_vars_size(common::ObIArray<share::ObSysVarClassType> &sys_var_delta_ids, int64_t &len) const;
  bool is_sync_sys_var(share::ObSysVarClassType sys_var_id) const;
  bool is_exist_error_sync_var(share::ObSysVarClassType sys_var_id) const;
  // record session state from active to anothe state. for record total_cpu_time.
  bool is_active_state_change(ObSQLSessionState last_state, ObSQLSessionState curr_state) {
    if (last_state == QUERY_ACTIVE && curr_state != QUERY_ACTIVE) {
      return true;
    } else {
      return false;
    }
  }

  // nested session and sql execute for foreign key.
  bool is_nested_session() const { return nested_count_ > 0; }
  int64_t get_nested_count() const { return nested_count_; }
  void set_nested_count(int64_t nested_count) { nested_count_ = nested_count; }
  int save_base_session(BaseSavedValue &saved_value, bool skip_cur_stmt_tables = false);
  int restore_base_session(BaseSavedValue &saved_value, bool skip_cur_stmt_tables = false);
  int save_basic_session(StmtSavedValue &saved_value, bool skip_cur_stmt_tables = false);
  int restore_basic_session(StmtSavedValue &saved_value);
  int begin_nested_session(StmtSavedValue &saved_value, bool skip_cur_stmt_tables = false);
  int end_nested_session(StmtSavedValue &saved_value);
  int begin_autonomous_session(TransSavedValue &saved_value);
  int end_autonomous_session(TransSavedValue &saved_value);
  int merge_stmt_tables();
  int set_start_stmt();
  int set_end_stmt();
  bool has_start_stmt() { return nested_count_ >= 0; }
  bool is_fast_select() const { return false; }

  bool is_server_status_in_transaction() const;

  bool has_explicit_start_trans() const { return tx_desc_ != NULL && tx_desc_->is_explicit(); }
  bool is_in_transaction() const { return tx_desc_ != NULL && tx_desc_->is_in_tx(); }
  bool has_active_autocommit_trans(transaction::ObTransID &trans_id);
  bool is_dup_ls_modified() const { return tx_desc_ != NULL && tx_desc_->is_dup_ls_modified(); }
  bool get_in_transaction() const { return is_in_transaction(); }
  uint64_t get_trans_flags() const { return trans_flags_.get_flags(); }
  void set_has_exec_inner_dml(bool value) { trans_flags_.set_has_exec_inner_dml(value); }
  bool has_exec_inner_dml() const { return trans_flags_.has_exec_inner_dml(); }
  void set_is_in_user_scope(bool value) { sql_scope_flags_.set_is_in_user_scope(value); }
  bool is_in_user_scope() const { return sql_scope_flags_.is_in_user_scope(); }
  SqlScopeFlags &get_sql_scope_flags() { return sql_scope_flags_; }
  share::SCN get_reserved_snapshot_version() const { return reserved_read_snapshot_version_; }
  void set_reserved_snapshot_version(const share::SCN snapshot_version) { reserved_read_snapshot_version_ = snapshot_version; }
  void reset_reserved_snapshot_version() { reserved_read_snapshot_version_.reset(); }

  bool get_check_sys_variable() { return check_sys_variable_; }
  void set_check_sys_variable(bool check_sys_variable) { check_sys_variable_ = check_sys_variable; }
  bool is_acquire_from_pool() const { return acquire_from_pool_; }
  void set_acquire_from_pool(bool acquire_from_pool) { acquire_from_pool_ = acquire_from_pool; }
  bool can_release_to_pool() const { return release_to_pool_; }
  void set_release_from_pool(bool release_to_pool) { release_to_pool_ = release_to_pool; }
  bool is_tenant_killed() { return ATOMIC_LOAD(&is_tenant_killed_) > 0; }
  void set_tenant_killed() { ATOMIC_STORE(&is_tenant_killed_, 1); }
  bool is_use_inner_allocator() const;
  int64_t get_reused_count() const { return reused_count_; }
  inline void set_first_need_txn_stmt_type(stmt::StmtType stmt_type)
  {
    if (stmt::T_NONE == first_need_txn_stmt_type_) {
      first_need_txn_stmt_type_ = stmt_type;
    }
  }
  inline void reset_first_need_txn_stmt_type() { first_need_txn_stmt_type_ = stmt::T_NONE; }
  inline stmt::StmtType get_first_need_txn_stmt_type() const { return first_need_txn_stmt_type_; }
  inline void set_need_recheck_txn_readonly(bool need) { need_recheck_txn_readonly_ = need; }
  inline bool need_recheck_txn_readonly() const { return need_recheck_txn_readonly_; }
  void set_stmt_type(stmt::StmtType stmt_type)
  {
    stmt_type_ = stmt_type;
    GET_DIAGNOSTIC_INFO->get_ash_stat().stmt_type_ = stmt_type;
  }
  stmt::StmtType get_stmt_type() const { return stmt_type_; }

  bool is_password_expired() const { return is_password_expired_; }
  void set_password_expired(bool value) { is_password_expired_ = value; }
  int64_t get_process_query_time() const { return process_query_time_; }
  void set_process_query_time(int64_t time) { process_query_time_ = time; }
  inline void set_client_sessid_support(bool is_client_sessid_support)
              { is_client_sessid_support_ = is_client_sessid_support; }
  inline bool is_client_sessid_support() { return is_client_sessid_support_; }
  inline void set_feedback_proxy_info_support(const bool is_feedback_proxy_info_support) { is_feedback_proxy_info_support_ = is_feedback_proxy_info_support; }
  inline bool is_feedback_proxy_info_support() { return is_feedback_proxy_info_support_; }
  int set_enable_role_ids(const ObIArray<uint64_t>& role_ids);
  int load_default_sys_variable(common::ObIAllocator &allocator, int64_t var_idx);

  int get_session_temp_table_used(bool &is_used) const;
  int get_enable_optimizer_null_aware_antijoin(bool &is_enabled) const;
  void update_tenant_config_version(int64_t v) { cached_tenant_config_version_ = v; };
  static int check_optimizer_features_enable_valid(const ObObj &val);
  int get_compatibility_control(share::ObCompatType &compat_type) const;
  int get_compatibility_version(uint64_t &compat_version) const;
  int get_security_version(uint64_t &security_version) const;
  uint64_t get_current_default_catalog() const;
  int check_feature_enable(const share::ObCompatFeatureType feature_type, bool &is_enable) const;
  void trace_all_sys_vars() const;
  bool is_real_inner_session() const { return is_real_inner_session_; }
  void set_real_inner_session(bool value) { is_real_inner_session_ = value; }
protected:
  int process_session_variable(share::ObSysVarClassType var, const common::ObObj &value,
                               const bool check_timezone_valid = true,
                               const bool is_update_sys_var = false);
  int process_session_variable_fast();
  //@brief process session log_level setting like 'all.*:info, sql.*:debug'.
  //int process_session_ob_binlog_row_image(const common::ObObj &value);
  int process_session_log_level(const common::ObObj &val);
  int process_session_sql_mode_value(const common::ObObj &value);
  int process_session_compatibility_mode_value(const ObObj &value);
  int process_session_time_zone_value(const common::ObObj &value, const bool check_timezone_valid);
  int process_session_overlap_time_value(const ObObj &value);
  int process_session_autocommit_value(const common::ObObj &val);
  int process_session_debug_sync(const common::ObObj &val, const bool is_global,
                                const bool is_update_sys_var);
  // session switch interface
  int base_save_session(BaseSavedValue &saved_value, bool skip_cur_stmt_tables = false);
  int base_restore_session(BaseSavedValue &saved_value);
  int stmt_save_session(StmtSavedValue &saved_value, bool skip_cur_stmt_tables = false);
  int stmt_restore_session(StmtSavedValue &saved_value);
  int trans_save_session(TransSavedValue &saved_value);
  int trans_restore_session(TransSavedValue &saved_value);
protected:
  // because the OB_MALLOC_NORMAL_BLOCK_SIZE block in ObPool has a BlockHeader(8 Bytes),
  // so the total mem avail is (OB_MALLOC_NORMAL_BLOCK_SIZE - 8), if we divide it by (4 * 1204),
  // the last (4 * 1024 - 8) is wasted.
  // and if we divide it by (4 * 1024 - 8), almost all mem can be used.
  static const int64_t SMALL_BLOCK_SIZE = 4 * 1024LL - 8;
private:
  //*************** reset series functions, do not allow external calls to prevent misuse or omission
  // External unified call to reset_tx_variable()
  void reset_tx_read_only();
  void reset_tx_isolation();
  void reset_trans_flags() { trans_flags_.reset(); }
  void clear_app_trace_id() { app_trace_id_.reset(); }
  //***************

  static int change_value_for_special_sys_var(const common::ObString &sys_var_name,
                                              const common::ObObj &ori_val,
                                              common::ObObj &new_val);
  static int change_value_for_special_sys_var(const share::ObSysVarClassType sys_var_id,
                                              const common::ObObj &ori_val,
                                              common::ObObj &new_val);
  int get_int64_sys_var(const share::ObSysVarClassType sys_var_id, int64_t &int64_val) const;
  int get_uint64_sys_var(const share::ObSysVarClassType sys_var_id, uint64_t &uint64_val) const;
  int get_bool_sys_var(const share::ObSysVarClassType sys_var_id, bool &bool_val) const;
  int get_charset_sys_var(share::ObSysVarClassType sys_var_id, common::ObCharsetType &cs_type) const;
  int get_collation_sys_var(share::ObSysVarClassType sys_var_id, common::ObCollationType &coll_type) const;
  int get_string_sys_var(share::ObSysVarClassType sys_var_id, common::ObString &str) const;
  int create_sys_var(share::ObSysVarClassType sys_var_id, int64_t store_idx, share::ObBasicSysVar *&sys_var);
//  int store_sys_var(int64_t store_idx, share::ObBasicSysVar *sys_var);
  int inner_get_sys_var(const common::ObString &sys_var_name, int64_t &store_idx, share::ObBasicSysVar *&sys_var) const;
  int inner_get_sys_var(const share::ObSysVarClassType sys_var_id, int64_t &store_idx, share::ObBasicSysVar *&sys_var) const;
  int inner_get_sys_var(const common::ObString &sys_var_name, share::ObBasicSysVar *&sys_var) const
  {
    int64_t store_idx = -1;
    return inner_get_sys_var(sys_var_name, store_idx, sys_var);
  }
  int inner_get_sys_var(const share::ObSysVarClassType sys_var_id, share::ObBasicSysVar *&sys_var) const
  {
    int64_t store_idx = -1;
    return inner_get_sys_var(sys_var_id, store_idx, sys_var);
  }
  int calc_need_serialize_vars(common::ObIArray<share::ObSysVarClassType> &sys_var_ids,
                               common::ObIArray<common::ObString> &user_var_names) const;
  int deep_copy_sys_variable(share::ObBasicSysVar &sys_var,
                             const share::ObSysVarClassType sys_var_id,
                             const common::ObObj &src_val);
  int defragment_sys_variable_from(ObArray<std::pair<int64_t, ObObj>> &tmp_value);
  void defragment_sys_variable_to(ObArray<std::pair<int64_t, ObObj>> &tmp_value);
  int deep_copy_trace_id_var(const common::ObObj &src_val,
                             common::ObObj *dest_val_ptr);
  inline int store_query_string_(const ObString &stmt);
  inline int store_query_string_(const ObString &stmt, int64_t& buf_len, char *& query, volatile int64_t& query_len);
  inline int set_session_state_(ObSQLSessionState state);
  // Write the default value of system variables, deserialized scene need use base_value as baseline.
  int init_system_variables(const bool print_info_log, const bool is_sys_tenant, bool is_deserialized = false);
protected:
  //============Note: The following member variables need to consider concurrency control when used================================
  struct MultiThreadData
  {
    const static int64_t DEFAULT_MAX_PACKET_SIZE = 1048576;
    MultiThreadData () : user_name_(),
                         host_name_(),
                         client_ip_(),
                         user_at_host_name_(),
                         user_at_client_ip_(),
                         peer_addr_(),
                         client_addr_(),
                         user_client_addr_(),
                         cur_query_buf_len_(0),
                         cur_query_(nullptr),
                         cur_query_len_(0),
                         top_query_buf_len_(0),
                         top_query_(nullptr),
                         top_query_len_(0),
                         cur_statement_id_(0),
                         last_active_time_(0),
                         dis_state_(CLIENT_FORCE_DISCONNECT),
                         state_(SESSION_SLEEP),
                         is_interactive_(false),
                         sock_desc_(),
                         mysql_cmd_(obmysql::COM_SLEEP),
                         cur_query_start_time_(0),
                         cur_state_start_time_(0),
                         wait_timeout_(0),
                         interactive_timeout_(0),
                         max_packet_size_(MultiThreadData::DEFAULT_MAX_PACKET_SIZE),
                         is_shadow_(false),
                         is_in_retry_(SESS_NOT_IN_RETRY),
                         client_addr_port_(0),
                         is_mark_killed_(false),
                         retry_active_time_(0),
                         is_request_end_(true)
    {
      CHAR_CARRAY_INIT(database_name_);
    }
    void reset(bool begin_nested_session = false)
    {
      if (!begin_nested_session) {
        // TODO(jiuren): move some thing here.
      }
      user_name_.reset();
      host_name_.reset();
      client_ip_.reset();
      user_at_host_name_.reset();
      user_at_client_ip_.reset();
      CHAR_CARRAY_INIT(database_name_);
      peer_addr_.reset();
      client_addr_.reset();
      user_client_addr_.reset();
      if (cur_query_ != nullptr) {
        cur_query_[0] = '\0';
      }
      if (top_query_ != nullptr) {
        top_query_[0] = '\0';
      }
      cur_query_len_ = 0;
      top_query_len_ = 0;
      cur_statement_id_ = 0;
      last_active_time_ = 0;
      dis_state_ = CLIENT_FORCE_DISCONNECT;
      state_ = SESSION_SLEEP;
      is_interactive_ = false;
      sock_desc_.clear_sql_session_info();
      sock_desc_.reset();
      mysql_cmd_ = obmysql::COM_SLEEP;
      cur_query_start_time_ = 0;
      cur_state_start_time_ = ::oceanbase::common::ObTimeUtility::current_time();
      wait_timeout_ = 0;
      interactive_timeout_ = 0;
      max_packet_size_ = MultiThreadData::DEFAULT_MAX_PACKET_SIZE;
      is_shadow_ = false;
      is_in_retry_ = SESS_NOT_IN_RETRY;
      client_addr_port_ = 0;
      is_mark_killed_ = false;
      retry_active_time_ = 0;
      is_request_end_ = true;
    }
    ~MultiThreadData ()
    {
    }
    common::ObString user_name_;    //current user name
    common::ObString host_name_;    //current user host name
    common::ObString client_ip_;    //current user real client host name
    common::ObString user_at_host_name_;    //current user@host, for current_user()
    common::ObString user_at_client_ip_;    //current user@clientip, for user()
    char database_name_[common::OB_MAX_DATABASE_NAME_BUF_LENGTH * OB_MAX_CHAR_LEN];  //default database
    // Assume the following scenario: the user sends from machine A through proxy machine B to machine C, then again to D.
    // Then user_client_addr is the address information of machine A, keep unchanged; client_addr is the address of proxy machine B, keep unchanged.
    // peer_addr are sequentially the addresses of machines C/D, changing with increasing depth; svr_addr (not recorded here) is the final execution machine D, remaining unchanged.
    common::ObAddr peer_addr_;
    common::ObAddr client_addr_;
    common::ObAddr user_client_addr_;
    int64_t cur_query_buf_len_;
    char *cur_query_;
    volatile int64_t cur_query_len_;
    int64_t top_query_buf_len_;
    char *top_query_;
    volatile int64_t top_query_len_;
    uint64_t cur_statement_id_;
    int64_t last_active_time_;
    ObDisconnectState dis_state_;
    ObSQLSessionState state_;
    bool is_interactive_;
    rpc::ObSqlSockDesc sock_desc_;
    obmysql::ObMySQLCmd mysql_cmd_;
    int64_t cur_query_start_time_;
    int64_t cur_state_start_time_;
    int64_t wait_timeout_;
    int64_t interactive_timeout_;
    int64_t max_packet_size_;
    bool is_shadow_;
    ObSessionRetryStatus is_in_retry_;//Indicates whether the current session is in query retry status
    int32_t client_addr_port_; // Record client address port.
    bool is_mark_killed_; // Mark the current session as delayed kill
    // In the retry scenario, record the cumulative active time except the current state,
    // and use it to count the CPU time. For example, 1. The current request status is Sleep,
    // waiting for retry, it will record the cumulative time of Active during previous execution.
    // 2. The current request status is Active, and it is retrying. It will ignore the active time
    // of the current status and record the cumulative time of Active during previous execution.
    int64_t retry_active_time_;
    bool is_request_end_; // This flag is used to distinguish whether the current request is over.
  };

public:
  // For performance, system variable cache value
  class SysVarsCacheData
  {
    OB_UNIS_VERSION_V(1);
  public:
    SysVarsCacheData()
      : auto_increment_increment_(0),
        sql_throttle_current_priority_(100),
        ob_last_schema_version_(0),
        sql_select_limit_(0),
        oracle_sql_select_limit_(0),
        auto_increment_offset_(0),
        last_insert_id_(0),
        binlog_row_image_(2),
        foreign_key_checks_(0),
        default_password_lifetime_(0),
        tx_read_only_(false),
        ob_enable_pl_cache_(false),
        ob_enable_plan_cache_(false),
        optimizer_use_sql_plan_baselines_(false),
        optimizer_capture_sql_plan_baselines_(false),
        is_result_accurate_(false),
        ob_enable_transmission_checksum_(false),
        character_set_results_(ObCharsetType::CHARSET_INVALID),
        character_set_connection_(ObCharsetType::CHARSET_INVALID),
        ob_enable_jit_(ObJITEnableMode::OFF),
        cursor_sharing_mode_(ObCursorSharingMode::FORCE_MODE),
        timestamp_(0),
        tx_isolation_(transaction::ObTxIsolationLevel::INVALID),
        iso_nls_currency_(),
        ob_pl_block_timeout_(0),
        log_row_value_option_(),
        default_lob_inrow_threshold_(OB_DEFAULT_LOB_INROW_THRESHOLD),
        autocommit_(false),
        ob_enable_trace_log_(false),
        ob_enable_sql_audit_(false),
        nls_length_semantics_(LS_BYTE),
        ob_org_cluster_id_(0),
        ob_query_timeout_(0),
        ob_trx_timeout_(0),
        collation_connection_(0),
        sql_mode_(DEFAULT_OCEANBASE_MODE),
        nls_formats_{},
        ob_trx_idle_timeout_(0),
        ob_trx_lock_timeout_(-1),
        nls_collation_(CS_TYPE_INVALID),
        nls_nation_collation_(CS_TYPE_INVALID),
        ob_trace_info_(),
        ob_plsql_ccflags_(),
        ob_max_read_stale_time_(0),
        runtime_filter_type_(0),
        runtime_filter_wait_time_ms_(0),
        runtime_filter_max_in_num_(0),
        runtime_bloom_filter_max_size_(INT_MAX32),
        enable_rich_vector_format_(false),
        ncharacter_set_connection_(ObCharsetType::CHARSET_SESSION_CACHE_NOT_LOADED_MARK),
        compat_type_(share::ObCompatType::COMPAT_MYSQL57),
        compat_version_(0),
        enable_sql_plan_monitor_(false),
        ob_enable_parameter_anonymous_block_(false),
        current_default_catalog_(0),
        security_version_(0),
        ob_enable_ps_parameter_anonymous_block_(false)
    {
      for (int64_t i = 0; i < ObNLSFormatEnum::NLS_MAX; ++i) {
        MEMSET(nls_formats_buf_[i], 0, MAX_NLS_FORMAT_STR_LEN);
      }
    }
    ~SysVarsCacheData() {}

    void reset()
    {
      auto_increment_increment_ = 0;
      sql_throttle_current_priority_ = 100;
      ob_last_schema_version_ = 0;
      sql_select_limit_ = 0;
      oracle_sql_select_limit_ = 0;
      auto_increment_offset_ = 0;
      last_insert_id_ = 0;
      binlog_row_image_ = 2;
      foreign_key_checks_ = 0;
      default_password_lifetime_ = 0;
      tx_read_only_ = false;
      ob_enable_pl_cache_ = false;
      ob_enable_plan_cache_ = false;
      optimizer_use_sql_plan_baselines_ = false;
      optimizer_capture_sql_plan_baselines_ = false;
      is_result_accurate_ = false;
      ob_enable_transmission_checksum_ = false;
      character_set_results_ = ObCharsetType::CHARSET_INVALID;
      character_set_connection_ = ObCharsetType::CHARSET_INVALID;
      ob_enable_jit_ = ObJITEnableMode::OFF;
      cursor_sharing_mode_ = ObCursorSharingMode::FORCE_MODE;
      timestamp_ = 0;
      tx_isolation_ = transaction::ObTxIsolationLevel::INVALID;
      ob_pl_block_timeout_ = 0;
      ob_plsql_ccflags_.reset();
      autocommit_ = false;
      ob_enable_trace_log_ = false;
      ob_org_cluster_id_ = 0;
      ob_query_timeout_ = 0;
      ob_trx_timeout_ = 0;
      collation_connection_ = 0;
      ob_enable_sql_audit_ = false;
      nls_length_semantics_ = LS_BYTE;
      sql_mode_ = DEFAULT_OCEANBASE_MODE;
      for (int64_t i = 0; i < ObNLSFormatEnum::NLS_MAX; ++i) {
        nls_formats_[i].reset();
        MEMSET(nls_formats_buf_[i], 0, MAX_NLS_FORMAT_STR_LEN);
      }
      ob_trx_idle_timeout_ = 0;
      ob_trx_lock_timeout_ = -1;
      nls_collation_ = CS_TYPE_INVALID;
      nls_nation_collation_ = CS_TYPE_INVALID;
      ob_trace_info_.reset();
      iso_nls_currency_.reset();
      ob_plsql_ccflags_.reset();
      log_row_value_option_.reset();
      ob_max_read_stale_time_ = 0;
      runtime_filter_type_ = 0;
      runtime_filter_wait_time_ms_ = 0;
      runtime_filter_max_in_num_ = 0;
      runtime_bloom_filter_max_size_ = INT32_MAX;
      enable_rich_vector_format_ = false;
      ncharacter_set_connection_ = ObCharsetType::CHARSET_INVALID;
      default_lob_inrow_threshold_ = OB_DEFAULT_LOB_INROW_THRESHOLD;
      compat_type_ = share::ObCompatType::COMPAT_MYSQL57;
      compat_version_ = 0;
      enable_sql_plan_monitor_ = false;
      ob_enable_parameter_anonymous_block_ = false;
      security_version_ = 0;
      ob_enable_ps_parameter_anonymous_block_ = false;
      current_default_catalog_ = 0;
    }

    inline bool operator==(const SysVarsCacheData &other) const {
      bool equal1 =  auto_increment_increment_ == other.auto_increment_increment_ &&
            sql_throttle_current_priority_ == other.sql_throttle_current_priority_ &&
            ob_last_schema_version_ == other.ob_last_schema_version_ &&
            sql_select_limit_ == other.sql_select_limit_ &&
            oracle_sql_select_limit_ == other.oracle_sql_select_limit_ &&
            auto_increment_offset_ == other.auto_increment_offset_ &&
            last_insert_id_ == other.last_insert_id_ &&
            binlog_row_image_ == other.binlog_row_image_ &&
            foreign_key_checks_ == other.foreign_key_checks_ &&
            default_password_lifetime_ == other.default_password_lifetime_ &&
            tx_read_only_ == other.tx_read_only_ &&
            ob_enable_pl_cache_ == other.ob_enable_pl_cache_ &&
            ob_enable_plan_cache_ == other.ob_enable_plan_cache_ &&
            optimizer_use_sql_plan_baselines_ == other.optimizer_use_sql_plan_baselines_ &&
            optimizer_capture_sql_plan_baselines_ == other.optimizer_capture_sql_plan_baselines_ &&
            is_result_accurate_ == other.is_result_accurate_ &&
            ob_enable_transmission_checksum_ == other.ob_enable_transmission_checksum_ &&
            character_set_results_ == other.character_set_results_ &&
            character_set_connection_ == other.character_set_connection_ &&
            ob_enable_jit_ == other.ob_enable_jit_ &&
            cursor_sharing_mode_ == other.cursor_sharing_mode_ &&
            timestamp_ == other.timestamp_ &&
            tx_isolation_ == other.tx_isolation_ &&
            ob_pl_block_timeout_ == other.ob_pl_block_timeout_ &&
            ob_plsql_ccflags_ == other.ob_plsql_ccflags_ &&
            autocommit_ == other.autocommit_ &&
            ob_org_cluster_id_ == other.ob_org_cluster_id_ &&
            ob_query_timeout_ == other.ob_query_timeout_ &&
            ob_trx_timeout_ == other.ob_trx_timeout_ &&
            collation_connection_ == other.collation_connection_ &&
            ob_enable_sql_audit_ == other.ob_enable_sql_audit_ &&
            nls_length_semantics_ == other.nls_length_semantics_ &&
            sql_mode_ == other.sql_mode_ &&
            ob_trx_idle_timeout_ == other.ob_trx_idle_timeout_ &&
            ob_trx_lock_timeout_ == other.ob_trx_lock_timeout_ &&
            nls_collation_ == other.nls_collation_ &&
            nls_nation_collation_ == other.nls_nation_collation_ &&
            ob_trace_info_ == other.ob_trace_info_ &&
            iso_nls_currency_ == other.iso_nls_currency_ &&
            ob_plsql_ccflags_ == other.ob_plsql_ccflags_ &&
            log_row_value_option_ == other.log_row_value_option_ &&
            ob_max_read_stale_time_ == other.ob_max_read_stale_time_ &&
            ob_max_read_stale_time_ == other.ob_max_read_stale_time_  &&
            enable_rich_vector_format_ == other.enable_rich_vector_format_ &&
            ncharacter_set_connection_ == other.ncharacter_set_connection_ &&
            default_lob_inrow_threshold_ == other.default_lob_inrow_threshold_ &&
            compat_type_ == other.compat_type_ &&
            compat_version_ == other.compat_version_ &&
            ob_enable_parameter_anonymous_block_ == other.ob_enable_parameter_anonymous_block_ &&
            security_version_ == other.security_version_ &&
            ob_enable_ps_parameter_anonymous_block_ == other.ob_enable_ps_parameter_anonymous_block_ &&
            current_default_catalog_ == other.current_default_catalog_;
      bool equal2 = true;
      for (int64_t i = 0; i < ObNLSFormatEnum::NLS_MAX; ++i) {
        if (nls_formats_[i] != other.nls_formats_[i]) {
          equal2 = false;
        }
      }
      return equal1 && equal2;
    }
    void set_nls_date_format(const common::ObString &format)
    {
      set_nls_format(NLS_DATE, format);
    }
    void set_nls_timestamp_format(const common::ObString &format)
    {
      set_nls_format(NLS_TIMESTAMP, format);
    }
    void set_nls_timestamp_tz_format(const common::ObString &format)
    {
      set_nls_format(NLS_TIMESTAMP_TZ, format);
    }
    void set_nls_format(const int64_t enum_value, const common::ObString &format)
    {
      if (0 <= enum_value && enum_value < ObNLSFormatEnum::NLS_MAX) {
        if (format.empty()) {
          nls_formats_[enum_value].reset();
          MEMSET(nls_formats_buf_[enum_value], 0, MAX_NLS_FORMAT_STR_LEN);
        } else {
          MEMCPY(nls_formats_buf_[enum_value], format.ptr(), format.length());
          nls_formats_[enum_value].assign_ptr(nls_formats_buf_[enum_value], format.length());
        }
      }
    }
    void set_iso_nls_currency(const common::ObString &format)
    {
      if (format.empty()) {
        iso_nls_currency_.reset();
      } else {
        MEMCPY(iso_nls_currency_buf_, format.ptr(), format.length());
        iso_nls_currency_.assign_ptr(iso_nls_currency_buf_, format.length());
      }
    }
    const common::ObString &get_nls_date_format() const
    {
      return nls_formats_[NLS_DATE];
    }
    const common::ObString &get_nls_timestamp_format() const
    {
      return nls_formats_[NLS_TIMESTAMP];
    }
    const common::ObString &get_nls_timestamp_tz_format() const
    {
      return nls_formats_[NLS_TIMESTAMP_TZ];
    }
    void set_ob_trace_info(const common::ObString &trace_info)
    {
      if (trace_info.empty()) {
        ob_trace_info_.reset();
      } else {
        const int32_t trace_len = std::min(trace_info.length(), OB_TRACE_BUFFER_SIZE);
        MEMCPY(trace_info_buf_, trace_info.ptr(), trace_len);
        ob_trace_info_.assign_ptr(trace_info_buf_, trace_len);
      }
    }
    const common::ObString &get_ob_trace_info() const
    {
      return ob_trace_info_;
    }
    const common::ObString &get_iso_nls_currency() const
    {
      return iso_nls_currency_;
    }
    void set_plsql_ccflags(const common::ObString &plsql_ccflags)
    {
      if (plsql_ccflags.empty()) {
        ob_plsql_ccflags_.reset();
      } else {
        const int32_t ccflags_len
          = std::min(plsql_ccflags.length(), OB_TMP_BUF_SIZE_256);
        MEMCPY(plsql_ccflags_, plsql_ccflags.ptr(), ccflags_len);
        ob_plsql_ccflags_.assign_ptr(plsql_ccflags_, ccflags_len);
      }
    }
    const common::ObString &get_plsql_ccflags() const
    {
      return ob_plsql_ccflags_;
    }
    void set_log_row_value_option(const common::ObString &option)
    {
      if (option.empty()) {
        log_row_value_option_.reset();
      } else {
        MEMCPY(log_row_value_option_buf_, option.ptr(), option.length());
        log_row_value_option_.assign_ptr(log_row_value_option_buf_, option.length());
      }
    }
    void set_default_lob_inrow_threshold(const int64_t default_lob_inrow_threshold)
    {
      default_lob_inrow_threshold_ = default_lob_inrow_threshold;
    }
    const common::ObString &get_log_row_value_option() const
    {
      return log_row_value_option_;
    }
    int64_t get_default_lob_inrow_threshold() const
    {
      return default_lob_inrow_threshold_;
    }

    TO_STRING_KV(K(autocommit_), K(ob_enable_trace_log_), K(ob_enable_sql_audit_), K(nls_length_semantics_),
                 K(ob_org_cluster_id_), K(ob_query_timeout_), K(ob_trx_timeout_), K(collation_connection_),
                 K(sql_mode_), K(nls_formats_[0]), K(nls_formats_[1]), K(nls_formats_[2]),
                 K(ob_trx_idle_timeout_), K(ob_trx_lock_timeout_), K(nls_collation_), K(nls_nation_collation_),
                 K_(sql_throttle_current_priority), K_(ob_last_schema_version), K_(sql_select_limit), K_(oracle_sql_select_limit),
                 K_(optimizer_use_sql_plan_baselines), K_(optimizer_capture_sql_plan_baselines),
                 K_(is_result_accurate), K_(character_set_results),
                 K_(character_set_connection), K_(ob_pl_block_timeout), K_(ob_plsql_ccflags),
                 K_(iso_nls_currency), K_(log_row_value_option), K_(ob_max_read_stale_time), K_(default_lob_inrow_threshold));
  public:
    static const int64_t MAX_NLS_FORMAT_STR_LEN = 256;
    //==========  No need to serialize  ============
    uint64_t auto_increment_increment_;
    int64_t sql_throttle_current_priority_;
    int64_t ob_last_schema_version_;
    int64_t sql_select_limit_;
    int64_t oracle_sql_select_limit_;
    uint64_t auto_increment_offset_;
    uint64_t last_insert_id_;
    int64_t binlog_row_image_;
    int64_t foreign_key_checks_;
    uint64_t default_password_lifetime_;
    bool tx_read_only_;
    bool ob_enable_pl_cache_;
    bool ob_enable_plan_cache_;
    bool optimizer_use_sql_plan_baselines_;
    bool optimizer_capture_sql_plan_baselines_;
    bool is_result_accurate_;
    bool ob_enable_transmission_checksum_;
    ObCharsetType character_set_results_;
    ObCharsetType character_set_connection_;
    ObJITEnableMode ob_enable_jit_;
    ObCursorSharingMode cursor_sharing_mode_;

    int64_t timestamp_;
    transaction::ObTxIsolationLevel tx_isolation_;

    common::ObString iso_nls_currency_;
    char iso_nls_currency_buf_[MAX_NLS_FORMAT_STR_LEN];
    int64_t ob_pl_block_timeout_;

    common::ObString log_row_value_option_;
    char log_row_value_option_buf_[OB_TMP_BUF_SIZE_256];
    int64_t default_lob_inrow_threshold_;
    //==========  need serialization  ============
    bool autocommit_;
    bool ob_enable_trace_log_;
    bool ob_enable_sql_audit_;
    ObLengthSemantics nls_length_semantics_;
    int64_t ob_org_cluster_id_;
    int64_t ob_query_timeout_;
    int64_t ob_trx_timeout_;
    int64_t collation_connection_;
    ObSQLMode sql_mode_;
    common::ObString nls_formats_[ObNLSFormatEnum::NLS_MAX];
    int64_t ob_trx_idle_timeout_;
    int64_t ob_trx_lock_timeout_;
    ObCollationType nls_collation_; //for oracle char and varchar2
    ObCollationType nls_nation_collation_; //for oracle nchar and nvarchar2
    ObString ob_trace_info_; // identifier from user app, pass through system including app & db
    char trace_info_buf_[OB_TRACE_BUFFER_SIZE];
    ObString ob_plsql_ccflags_;
    char plsql_ccflags_[OB_TMP_BUF_SIZE_256];
    int64_t ob_max_read_stale_time_;
    int64_t runtime_filter_type_;
    int64_t runtime_filter_wait_time_ms_;
    int64_t runtime_filter_max_in_num_;
    int64_t runtime_bloom_filter_max_size_;
    bool enable_rich_vector_format_;
    ObCharsetType ncharacter_set_connection_;
    share::ObCompatType compat_type_;
    uint64_t compat_version_;
    // No use. Placeholder.
    bool enable_sql_plan_monitor_;
    bool ob_enable_parameter_anonymous_block_;
    uint64_t current_default_catalog_;
    uint64_t security_version_;
    bool ob_enable_ps_parameter_anonymous_block_;
  private:
    char nls_formats_buf_[ObNLSFormatEnum::NLS_MAX][MAX_NLS_FORMAT_STR_LEN];
  };
private:
#define DEF_SYS_VAR_CACHE_FUNCS(SYS_VAR_TYPE, SYS_VAR_NAME)                           \
  void set_##SYS_VAR_NAME(SYS_VAR_TYPE value)                                         \
  {                                                                                   \
    inc_data_.SYS_VAR_NAME##_ = (value);                                              \
    inc_##SYS_VAR_NAME##_ = true;                                                     \
  }                                                                                   \
  void set_base_##SYS_VAR_NAME(SYS_VAR_TYPE value)                                    \
  {                                                                                   \
    base_data_.SYS_VAR_NAME##_ = (value);                                             \
  }                                                                                   \
  const SYS_VAR_TYPE &get_##SYS_VAR_NAME() const                                      \
  {                                                                                   \
    return get_##SYS_VAR_NAME(inc_##SYS_VAR_NAME##_);                                 \
  }                                                                                   \
  const SYS_VAR_TYPE &get_##SYS_VAR_NAME(bool is_inc) const                           \
  {                                                                                   \
    return is_inc ? inc_data_.SYS_VAR_NAME##_ : base_data_.SYS_VAR_NAME##_;           \
  }

#define DEF_SYS_VAR_CACHE_FUNCS_STR(SYS_VAR_NAME)                                     \
  void set_base_##SYS_VAR_NAME(const common::ObString &value)            \
  {                                                                                   \
    base_data_.set_##SYS_VAR_NAME(value);                                             \
  }                                                                                   \
  void set_##SYS_VAR_NAME(const common::ObString &value)                 \
  {                                                                                   \
      inc_data_.set_##SYS_VAR_NAME(value);                                            \
      inc_##SYS_VAR_NAME##_ = true;                                                   \
  }                                                                                   \
  const common::ObString &get_##SYS_VAR_NAME() const                                  \
  {                                                                                   \
    return get_##SYS_VAR_NAME(inc_##SYS_VAR_NAME##_);                                 \
  }                                                                                   \
  const common::ObString &get_##SYS_VAR_NAME(bool is_inc) const                       \
  {                                                                                   \
    return is_inc ? inc_data_.get_##SYS_VAR_NAME() : base_data_.get_##SYS_VAR_NAME(); \
  }

  class SysVarsCache
  {
  public:
    SysVarsCache()
      : inc_flags_(0)
    {}
    ~SysVarsCache()
    {}
  public:
    void reset()
    {
      inc_data_.reset();
      inc_flags_ = 0;
    }
    void clean_inc()
    {
      inc_flags_ = 0;
    }
    bool is_inc_empty() const
    {
      return inc_flags_ == 0;
    }
    DEF_SYS_VAR_CACHE_FUNCS(uint64_t, auto_increment_increment);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, sql_throttle_current_priority);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, ob_last_schema_version);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, sql_select_limit);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, oracle_sql_select_limit);
    DEF_SYS_VAR_CACHE_FUNCS(uint64_t, auto_increment_offset);
    DEF_SYS_VAR_CACHE_FUNCS(uint64_t, last_insert_id);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, binlog_row_image);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, foreign_key_checks);
    DEF_SYS_VAR_CACHE_FUNCS(uint64_t, default_password_lifetime);
    DEF_SYS_VAR_CACHE_FUNCS(bool, tx_read_only);
    DEF_SYS_VAR_CACHE_FUNCS(bool, ob_enable_pl_cache);
    DEF_SYS_VAR_CACHE_FUNCS(bool, ob_enable_plan_cache);
    DEF_SYS_VAR_CACHE_FUNCS(bool, optimizer_use_sql_plan_baselines);
    DEF_SYS_VAR_CACHE_FUNCS(bool, optimizer_capture_sql_plan_baselines);
    DEF_SYS_VAR_CACHE_FUNCS(bool, is_result_accurate);
    DEF_SYS_VAR_CACHE_FUNCS(bool, ob_enable_transmission_checksum);
    DEF_SYS_VAR_CACHE_FUNCS(ObCharsetType, character_set_results);
    DEF_SYS_VAR_CACHE_FUNCS(ObCharsetType, character_set_connection);
    DEF_SYS_VAR_CACHE_FUNCS(ObJITEnableMode, ob_enable_jit);
    DEF_SYS_VAR_CACHE_FUNCS(ObCursorSharingMode, cursor_sharing_mode);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, timestamp);
    DEF_SYS_VAR_CACHE_FUNCS(transaction::ObTxIsolationLevel, tx_isolation);
    DEF_SYS_VAR_CACHE_FUNCS(bool, autocommit);
    DEF_SYS_VAR_CACHE_FUNCS(bool, ob_enable_trace_log);
    DEF_SYS_VAR_CACHE_FUNCS(bool, ob_enable_sql_audit);
    DEF_SYS_VAR_CACHE_FUNCS(ObLengthSemantics, nls_length_semantics);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, ob_org_cluster_id);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, ob_query_timeout);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, ob_trx_timeout);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, collation_connection);
    DEF_SYS_VAR_CACHE_FUNCS(ObSQLMode, sql_mode);
    DEF_SYS_VAR_CACHE_FUNCS_STR(nls_date_format);
    DEF_SYS_VAR_CACHE_FUNCS_STR(nls_timestamp_format);
    DEF_SYS_VAR_CACHE_FUNCS_STR(nls_timestamp_tz_format);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, ob_trx_idle_timeout);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, ob_trx_lock_timeout);
    DEF_SYS_VAR_CACHE_FUNCS(ObCollationType, nls_collation);
    DEF_SYS_VAR_CACHE_FUNCS(ObCollationType, nls_nation_collation);
    DEF_SYS_VAR_CACHE_FUNCS_STR(ob_trace_info);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, ob_pl_block_timeout);
    DEF_SYS_VAR_CACHE_FUNCS_STR(plsql_ccflags);
    DEF_SYS_VAR_CACHE_FUNCS_STR(iso_nls_currency);
    DEF_SYS_VAR_CACHE_FUNCS_STR(log_row_value_option);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, ob_max_read_stale_time);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, runtime_filter_type);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, runtime_filter_wait_time_ms);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, runtime_filter_max_in_num);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, runtime_bloom_filter_max_size);
    DEF_SYS_VAR_CACHE_FUNCS(bool, enable_rich_vector_format);
    DEF_SYS_VAR_CACHE_FUNCS(ObCharsetType, ncharacter_set_connection);
    DEF_SYS_VAR_CACHE_FUNCS(int64_t, default_lob_inrow_threshold);
    DEF_SYS_VAR_CACHE_FUNCS(share::ObCompatType, compat_type);
    DEF_SYS_VAR_CACHE_FUNCS(uint64_t, compat_version);
    DEF_SYS_VAR_CACHE_FUNCS(bool, enable_sql_plan_monitor);
    DEF_SYS_VAR_CACHE_FUNCS(bool, ob_enable_parameter_anonymous_block);
    DEF_SYS_VAR_CACHE_FUNCS(uint64_t, security_version);
    DEF_SYS_VAR_CACHE_FUNCS(bool, ob_enable_ps_parameter_anonymous_block);
    DEF_SYS_VAR_CACHE_FUNCS(uint64_t, current_default_catalog);
    void set_autocommit_info(bool inc_value)
    {
      inc_data_.autocommit_ = inc_value;
      inc_autocommit_ = true;
    }
    void get_autocommit_info(bool &inc_value)
    {
      if (inc_autocommit_) {
        inc_value = inc_data_.autocommit_;
      } else {
        inc_value = base_data_.autocommit_;
      }
    }
  public:
    // base_data is the hardcode variable value in ObSysVariables
    static SysVarsCacheData base_data_;
    SysVarsCacheData inc_data_;
    union { // FARM COMPAT WHITELIST
      uint64_t inc_flags_;
      struct {
        bool inc_auto_increment_increment_:1;
        bool inc_sql_throttle_current_priority_:1;
        bool inc_ob_last_schema_version_:1;
        bool inc_sql_select_limit_:1;
        bool inc_oracle_sql_select_limit_:1;
        bool inc_auto_increment_offset_:1;
        bool inc_last_insert_id_:1;
        bool inc_binlog_row_image_:1;
        bool inc_foreign_key_checks_:1;
        bool inc_default_password_lifetime_:1;
        bool inc_tx_read_only_:1;
        bool inc_ob_enable_plan_cache_:1;
        bool inc_optimizer_use_sql_plan_baselines_:1;
        bool inc_optimizer_capture_sql_plan_baselines_:1;
        bool inc_is_result_accurate_:1;
        bool inc_ob_enable_transmission_checksum_:1;
        bool inc_character_set_results_:1;
        bool inc_character_set_connection_:1;
        bool inc_ob_enable_jit_:1;
        bool inc_cursor_sharing_mode_:1;
        bool inc_timestamp_:1;
        bool inc_tx_isolation_:1;
        bool inc_autocommit_:1;
        bool inc_ob_enable_trace_log_:1;
        bool inc_ob_enable_sql_audit_;
        bool inc_nls_length_semantics_:1;
        bool inc_ob_org_cluster_id_:1;
        bool inc_ob_query_timeout_:1;
        bool inc_ob_trx_timeout_:1;
        bool inc_collation_connection_:1;
        bool inc_sql_mode_:1;
        bool inc_nls_date_format_:1;
        bool inc_nls_timestamp_format_:1;
        bool inc_nls_timestamp_tz_format_:1;
        bool inc_ob_trx_idle_timeout_:1;
        bool inc_ob_trx_lock_timeout_:1;
        bool inc_nls_collation_:1;
        bool inc_nls_nation_collation_:1;
        bool inc_ob_trace_info_:1;
        bool inc_ob_pl_block_timeout_:1;
        bool inc_plsql_ccflags_:1;
        bool inc_iso_nls_currency_:1;
        bool inc_log_row_value_option_:1;
        bool inc_ob_max_read_stale_time_:1;
        bool inc_runtime_filter_type_:1;
        bool inc_runtime_filter_wait_time_ms_:1;
        bool inc_runtime_filter_max_in_num_:1;
        bool inc_runtime_bloom_filter_max_size_:1;
        bool inc_enable_rich_vector_format_:1; 
        bool inc_ncharacter_set_connection_:1; 
        bool inc_default_lob_inrow_threshold_:1;
        bool inc_ob_enable_pl_cache_:1;
        bool inc_compat_type_:1;
        bool inc_compat_version_:1;
        bool inc_enable_sql_plan_monitor_:1;
        bool inc_ob_enable_parameter_anonymous_block_:1;
        bool inc_security_version_:1;
        bool inc_ob_enable_ps_parameter_anonymous_block_:1;
        bool inc_current_default_catalog_:1;
      };
    };
  };
protected:
  const uint64_t orig_tenant_id_;     // which tenant new me
private:
  static const int64_t CACHED_SYS_VAR_VERSION = 721;// a magic num
  static const int MAX_SESS_BT_BUFF_SIZE = 1024;

  ObTenantSQLSessionMgr *tenant_session_mgr_;
  // data structure related:
  common::ObRecursiveMutex query_mutex_;//mutex multiple query requests on the same session
  common::ObRecursiveMutex thread_data_mutex_;//mutex multiple threads for concurrent read and write to the same session member, protecting the consistency of thread_data_
  bool is_valid_;  // is valid session entry
  bool is_deserialized_; // whether the session is obtained through deserialization, currently only used for data cleanup when releasing temporary table sessions
  // session properties:
  char tenant_[common::OB_MAX_TENANT_NAME_LENGTH + 1];         // current tenant
  uint64_t tenant_id_;            // current tenant ID, used for privilege check and resource audit
  char effective_tenant_[common::OB_MAX_TENANT_NAME_LENGTH + 1];
  uint64_t effective_tenant_id_;            // current effective tenant ID, used for schema check
  uint64_t rpc_tenant_id_;
  bool is_changed_to_temp_tenant_;              // if tenant is changed to temp tenant for show statement
  uint64_t user_id_;              // current user id
  common::ObString client_version_;  // current client version
  common::ObString driver_version_;  // current driver version
  uint32_t sessid_;
  uint32_t master_sessid_;
  uint32_t client_sessid_;
  uint64_t client_create_time_;
  uint64_t proxy_sessid_;
  int64_t global_vars_version_; // used for obproxy synchronize variables
  int64_t sys_var_base_version_;
  /*******************************************
   * transaction ctrl relative for session
   *******************************************/
protected:
  transaction::ObTxDesc *tx_desc_;
  transaction::ObTxExecResult tx_result_; // TODO: move to QueryCtx/ExecCtx
  // reserved read snapshot version for current or previous stmt in the txn. And
  // it is used for multi-version garbage colloector to collect ative snapshot.
  // While it may be empty for the txn with ac = 1 and remote execution whose
  // snapshot version is generated from remote server(called by start_stmt). So
  // use it only query is active and version is valid.
  share::SCN reserved_read_snapshot_version_;
  transaction::ObXATransID xid_;
  bool associated_xa_; // session joined distr-xa-trans by xa-start
  int64_t cached_tenant_config_version_;
public:
  const transaction::ObXATransID &get_xid() const { return xid_; }
  transaction::ObTransID get_tx_id() const { return tx_desc_ != NULL ? tx_desc_->get_tx_id() : transaction::ObTransID(); }
  transaction::ObTxDesc /*Nullable*/ *&get_tx_desc() { return tx_desc_; }
  const transaction::ObTxDesc /*Nullable*/ *get_tx_desc() const { return tx_desc_; }
  transaction::ObTxExecResult &get_trans_result() { return tx_result_; }
  bool associated_xa() const { return associated_xa_; }
  int associate_xa(const transaction::ObXATransID &xid) { associated_xa_ = true; return xid_.set(xid); }
  void disassociate_xa() { associated_xa_ = false; xid_.reset(); }
private:
  common::ObSEArray<TableStmtType, 2> total_stmt_tables_;
  common::ObSEArray<TableStmtType, 1> cur_stmt_tables_;
  char ssl_cipher_buff_[64];
  // following 3 params are used to diagnosis session leak.
  char sess_bt_buff_[MAX_SESS_BT_BUFF_SIZE];
  int sess_bt_buff_pos_;
  int32_t sess_ref_cnt_;
  int32_t sess_ref_seq_;

protected:
  // alloc at most SMALL_BLOCK_SIZE bytes for each alloc() call.
  // free() call returns memory back to block pool
  common::ObSmallBlockAllocator<> block_allocator_;
  common::ObSmallBlockAllocator<> ps_session_info_allocator_;
  common::ObSmallBlockAllocator<> cursor_info_allocator_; // for alloc memory of PS CURSOR/SERVER REF CURSOR
  common::ObSmallBlockAllocator<> package_info_allocator_; // for alloc memory of session package state
  common::ObStringBuf sess_level_name_pool_; // will reset when disconnect session
  common::ObStringBuf conn_level_name_pool_; // will reset when reset connection and disconnect session
  intptr_t json_pl_mngr_; // for pl json manage
  TransFlags trans_flags_;
  SqlScopeFlags sql_scope_flags_;
  bool need_reset_package_; // for dbms_session.reset_package

private:
  common::ObStringBuf base_sys_var_alloc_; // for variables names and statement names
  // Double buffer optimization
  common::ObStringBuf *inc_sys_var_alloc_[2]; // for values in sys variables update
  common::ObStringBuf inc_sys_var_alloc1_; // for values in sys variables update
  common::ObStringBuf inc_sys_var_alloc2_; // for values in sys variables update
  int32_t current_buf_index_; // for record current buf index
  // Double buffer optimization end.
  common::ObWrapperAllocator bucket_allocator_wrapper_;
  ObSessionValMap user_var_val_map_; // user variables
  share::ObBasicSysVar *sys_vars_[share::ObSysVarFactory::ALL_SYS_VARS_COUNT]; // system variables
  common::ObSEArray<int64_t, 32> influence_plan_var_indexs_;
  common::ObString sys_var_in_pc_str_;
  // configurations that will influence execution plan
  common::ObString config_in_pc_str_;
  bool is_first_gen_; // is first generate sys_var_in_pc_str_;
  bool is_first_gen_config_; // whether is first time t o generate config_in_pc_str_
  share::ObSysVarFactory sys_var_fac_;
  char trace_id_buff_[64];//Since the trace_id system variable is updated in the case of a slow query, a buffer is used to store its content, preventing frequent memory allocation
  int64_t next_frag_mem_point_; // Used to control memory fragmentation of sys var (repeatedly setting the same varchar value can cause memory fragmentation)
  int64_t sys_vars_encode_max_size_;
  //==============System variables related variables, need to be serialized to remote==============
  common::ObConsistencyLevel consistency_level_;
  ObTimeZoneInfoWrap tz_info_wrap_;
  int64_t next_tx_read_only_;
  transaction::ObTxIsolationLevel next_tx_isolation_;
  bool enable_mysql_compatible_dates_;
  bool is_diagnosis_enabled_;
  int64_t diagnosis_limit_num_;
  //===============================================================
  //==============System variables related variables, do not serialize to remote==============
  bool log_id_level_map_valid_;
  common::ObLogIdLevelMap log_id_level_map_;
  //===============================================================
  // Lifecycle not guaranteed, use this pointer with caution
  const ObPhysicalPlan *cur_phy_plan_;
  // sql_id of cur_phy_plan_ sql
  char sql_id_[common::OB_MAX_SQL_ID_LENGTH + 1];
  uint64_t plan_id_; // for ASH sampling, get current SQL's sql_id & plan_id
  uint64_t last_plan_id_;
  uint64_t plan_hash_;

  ObFLTVars flt_vars_;
  //=======================ObProxy && OCJ related============================
  obmysql::ObMySQLCapabilityFlags capability_;
  obmysql::ObProxyCapabilityFlags proxy_capability_;
  obmysql::ObClientAttributeCapabilityFlags client_attribute_capability_;
  common::ObClientMode client_mode_; // client mode, java client , obproxy or etc.
  // add by oushen, track changed session info
  common::ObSEArray<ChangedVar, 8> changed_sys_vars_;
  common::ObSEArray<common::ObString, 16> changed_user_vars_;
  common::ObArenaAllocator changed_var_pool_;  // reuse for each statement
  common::ObReserveArenaAllocator<256> extra_info_allocator_; // use for extra_info in 20 protocol
  bool is_database_changed_;  // is schema changed
  share::ObFeedbackManager feedback_manager_; // feedback T-L-V
  // add by gujian, cached the flag whether transaction is sepcified
  ObTransSpecfiedStatus trans_spec_status_;
  //========================================================================

  // debug sync actions stored in session
  common::ObDSSessionActions debug_sync_actions_;
  ObPartitionHitInfo partition_hit_;
  uint32_t magic_num_;
  int64_t current_execution_id_;
  common::ObCurTraceId::TraceId last_trace_id_;
  common::ObCurTraceId::TraceId curr_trace_id_;
  common::ObString app_trace_id_;
  uint64_t database_id_;
  ObQueryRetryInfo retry_info_;
  // The trace_id of the previous query packet, used to determine if it is a retry query packet. Here we only care about the query, not other types of packets (e.g., init db).
  common::ObCurTraceId::TraceId last_query_trace_id_;
protected:
  //this should be used by subclass, so need be protected
  MultiThreadData thread_data_;
  // nested session and sql execute for foreign key.
  int64_t nested_count_; // initialized to -1; set to 0 when a stmt is executing; incremented when nesting occurs
  // Configurations that will influence execution plan.
  ObConfigInfoInPC inf_pc_configs_;
  common::ObString client_identifier_;
  // For performance, system variable local cache value
public:
  inline const SysVarsCacheData &get_sys_var_cache_inc_data() const {
    return sys_vars_cache_.inc_data_;
  }
  inline SysVarsCacheData &get_sys_var_cache_inc_data() {
    return sys_vars_cache_.inc_data_;
  }
private:
  SysVarsCache sys_vars_cache_;
  static int fill_sys_vars_cache_base_value(
      share::ObSysVarClassType var,
      SysVarsCache &sys_vars_cache,
      const common::ObObj &val);
private:
  // @Xiu Ming 2.2 version before used to identify the SHOW statement for read-only zone scenarios, its value due to implementation defects, is always false
  // After version 2.2, the read-only zone is removed, and the physical standby database no longer depends on this value, thus, this value is no longer used
  // bool literal_query_;
  // The end time of the previous statement
  int64_t curr_trans_last_stmt_end_time_;

  bool check_sys_variable_;
  bool acquire_from_pool_;
  // In the constructor it is initialized to true, and set to false in some specific error cases, indicating that the session cannot be released back to the session pool.
  // So reset interface does not need to, and cannot reset release_to_pool_.
  bool release_to_pool_;
  volatile int64_t is_tenant_killed_;  // use int64_t for ATOMIC_LOAD / ATOMIC_STORE.
  int64_t reused_count_;
  // type of first stmt which need transaction
  // either transactional read or transactional write
  stmt::StmtType first_need_txn_stmt_type_;
  // some Cmd like DDL will commit current transaction, and need recheck tx read only settings before run
  bool need_recheck_txn_readonly_;
  //min_cluster_version_: record the minimum server version number of the cluster before sql execution
  // Solve the problem of compatibility:
  //   versions before 2.2.3 would serialize all system variables that need serialization,
  //   2.2.3 and later versions do not serialize ORACLE_ONLY system variables,
  //   The following scenario will report an error (issue 1):
  //     During the period when 222 and 223 are running together, the client executed a px query and reported -4016 when serializing a system variable in the session,
  //     The server where the main thread resides is 223, and the server where the sqc end resides is 222. This query's entire lifecycle will involve two sessions of serialization and deserialization.
  //      The first time is init_sqc rpc(executed on 223, only serializing non-ORACLE_ONLY variables)
  //      The second time is on 222, sqc starting worker will copy all exec_ctx to other workers, and the copying method is serialization + deserialization.
  //      During the first serialization of init_sqc rpc, if it is a mysql tenant, only the 《System Variables to be Serialized》 will be serialized as well as
  //      <The system variables that are NOT ORACLE_ONLY>, therefore the observer of 222 has only these variables after deserialization.
  //      but when the second deserialization of session occurs during the start worker phase, version 222 does not have this filter for <non ORACLE_ONLY system variables>,
  //      Therefore serialization filters based on hardcoded system variables will only do <needs serialization> filtering, it will detect more system variables than those in the current session,
  //      These variables detect that they are empty in the current session and will then report an unexpected condition with error code 4016.
  //
  //  To solve the above scenario, version control will be added. If during the upgrade process, the current minimum version number (using GET_MIN_CLUSTER_VERSION()) is not version 2.2.3 or higher,
  //  Then use the old way, serialize the ORACLE_ONLY system variable to the remote machine as well, which can resolve the above error scenario, but another scenario will still have issues:
  //
  //  Issue 2: When version 2.2.3 sends a remote request to version 2.2.2, it calculates the size for serialization once, including the system variables that need to be serialized. Due to the minimum version number
  //  is 2.2.2, will include the ORACLE_ONLY system variable in the serialized variables; when actually performing the serialization action, it will also calculate once the system variables that need to be serialized,
  // If at this point all have upgraded to 2.2.3, then the ORACLE_ONLY variable will not be serialized, at this time it will appear that the actual serialized size is inconsistent with the previously calculated required serialized size, resulting in unexpected behavior;
  //
  //  To solve problem 2, there are two approaches (approach 2 is available):
  //     Approach 1: Push the system variables that need to be serialized into a session member sys_var_ids_ when calculating the serialization size, next time during actual serialization do not recalculate the system variables that need to be serialized
  //            Directly utilize sys_var_ids_ serialization corresponding system variables, this approach will have one problem, which is that we sqc start work for session serialization when, is concurrent, initialization sys_var_ids_
  //            There will be concurrency issues, therefore this approach is not feasible;
  //     Approach 2: Record min_cluster_version_ on the session, initialize before execution, use the same minimum version number to determine which system variables need to be serialized when calculating the serialization size of the session and during actual serialization;
  //            Ensure session serialization size and actual serialized size are consistent;
  //
  uint64_t exec_min_cluster_version_;
  stmt::StmtType stmt_type_;
private:
  // Construct the thread id for the current session, used for the THREAD_ID field in all_virtual_processlist
  // Through this id you can quickly perform `pstack THREADID` operation on the worker
  int64_t thread_id_;
  // indicate whether user password is expired, is set when session is established.
  // will not be changed during whole session lifetime unless user changes password
  // in this session.
  bool is_password_expired_;
  // timestamp of processing current query. refresh when retry.
  int64_t process_query_time_;
  int64_t last_update_tz_time_; //timestamp of last attempt to update timezone info
  bool is_client_sessid_support_; //client session id support flag
  bool is_feedback_proxy_info_support_; // to confirm whether obproxy supports feedback_proxy_info
  bool use_rich_vector_format_;
  int64_t last_refresh_schema_version_;
  // rich format specified hint, e.g. `select /*+opt_param('enable_rich_vector_format', 'true')*/ * from t`
  // force_rich_vector_format_ == FORCE_ON => use_rich_format() returns true
  // force_rich_vector_format_ == FORCE_OFF => use_rich_format() returns false
  // otherwise use_rich_format() returns use_rich_vector_format_
  ForceRichFormatStatus force_rich_vector_format_;
  // just used to plan cache key
  bool config_use_rich_format_;

  common::ObSEArray<uint64_t, 4> enable_role_ids_;
  uint64_t sys_var_config_hash_val_;
  char thread_name_[OB_THREAD_NAME_BUF_LEN];
  bool is_real_inner_session_; 
  // Currently, when inner sql is executed, the session will be created from session_mgr in most cases. We think he is an inner session;
  // In addition, in situations such as PL execution, the external session will be passed to the inner sql Connection. In this case, it is not considered an inner session.
  // There are differences between the two in terms of ASH statistics and so on, so they should be distinguished.
  bool has_ccl_rule_;
  int64_t last_ccl_cnt_update_time_;
public:
  bool get_enable_hyperscan_regexp_engine() const;
  int8_t get_min_const_integer_precision() const;
};


inline const common::ObString ObBasicSessionInfo::get_current_query_string() const
{
  common::ObString str_ret;
  str_ret.assign_ptr(const_cast<char *>(thread_data_.cur_query_), static_cast<int64_t>(thread_data_.cur_query_len_));
  return str_ret;
}

inline const common::ObString ObBasicSessionInfo::get_top_query_string() const
{
  common::ObString str_ret;
  str_ret.assign_ptr(const_cast<char *>(thread_data_.top_query_), static_cast<int32_t>(thread_data_.top_query_len_));
  return str_ret;
}

inline ObCollationType ObBasicSessionInfo::get_local_collation_connection() const
{
  return static_cast<common::ObCollationType>(sys_vars_cache_.get_collation_connection());
}

inline ObCollationType ObBasicSessionInfo::get_nls_collation() const
{
  return sys_vars_cache_.get_nls_collation();
}

inline ObCollationType ObBasicSessionInfo::get_nls_collation_nation() const
{
  return sys_vars_cache_.get_nls_nation_collation();
}

inline const ObString &ObBasicSessionInfo::get_ob_trace_info() const
{
  return sys_vars_cache_.get_ob_trace_info();
}

inline const ObString &ObBasicSessionInfo::get_plsql_ccflags() const
{
  return sys_vars_cache_.get_plsql_ccflags();
}

inline const ObString &ObBasicSessionInfo::get_iso_nls_currency() const
{
  return sys_vars_cache_.get_iso_nls_currency();
}

inline const ObString &ObBasicSessionInfo::get_log_row_value_option() const
{
  return sys_vars_cache_.get_log_row_value_option();
}

inline int64_t ObBasicSessionInfo::get_default_lob_inrow_threshold() const
{
  return sys_vars_cache_.get_default_lob_inrow_threshold();
}

inline bool ObBasicSessionInfo::get_local_autocommit() const
{
  return sys_vars_cache_.get_autocommit();
}

inline uint64_t ObBasicSessionInfo::get_local_auto_increment_increment() const
{
  return sys_vars_cache_.get_auto_increment_increment();
}

inline uint64_t ObBasicSessionInfo::get_local_auto_increment_offset() const
{
  return sys_vars_cache_.get_auto_increment_offset();
}

inline uint64_t ObBasicSessionInfo::get_local_last_insert_id() const
{
  return sys_vars_cache_.get_last_insert_id();
}

inline bool ObBasicSessionInfo::get_local_ob_enable_pl_cache() const
{
  return sys_vars_cache_.get_ob_enable_pl_cache();
}

inline bool ObBasicSessionInfo::get_local_ob_enable_plan_cache() const
{
  return sys_vars_cache_.get_ob_enable_plan_cache();
}

inline bool ObBasicSessionInfo::get_local_ob_enable_sql_audit() const
{
  return sys_vars_cache_.get_ob_enable_sql_audit();
}

inline bool ObBasicSessionInfo::get_local_ob_enable_parameter_anonymous_block() const
{
  return sys_vars_cache_.get_ob_enable_parameter_anonymous_block();
}

inline bool ObBasicSessionInfo::get_local_ob_enable_ps_parameter_anonymous_block() const
{
  return sys_vars_cache_.get_ob_enable_ps_parameter_anonymous_block();
}

inline ObLengthSemantics ObBasicSessionInfo::get_local_nls_length_semantics() const
{
  return sys_vars_cache_.get_nls_length_semantics();
}

//oracle SYS user actual nls_length_semantics is always BYTE
inline ObLengthSemantics ObBasicSessionInfo::get_actual_nls_length_semantics() const
{
  return is_oracle_sys_database_id(get_database_id()) ?
         LS_BYTE : sys_vars_cache_.get_nls_length_semantics();
}

inline int64_t ObBasicSessionInfo::get_local_ob_org_cluster_id() const
{
  return sys_vars_cache_.get_ob_org_cluster_id();
}

inline int64_t ObBasicSessionInfo::get_local_timestamp() const
{
  return sys_vars_cache_.get_timestamp();
}
inline const common::ObString ObBasicSessionInfo::get_local_nls_date_format() const
{
  return sys_vars_cache_.get_nls_date_format();
}
inline const common::ObString ObBasicSessionInfo::get_local_nls_timestamp_format() const
{
  return sys_vars_cache_.get_nls_timestamp_format();
}
inline const common::ObString ObBasicSessionInfo::get_local_nls_timestamp_tz_format() const
{
  return sys_vars_cache_.get_nls_timestamp_tz_format();
}

inline int ObBasicSessionInfo::get_local_nls_format(const ObObjType type, ObString &format_str) const
{
  int ret = common::OB_SUCCESS;
  switch (type) {
    case ObDateTimeType:
      format_str = sys_vars_cache_.get_nls_date_format();
      break;
    default:
      ret = OB_INVALID_DATE_VALUE;
      SQL_SESSION_LOG(WARN, "invalid argument. wrong type for source.", K(ret), K(type));
      break;
  }
  return ret;
}

inline int ObBasicSessionInfo::set_follower_first_feedback(const share::ObFollowerFirstFeedbackType type)
{
  INIT_SUCC(ret);
  if (is_abundant_feedback_support()) {
    if (OB_FAIL(feedback_manager_.add_follower_first_fb_info(type))) {
      SQL_SESSION_LOG(WARN, "fail to add follower first fb info", K(type), K(ret));
    }
  }
  return ret;
}

inline int ObBasicSessionInfo::set_partition_location_feedback(const share::ObFBPartitionParam &param)
{
   INIT_SUCC(ret);
  if (is_abundant_feedback_support()) {
    if (OB_FAIL(feedback_manager_.add_partition_fb_info(param))) {
      SQL_SESSION_LOG(WARN, "fail to add partition fb info", K(param), K(ret));
    }
  }
  return ret;
}
// Object (currently only used for PL, subsequent expr will be handled similarly) execution environment
class ObExecEnv
{
public:
  // Serialization order
  enum ExecEnvType
  {
    SQL_MODE = 0,
    CHARSET_CLIENT,
    COLLATION_CONNECTION,
    COLLATION_DATABASE,
    PLSQL_CCFLAGS,
    PLSQL_OPTIMIZE_LEVEL,
    MAX_ENV,
  };

  static constexpr share::ObSysVarClassType ExecEnvMap[MAX_ENV + 1] = {
    share::SYS_VAR_SQL_MODE,
    share::SYS_VAR_CHARACTER_SET_CLIENT,
    share::SYS_VAR_COLLATION_CONNECTION,
    share::SYS_VAR_COLLATION_DATABASE,
    share::SYS_VAR_PLSQL_CCFLAGS,
    share::SYS_VAR_PLSQL_OPTIMIZE_LEVEL,
    share::SYS_VAR_INVALID
  };

  ObExecEnv() :
    sql_mode_(DEFAULT_OCEANBASE_MODE),
    charset_client_(CS_TYPE_INVALID),
    collation_connection_(CS_TYPE_INVALID),
    collation_database_(CS_TYPE_INVALID),
    plsql_ccflags_(),
    plsql_optimize_level_(2)  // default PLSQL_OPTIMIZE_LEVEL = 2
  { }

  virtual ~ObExecEnv() {}

  TO_STRING_KV(K_(sql_mode),
               K_(charset_client),
               K_(collation_connection),
               K_(collation_database),
               K_(plsql_ccflags),
               K_(plsql_optimize_level));

  void reset();

  bool operator==(const ObExecEnv &other) const;
  bool operator!=(const ObExecEnv &other) const;

  static int gen_exec_env(const ObBasicSessionInfo &session, char* buf, int64_t len, int64_t &pos);
  static int gen_exec_env(const share::schema::ObSysVariableSchema &sys_variable,
                          char* buf,
                          int64_t len,
                          int64_t &pos);

  int init(const ObString &exec_env);
  int load(ObBasicSessionInfo &session, ObIAllocator *alloc = NULL);
  int store(ObBasicSessionInfo &session);

  ObSQLMode get_sql_mode() const { return sql_mode_; }
  ObCharsetType get_charset_client() { return ObCharset::charset_type_by_coll(charset_client_); }
  ObCollationType get_collation_connection() { return collation_connection_; }
  ObCollationType get_collation_database() { return collation_database_; }
  ObString& get_plsql_ccflags() { return plsql_ccflags_; }

  void set_plsql_ccflags(ObString &plsql_ccflags) { plsql_ccflags_ = plsql_ccflags; }

  int64_t get_plsql_optimize_level() { return plsql_optimize_level_; }
  void set_plsql_optimize_level(int64_t level) { plsql_optimize_level_ = plsql_optimize_level_; }

private:
  ObSQLMode sql_mode_;
  ObCollationType charset_client_;
  ObCollationType collation_connection_;
  ObCollationType collation_database_;
  ObString plsql_ccflags_;
  int64_t plsql_optimize_level_;
};


}//end of namespace sql
}//end of namespace oceanbase

#endif /* _OB_OBPROXY_BASIC_SESSION_INFO_H */
