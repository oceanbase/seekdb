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

#ifndef OCEANBASE_SQL_SESSION_OB_SQL_SESSION_INFO_
#define OCEANBASE_SQL_SESSION_OB_SQL_SESSION_INFO_

#include "common/sql_mode/ob_sql_mode.h"
#include "common/ob_range.h"
#include "lib/net/ob_addr.h"
#include "share/ob_define.h"
#include "share/ob_ddl_common.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/ob_name_def.h"
#include "lib/oblog/ob_warning_buffer.h"
#include "lib/list/ob_list.h"
#include "lib/allocator/page_arena.h"
#include "lib/objectpool/ob_pool.h"
#include "lib/time/ob_cur_time.h"
#include "lib/lock/ob_recursive_mutex.h"
#include "lib/hash/ob_link_hashmap.h"
#include "lib/stat/ob_diagnose_info.h"
#include "rpc/obmysql/ob_mysql_packet.h"
#include "sql/ob_sql_config_provider.h"
#include "sql/ob_end_trans_callback.h"
#include "sql/session/ob_session_val_map.h"
#include "sql/session/ob_basic_session_info.h"
#include "sql/monitor/ob_exec_stat.h"
#include "share/rc/ob_tenant_base.h"
#include "share/rc/ob_context.h"
#include "sql/ob_optimizer_trace_impl.h"
#include "observer/dbms_scheduler/ob_dbms_sched_job_utils.h"
#include "sql/plan_cache/ob_plan_cache_util.h"
#include "lib/stat/ob_diagnostic_info_guard.h"

namespace oceanbase
{
namespace observer
{
class ObQueryDriver;
class ObSqlEndTransCb;
class ObPieceCache;
}
namespace pl
{
class ObPLPackageState;
class ObPL;
struct ObPLExecRecursionCtx;
struct ObPLSqlCodeInfo;
class ObPLContext;
class ObDbmsCursorInfo;

class ObPLProfiler;

} // namespace pl

namespace share
{
struct ObSequenceValue;
}
namespace memtable { class ObBtreeIterCache; }
using common::ObPsStmtId;
namespace sql
{
class ObResultSet;
class ObPlanCache;
class ObPsCache;
class ObPsSessionInfo;
class ObPsStmtInfo;
class ObStmt;
class ObSQLSessionInfo;
class ObPlanItemMgr;

class SessionInfoKey
{
public:
  SessionInfoKey() : sessid_(0) { }
  explicit SessionInfoKey(uint32_t sessid) : sessid_(sessid) {}
  uint64_t hash() const
  { uint64_t hash_value = 0;
    hash_value = common::murmurhash(&sessid_, sizeof(sessid_), hash_value);
    return hash_value;
  };
  int hash(uint64_t &hash_val) const
  {
    hash_val = hash();
    return OB_SUCCESS;
  };
  int compare(const SessionInfoKey & r)
  {
    int cmp = 0;
    if (sessid_ < r.sessid_) {
      cmp = -1;
    } else if (sessid_ > r.sessid_) {
      cmp = 1;
    } else {
      cmp = 0;
    }
    return cmp;
  }
public:
  uint32_t sessid_;
};

struct ObContextUnit
{
  inline void free(common::ObIAllocator &alloc) {
    if (nullptr != value_.ptr()) {
      alloc.free(value_.ptr());
      value_.reset();
    }
    if (nullptr != attribute_.ptr()) {
      alloc.free(attribute_.ptr());
      attribute_.reset();
    }
  }
  int deep_copy(const common::ObString &attribute,
                const common::ObString &value,
                common::ObIAllocator &alloc) {
    int ret = OB_SUCCESS;
    if (OB_FAIL(ob_write_string(alloc, attribute, attribute_))) {
      SQL_ENG_LOG(WARN, "failed to copy attribute", K(ret));
    } else if (OB_FAIL(ob_write_string(alloc, value, value_))) {
      alloc.free(attribute_.ptr());
      attribute_.reset();
      SQL_ENG_LOG(WARN, "failed to copy value", K(ret));
    }
    return ret;
  }
  ObString attribute_;
  ObString value_;
  TO_STRING_KV(K(attribute_), K(value_));
  OB_UNIS_VERSION(1);
};

struct ObSessionStat final
{
  ObSessionStat() : total_logical_read_(0), total_physical_read_(0), total_logical_write_(0),
                    total_lock_count_(0), total_cpu_time_us_(0), total_exec_time_us_(0),
                    total_alive_time_us_(0)
      {}
  void reset() { new (this) ObSessionStat(); }

  TO_STRING_KV(K_(total_logical_read), K_(total_physical_read), K_(total_logical_write),
      K_(total_lock_count), K_(total_cpu_time_us), K_(total_exec_time_us), K_(total_alive_time_us));

  uint64_t total_logical_read_;
  uint64_t total_physical_read_;
  uint64_t total_logical_write_;
  uint64_t total_lock_count_;
  uint64_t total_cpu_time_us_;
  uint64_t total_exec_time_us_;
  uint64_t total_alive_time_us_;
};
// The concurrency control of this structure is the same as other variables on the Session
class ObTenantCachedSchemaGuardInfo
{
public:
  ObTenantCachedSchemaGuardInfo()
    : schema_guard_(share::schema::ObSchemaMgrItem::MOD_CACHED_GUARD)
  { reset(); }
  ~ObTenantCachedSchemaGuardInfo() { reset(); }
  void reset();
  // The cached information, which may be inconsistent with the version maintained by the schema service,
  // Caller needs to decide whether to call the refresh_tenant_schema_version interface based on the situation
  share::schema::ObSchemaGetterGuard &get_schema_guard() { return schema_guard_; }
  int refresh_tenant_schema_guard();
  // Try to return the ref of schema_mgr, rule: perform a revert operation on schema_guard every 10s;
  // 1. If session has request access, then after each statement ends, attempt to trigger once;
  // 2. If session does not have frequent access, solve it through background traversal by session_mgr;
  void try_revert_schema_guard();
private:
  share::schema::ObSchemaGetterGuard schema_guard_;
  // Record the timestamp of re-acquiring schema guard, the background needs to have a fallback revert guard ref mechanism to avoid schema mgr slot from being unable to release
  int64_t ref_ts_;
  int64_t schema_version_;
};

typedef common::hash::ObHashMap<uint64_t, pl::ObPLPackageState *,
                                common::hash::NoPthreadDefendMode> ObPackageStateMap;
typedef common::hash::ObHashMap<uint64_t, share::ObSequenceValue,
                                common::hash::NoPthreadDefendMode> ObSequenceCurrvalMap;

#define OB_UTL_TCP_DEFAULT_TX_TIMEOUT -1  //wait indefinitely
struct ObSockFdParam
{
  ObSockFdParam()
  : session_id_(OB_INVALID_ID), m_addr_info_(NULL), tx_timeout_(OB_UTL_TCP_DEFAULT_TX_TIMEOUT), collation_(CS_TYPE_INVALID)
  {}

  ObSockFdParam(const int64_t session_id, void* m_addr_info, const int32_t tx_timeout, const ObCollationType coll_type)
  : session_id_(session_id), m_addr_info_(m_addr_info), tx_timeout_(tx_timeout), collation_(coll_type)
  {}

  int64_t  session_id_;
  void*    m_addr_info_; 
  int32_t  tx_timeout_;
  ObCollationType collation_;

  TO_STRING_KV(K_(session_id), K_(m_addr_info), K_(collation));
};
typedef common::hash::ObHashMap<int64_t, ObSockFdParam, common::hash::NoPthreadDefendMode> ObSockFdMap;
typedef common::hash::ObHashMap<common::ObString,
                                ObContextUnit *,
                                common::hash::NoPthreadDefendMode,
                                common::hash::hash_func<common::ObString>,
                                common::hash::equal_to<common::ObString>,
                                common::hash::SimpleAllocer<typename common::hash::HashMapTypes<common::ObString, ObContextUnit *>::AllocType>,
                                common::hash::NormalPointer,
                                oceanbase::common::ObMalloc,
                                2> ObInnerContextHashMap;
struct ObInnerContextMap {
  ObInnerContextMap(common::ObIAllocator &alloc) : context_name_(),
                    context_map_(nullptr), alloc_(alloc) {}
  void destroy()
  {
    if (OB_NOT_NULL(context_map_)) {
      for (auto it = context_map_->begin(); it != context_map_->end(); ++it) {
        it->second->free(alloc_);
        alloc_.free(it->second);
      }
    }
    destroy_map();
  }
  void destroy_map()
  {
    if (OB_NOT_NULL(context_map_)) {
      context_map_->destroy();
      alloc_.free(context_map_);
      context_map_ = nullptr;
    }
    if (OB_NOT_NULL(context_name_.ptr())) {
      alloc_.free(context_name_.ptr());
      context_name_.reset();
    }
  }
  int init()
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(context_map_ = static_cast<ObInnerContextHashMap *>
                                  (alloc_.alloc(sizeof(ObInnerContextHashMap))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_ENG_LOG(WARN, "failed to alloc mem for hash map", K(ret));
    } else {
      new (context_map_) ObInnerContextHashMap ();
      if (OB_FAIL(context_map_->create(hash::cal_next_prime(32),
                                       ObModIds::OB_HASH_BUCKET,
                                       ObModIds::OB_HASH_NODE))) {
        SQL_ENG_LOG(WARN, "failed to init hash map", K(ret));
      }
    }
    return ret;
  }
  inline bool operator==(const ObInnerContextMap &other) const {
    bool equal1 =  context_name_ == other.context_name_ &&
                  context_map_->size() == other.context_map_->size();
    bool equal2 = true;
    if (equal1) {
      auto it2 = other.context_map_->begin();
      for (auto it1 = context_map_->begin(); it1 != context_map_->end() &&
          it2 != other.context_map_->end(); ++it1, ++it2) {
        if (it1->second != it2->second) {
          equal2 = false;
        }
      }
    }

    return equal1 && equal2;
  }
  ObString context_name_;
  ObInnerContextHashMap *context_map_;
  common::ObIAllocator &alloc_;
  OB_UNIS_VERSION(1);
public:
  TO_STRING_KV(K(context_name_), K(context_map_->size()));
};
typedef common::hash::ObHashMap<common::ObString, ObInnerContextMap *,
                                common::hash::NoPthreadDefendMode,
                                common::hash::hash_func<common::ObString>,
                                common::hash::equal_to<common::ObString>,
                                common::hash::SimpleAllocer<typename common::hash::HashMapTypes<common::ObString, ObInnerContextMap *>::AllocType>,
                                common::hash::NormalPointer,
                                oceanbase::common::ObMalloc,
                                2> ObContextsMap;
typedef common::LinkHashNode<SessionInfoKey> SessionInfoHashNode;
typedef common::LinkHashValue<SessionInfoKey> SessionInfoHashValue;
// ObBasicSessionInfo stores system variables and related variables, and stores the state that needs to be serialized to the remote when executing SQL tasks remotely
// ObPsInfoMgr stores prepared statement related information
// ObSQLSessionInfo stores other runtime state information, remote execution of SQL execution plan does not require serialization to the remote end
class ObSQLSessionInfo: public common::ObVersionProvider, public ObBasicSessionInfo, public SessionInfoHashValue
{
  OB_UNIS_VERSION(1);
public:
  friend class LinkExecCtxGuard;
  // notice!!! register exec ctx to session for later access
  // used for temp session, such as session for rpc processor, px worker, das processor, etc
  // not used for main session
  class ExecCtxSessionRegister
  {
  public:
    ExecCtxSessionRegister(ObSQLSessionInfo &session, ObExecContext *exec_ctx)
    {
      session.set_cur_exec_ctx(exec_ctx);
    }
  };
  friend class ExecCtxSessionRegister;
  enum SessionType
  {
    INVALID_TYPE,
    USER_SESSION,
    INNER_SESSION
  };
  // for switch stmt.
  class StmtSavedValue : public ObBasicSessionInfo::StmtSavedValue
  {
  public:
    StmtSavedValue()
      : ObBasicSessionInfo::StmtSavedValue()
    {
      reset();
    }
    ~StmtSavedValue()
    {
      reset();
    }
    inline void reset()
    {
      ObBasicSessionInfo::StmtSavedValue::reset();
      audit_record_.reset();
      session_type_ = INVALID_TYPE;
      inner_flag_ = false;
      is_ignore_stmt_ = false;
      catalog_id_ = OB_INVALID_ID;
      db_id_ = OB_INVALID_ID;
      db_name_.reset();
    }
  public:
    ObAuditRecordData audit_record_;
    SessionType session_type_;
    bool inner_flag_;
    bool is_ignore_stmt_;
    uint64_t catalog_id_;
    uint64_t db_id_;
    common::ObSqlString db_name_;
  };

  class CursorCache {
    public:
      CursorCache() : mem_context_(nullptr), next_cursor_id_(1LL << 31), pl_cursor_map_(), pl_non_session_cursor_map_() {}
      virtual ~CursorCache() { NULL != mem_context_ ? DESTROY_CONTEXT(mem_context_) : (void)(NULL); }
      int init()
      {
        int ret = OB_SUCCESS;
        if (OB_FAIL(ROOT_CONTEXT->CREATE_CONTEXT(mem_context_,
            lib::ContextParam().set_mem_attr(ObModIds::OB_PL)))) {
          SQL_ENG_LOG(WARN, "create memory entity failed");
        } else if (OB_ISNULL(mem_context_)) {
          ret = OB_ERR_UNEXPECTED;
          SQL_ENG_LOG(WARN, "null memory entity returned");
        } else if (!pl_cursor_map_.created() &&
                   OB_FAIL(pl_cursor_map_.create(common::hash::cal_next_prime(32),
                                                 ObModIds::OB_HASH_BUCKET, ObModIds::OB_HASH_NODE))) {
          if (NULL != mem_context_) {
            DESTROY_CONTEXT(mem_context_);
            mem_context_ = NULL;
          }
          SQL_ENG_LOG(WARN, "create sequence current value map failed", K(ret));
        } else if (!pl_non_session_cursor_map_.created() &&
                   OB_FAIL(pl_non_session_cursor_map_.create(common::hash::cal_next_prime(32),
                                                       ObModIds::OB_HASH_BUCKET, ObModIds::OB_HASH_NODE))) {
          if (pl_cursor_map_.created()) {
            pl_cursor_map_.destroy();
          }
          if (NULL != mem_context_) {
            DESTROY_CONTEXT(mem_context_);
            mem_context_ = NULL;
          }
          SQL_ENG_LOG(WARN, "create pl_non_session_cursor_map_ failed", K(ret));
        } else { /*do nothing*/ }
        return ret;
      }

      int close_all(sql::ObSQLSessionInfo &session)
      {
        int ret = OB_SUCCESS;
        while (pl_cursor_map_.size() > 0) { // ignore error, just log, try to close all cursor in this loop.
          int ret = OB_SUCCESS;
          CursorMap::iterator iter = pl_cursor_map_.begin();
          pl::ObPLCursorInfo *cursor = NULL;
          if (iter == pl_cursor_map_.end()) {
            ret = OB_ERR_UNEXPECTED;
            SQL_ENG_LOG(ERROR, "unexpected hashmap iter", K(ret));
            break;
          } else if (OB_ISNULL(cursor = iter->second)) {
            ret = OB_ERR_UNEXPECTED;
            SQL_ENG_LOG(WARN, "unexpected nullptr cursor info", K(ret), K(iter->first));
            if (OB_FAIL(pl_cursor_map_.erase_refactored(iter->first))) {
              SQL_ENG_LOG(ERROR, "failed to erase hash map", K(ret), K(iter->first));
              break;
            }
          } else if (OB_FAIL(session.close_cursor(cursor->get_id()))) {
            SQL_ENG_LOG(WARN, "failed to close session cursor", K(ret), K(cursor->get_id()));
          } else {
            SQL_ENG_LOG(INFO, "clsoe session cursor implicit successed!", K(cursor->get_id()));
          }
        }
        if (pl_cursor_map_.size() > 0) {
          ret = OB_ERR_UNEXPECTED;
          SQL_ENG_LOG(ERROR, "failed to close all cursor", K(ret), K(pl_cursor_map_.size()));
        }
        return ret;
      }

      inline bool is_inited() const { return NULL != mem_context_; }
      void reset()
      {
        int ret = OB_SUCCESS;
        if (pl_cursor_map_.size() != 0) {
          ret = OB_ERR_UNEXPECTED;
          SQL_ENG_LOG(ERROR, "session cursor map not empty, cursor leaked", K(pl_cursor_map_.size()));
        }
        pl_cursor_map_.reuse();
        pl_non_session_cursor_map_.reuse();
        next_cursor_id_ = 1LL << 31;
        if (NULL != mem_context_) {
          DESTROY_CONTEXT(mem_context_);
          mem_context_ = NULL;
        }
      }
      inline int64_t gen_cursor_id() { return __sync_add_and_fetch(&next_cursor_id_, 1); }
    public:
      lib::MemoryContext mem_context_;
      int64_t next_cursor_id_;
      typedef common::hash::ObHashMap<int64_t, pl::ObPLCursorInfo*,
                                      common::hash::NoPthreadDefendMode> CursorMap;
      CursorMap pl_cursor_map_;
      CursorMap pl_non_session_cursor_map_;
  };

  class ObCachedTenantConfigInfo
  {
  public:
    ObCachedTenantConfigInfo(ObSQLSessionInfo *session) :
                                 enable_batched_multi_statement_(false),
                                 enable_sql_extension_(false),
                                 enable_bloom_filter_(true),
                                 px_join_skew_handling_(true),
                                 px_join_skew_minfreq_(30),
                                 sort_area_size_(128*1024*1024),
                                 hash_area_size_(128*1024*1024),
                                 data_version_(0),
                                 enable_query_response_time_stats_(false),
                                 enable_immediate_row_conflict_check_(false),
                                 range_optimizer_max_mem_size_(128*1024*1024),
                                 _query_record_size_limit_(65536),
                                 enable_decimal_int_type_(false),
                                 enable_mysql_compatible_dates_(false),
                                 print_sample_ppm_(0),
                                 last_check_ec_ts_(0),
                                 enable_enhanced_cursor_validation_(false),
                                 enable_enum_set_subschema_(false),
                                 _ob_sqlstat_enable_(true),
                                 force_enable_plan_tracing_(false),
                                 pc_adaptive_min_exec_time_threshold_(0),
                                 pc_adaptive_effectiveness_ratio_threshold_(0),
                                 enable_adaptive_plan_cache_(false),
                                 enable_sql_ccl_rule_(true),
                                 session_(session)
    {
    }
    ~ObCachedTenantConfigInfo() {}
    void refresh();
    bool get_enable_batched_multi_statement() const { return enable_batched_multi_statement_; }
    bool get_enable_bloom_filter() const { return enable_bloom_filter_; }
    bool get_enable_sql_extension() const { return enable_sql_extension_; }
    int64_t get_sort_area_size() const { return ATOMIC_LOAD(&sort_area_size_); }
    int64_t get_hash_area_size() const { return ATOMIC_LOAD(&hash_area_size_); }
    uint64_t get_data_version() const { return ATOMIC_LOAD(&data_version_); }
    bool enable_query_response_time_stats() const { return enable_query_response_time_stats_; }
    int64_t get_print_sample_ppm() const { return ATOMIC_LOAD(&print_sample_ppm_); }
    bool get_px_join_skew_handling() const { return px_join_skew_handling_; }
    int64_t get_px_join_skew_minfreq() const { return px_join_skew_minfreq_; }
    int64_t get_range_optimizer_max_mem_size() const { return range_optimizer_max_mem_size_; }
    int64_t get_query_record_size_limit() const { return _query_record_size_limit_; }
    bool get_enable_decimal_int_type() const { return enable_decimal_int_type_; }
    bool enable_enhanced_cursor_validation() const { return enable_enhanced_cursor_validation_; }
    bool get_enable_mysql_compatible_dates() const { return enable_mysql_compatible_dates_; }
    bool enable_enum_set_subschema() const { return enable_enum_set_subschema_; }
    bool get_ob_sqlstat_enable() const { return _ob_sqlstat_enable_; }
    bool enable_immediate_row_conflict_check() const { return ATOMIC_LOAD(&enable_immediate_row_conflict_check_); }
    bool force_enable_plan_tracing() const
    {
      return force_enable_plan_tracing_;
    }
    int64_t get_pc_adaptive_effectiveness_ratio_threshold() const
    {
      return pc_adaptive_effectiveness_ratio_threshold_;
    }
    int64_t get_pc_adaptive_min_exec_time_threshold() const
    {
      return pc_adaptive_min_exec_time_threshold_;
    }
    bool enable_plan_cache_adaptive() const
    {
      return enable_adaptive_plan_cache_;
    }
    bool enable_sql_ccl_rule() const
    {
      return enable_sql_ccl_rule_;
    }

  private:
    // Tenant-level configuration item cache session, avoid refreshing every time it is retrieved
    bool enable_batched_multi_statement_;
    bool enable_sql_extension_;
    bool enable_bloom_filter_;
    bool px_join_skew_handling_;
    int64_t px_join_skew_minfreq_;
    int64_t sort_area_size_;
    int64_t hash_area_size_;
    uint64_t data_version_;
    bool enable_query_response_time_stats_;
    bool enable_immediate_row_conflict_check_;
    int64_t range_optimizer_max_mem_size_;
    int64_t _query_record_size_limit_;
    bool enable_decimal_int_type_;
    bool enable_mysql_compatible_dates_;
    // for record sys config print_sample_ppm
    int64_t print_sample_ppm_;
    int64_t last_check_ec_ts_;
    bool enable_enhanced_cursor_validation_;
    bool enable_enum_set_subschema_;
    bool _ob_sqlstat_enable_;
    bool force_enable_plan_tracing_;
    int64_t pc_adaptive_min_exec_time_threshold_;
    int64_t pc_adaptive_effectiveness_ratio_threshold_;
    bool enable_adaptive_plan_cache_;
    bool enable_sql_ccl_rule_;
    ObSQLSessionInfo *session_;
  };

  class ApplicationInfo {
    OB_UNIS_VERSION(1);
  public:
    common::ObString module_name_;  // name as set by the dbms_application_info(set_module)
    common::ObString action_name_;  // action as set by the dbms_application_info(set_action)
    common::ObString client_info_;  // for addition info
    void reset() {
      module_name_.reset();
      action_name_.reset();
      client_info_.reset();
    }
    TO_STRING_KV(K_(module_name), K_(action_name), K_(client_info));
  };


public:
  ObSQLSessionInfo();
  virtual ~ObSQLSessionInfo();

  int init(uint32_t sessid,
           common::ObIAllocator *bucket_allocator,
           const ObTZInfoMap *tz_info = NULL);
  //for test
  int test_init(uint32_t version, uint32_t sessid,
           common::ObIAllocator *bucket_allocator);
  void destroy(bool skip_sys_var = false);
  void reset(bool skip_sys_var);
  void clean_status();
  void set_plan_cache(ObPlanCache *cache) { plan_cache_ = cache; }
  void set_ps_cache(ObPsCache *cache) { ps_cache_ = cache; }
  const common::ObWarningBuffer &get_show_warnings_buffer() const { return show_warnings_buf_; }
  const common::ObWarningBuffer &get_warnings_buffer() const { return warnings_buf_; }
  common::ObWarningBuffer &get_warnings_buffer() { return warnings_buf_; }

  // self-verification add.

  void reset_warnings_buf()
  {
    warnings_buf_.reset();
    pl_exact_err_msg_.reset();
  }
  void reset_show_warnings_buf() { show_warnings_buf_.reset(); }
  ObPrivSet get_user_priv_set() const { return user_priv_set_; }
  ObPrivSet get_db_priv_set() const { return db_priv_set_; }
  ObPlanCache *get_plan_cache();
  ObPlanCache *get_plan_cache_directly() const { return plan_cache_; };
  ObPsCache *get_ps_cache();
  memtable::ObBtreeIterCache *get_btree_iter_cache() { return btree_iter_cache_; }
  void set_user_priv_set(const ObPrivSet priv_set) { user_priv_set_ = priv_set; }
  void set_db_priv_set(const ObPrivSet priv_set) { db_priv_set_ = priv_set; }
  void set_show_warnings_buf(int error_code);
  void update_show_warnings_buf();
  void set_global_sessid(const int64_t global_sessid)
  {
    global_sessid_ = global_sessid;
  }
  int64_t get_global_sessid() const { return global_sessid_; }
  void set_read_uncommited(bool read_uncommited) { read_uncommited_ = read_uncommited; }
  bool get_read_uncommited() const { return read_uncommited_; }
  void set_version_provider(const common::ObVersionProvider *version_provider)
  {
    version_provider_ = version_provider;
  }
  const common::ObVersion get_frozen_version() const
  {
    return version_provider_->get_frozen_version();
  }
  const common::ObVersion get_merged_version() const
  {
    return version_provider_->get_merged_version();
  }
  void set_config_provider(const ObSQLConfigProvider *config_provider)
  {
    config_provider_ = config_provider;
  }
  bool is_read_only() const { return config_provider_->is_read_only(); };
  int64_t get_nlj_cache_limit() const { return config_provider_->get_nlj_cache_limit(); };
  bool is_terminate(int &ret) const;

  void set_curr_trans_start_time(int64_t t) { curr_trans_start_time_ = t; };
  int64_t get_curr_trans_start_time() const { return curr_trans_start_time_; };

  void set_curr_trans_last_stmt_time(int64_t t) { curr_trans_last_stmt_time_ = t; };
  int64_t get_curr_trans_last_stmt_time() const { return curr_trans_last_stmt_time_; };

  void set_sess_create_time(const int64_t t) { sess_create_time_ = t; };
  int64_t get_sess_create_time() const { return sess_create_time_; };

  void set_has_temp_table_flag() { has_temp_table_flag_ = true; };
  bool get_has_temp_table_flag() const { return has_temp_table_flag_; };
  void set_accessed_session_level_temp_table() { has_accessed_session_level_temp_table_ = true; }
  bool has_accessed_session_level_temp_table() const { return has_accessed_session_level_temp_table_; }
  // Clear temporary table
  int drop_temp_tables(const bool is_sess_disconn = true,
                       const bool is_reset_connection = false);

  void set_for_trigger_package(bool value) { is_for_trigger_package_ = value; }
  bool is_for_trigger_package() const { return is_for_trigger_package_; }
  void set_trans_type(transaction::ObTxClass t) { trans_type_ = t; }
  transaction::ObTxClass get_trans_type() const { return trans_type_; }

  int get_session_priv_info(share::schema::ObSessionPrivInfo &session_priv) const;
  void set_found_rows(const int64_t count) { found_rows_ = count; }
  int64_t get_found_rows() const { return found_rows_; }
  void set_affected_rows(const int64_t count) { affected_rows_ = count; }
  int64_t get_affected_rows() const { return affected_rows_; }
  bool has_user_super_privilege() const;
  bool has_user_process_privilege() const;
  int check_read_only_privilege(const bool read_only,
                                const ObSqlTraits &sql_traits);
  int check_global_read_only_privilege(const bool read_only,
                                       const ObSqlTraits &sql_traits);

  int remove_prepare(const ObString &ps_name);
  int get_prepare_id(const ObString &ps_name, ObPsStmtId &ps_id) const;
  int add_prepare(const ObString &ps_name, ObPsStmtId ps_id);
  int remove_ps_session_info(const ObPsStmtId stmt_id);
  int get_ps_session_info(const ObPsStmtId stmt_id,
                          ObPsSessionInfo *&ps_session_info) const;
  int check_ps_stmt_id_in_use(const ObPsStmtId stmt_id, bool &is_in_use);
  int add_ps_stmt_id_in_use(const ObPsStmtId stmt_id);
  int earse_ps_stmt_id_in_use(const ObPsStmtId stmt_id);
  template <typename Visitor>
  int visit_ps_session_info(const ObPsStmtId stmt_id,
                          Visitor &visitor)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(!ps_session_info_map_.created())) {
      ret = OB_HASH_NOT_EXIST;
      SQL_ENG_LOG(WARN, "map not created before insert any element", K(ret));
    } else if (OB_FAIL(ps_session_info_map_.read_atomic<Visitor>(stmt_id, visitor))) {
      SQL_ENG_LOG(WARN, "get ps session info failed", K(ret), K(stmt_id), K(get_server_sid()));
      if (ret == OB_HASH_NOT_EXIST) {
        ret = OB_EER_UNKNOWN_STMT_HANDLER;
      }
    }
    return ret;
  }
  template <typename T>
  int update_ps_session_info_safety(const ObPsStmtId stmt_id, T &update)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(!ps_session_info_map_.created())) {
      ret = OB_HASH_NOT_EXIST;
      SQL_ENG_LOG(WARN, "map not created before insert any element", K(ret));
    } else if (OB_FAIL(ps_session_info_map_.atomic_refactored<T>(stmt_id, update))) {
      SQL_ENG_LOG(WARN, "get ps session info failed", K(ret), K(stmt_id), K(get_server_sid()));
      if (ret == OB_HASH_NOT_EXIST) {
        ret = OB_EER_UNKNOWN_STMT_HANDLER;
      }
    }
    return ret;
  }
  int64_t get_ps_session_info_size() const { return ps_session_info_map_.size(); }
  inline pl::ObPL *get_pl_engine() const { return GCTX.pl_engine_; }

  pl::ObPLCursorInfo *get_pl_implicit_cursor();

  pl::ObPLSqlCodeInfo *get_pl_sqlcode_info();

  bool has_pl_implicit_savepoint();
  void clear_pl_implicit_savepoint();
  void set_has_pl_implicit_savepoint(bool v);

  inline pl::ObPLContext *get_pl_context() { return pl_context_; }
  inline const pl::ObPLContext *get_pl_context() const { return pl_context_; }
  inline void set_pl_stack_ctx(pl::ObPLContext *pl_stack_ctx)
  {
    pl_context_ = pl_stack_ctx;
  }

  bool is_pl_debug_on();
  inline pl::ObPLProfiler *get_pl_profiler() const
  {
    pl::ObPLProfiler *profiler = nullptr;


    return profiler;
  }

  inline void set_pl_attached_id(uint32_t id) { pl_attach_session_id_ = id; }
  inline uint32_t get_pl_attached_id() const { return pl_attach_session_id_; }

  inline common::hash::ObHashSet<common::ObString> *get_pl_sync_pkg_vars()
  {
    return pl_sync_pkg_vars_;
  }

  inline void set_pl_query_sender(observer::ObQueryDriver *driver) { pl_query_sender_ = driver; }
  inline observer::ObQueryDriver* get_pl_query_sender() { return pl_query_sender_; }

  inline void set_ps_protocol(bool is_ps_protocol) { pl_ps_protocol_ = is_ps_protocol; }
  inline bool is_ps_protocol() { return pl_ps_protocol_; }

  int replace_user_variable(const common::ObString &name, const ObSessionVariable &value);
  int replace_user_variable(
    ObExecContext &ctx, const common::ObString &name, const ObSessionVariable &value);
  int replace_user_variables(ObExecContext &ctx, const ObSessionValMap &user_var_map);
  int set_package_variables(ObExecContext &ctx, const ObSessionValMap &user_var_map);
  int set_package_variable(ObExecContext &ctx,
    const common::ObString &key, const common::ObObj &value, bool from_proxy = false);

  inline bool get_pl_can_retry() { return pl_can_retry_; }
  inline void set_pl_can_retry(bool can_retry) { pl_can_retry_ = can_retry; }

  void reset_plsql_exec_time() { plsql_exec_time_ = 0; }
  void add_plsql_exec_time(int64_t plsql_exec_time) { plsql_exec_time_ += plsql_exec_time; }
  void reset_plsql_compile_time() { plsql_compile_time_ = 0; }
  void add_plsql_compile_time(int64_t plsql_compile_time) { plsql_compile_time_ += plsql_compile_time; }

  CursorCache &get_cursor_cache() { return pl_cursor_cache_; }
  pl::ObPLCursorInfo *get_cursor(int64_t cursor_id);
  pl::ObDbmsCursorInfo *get_dbms_cursor(int64_t cursor_id);
  int add_cursor(pl::ObPLCursorInfo *cursor);
  int close_cursor(pl::ObPLCursorInfo *&cursor);
  int close_cursor(int64_t cursor_id);
  inline void inc_session_cursor() {
    EVENT_INC(SQL_OPEN_CURSORS_CURRENT);
    EVENT_INC(SQL_OPEN_CURSORS_CUMULATIVE);
  };
  inline void dec_session_cursor() {
    EVENT_DEC(SQL_OPEN_CURSORS_CURRENT);
  };
  int make_cursor(pl::ObPLCursorInfo *&cursor);
  int add_non_session_cursor(pl::ObPLCursorInfo *cursor);
  void del_non_session_cursor(pl::ObPLCursorInfo *cursor);
  int init_cursor_cache();
  int make_dbms_cursor(pl::ObDbmsCursorInfo *&cursor,
                       uint64_t id = OB_INVALID_ID);
  int print_all_cursor();

  inline void *get_inner_conn() { return inner_conn_; }
  inline void set_inner_conn(void *inner_conn)
  {
    inner_conn_ = inner_conn;
  }

  // show trace

  ObEndTransAsyncCallback &get_end_trans_cb() { return end_trans_cb_; }
  observer::ObSqlEndTransCb &get_mysql_end_trans_cb()
  {
    return end_trans_cb_.get_mysql_end_trans_cb();
  }
  int get_collation_type_of_names(const ObNameTypeClass type_class, common::ObCollationType &cs_type) const;
  int kill_query();
  int set_query_deadlocked();

  inline void set_inner_session()
  {
    inner_flag_ = true;
    session_type_ = INNER_SESSION;
  }
  inline void set_user_session()
  {
    inner_flag_ = false;
    session_type_ = USER_SESSION;
  }
  void set_session_type_with_flag();
  void set_session_type(SessionType session_type) { session_type_ = session_type; }
  inline SessionType get_session_type() const { return session_type_; }
  // sql from obclient, proxy, PL are all marked as user_session
  // NOTE: for sql from PL, is_inner() = true, is_user_session() = true
  inline bool is_user_session() const { return USER_SESSION == session_type_; }
  void set_early_lock_release(bool enable);
  bool get_early_lock_release() const { return enable_early_lock_release_; }

  bool is_inner() const
  {
    return inner_flag_;
  }
  void reset_audit_record(bool need_retry = false)
  {
    if (!need_retry) {
      audit_record_.reset();
    } else {
      // memset without try_cnt_ and exec_timestamp_
      int64_t try_cnt = audit_record_.try_cnt_;
      ObExecTimestamp exec_timestamp = audit_record_.exec_timestamp_;
      audit_record_.reset();
      audit_record_.try_cnt_ = try_cnt;
      audit_record_.exec_timestamp_ = exec_timestamp;
    }
  }
  ObAuditRecordData &get_raw_audit_record() { return audit_record_; }
  const ObAuditRecordData &get_raw_audit_record() const { return audit_record_; }
  // When finally need to push record to audit buffer, use this method,
  // This method will retrieve some session data that can be obtained and will not change during the retry process
  // Field initialization
  ObSessionStat &get_session_stat() { return session_stat_; }
  void update_stat_from_exec_record();
  void update_stat_from_exec_timestamp();

  void set_is_remote(bool is_remote) { is_remote_session_ = is_remote; }
  bool is_remote_session() const { return is_remote_session_; }

  int save_session(StmtSavedValue &saved_value);
  int save_sql_session(StmtSavedValue &saved_value);
  int restore_sql_session(StmtSavedValue &saved_value);
  int restore_session(StmtSavedValue &saved_value);
  ObExecContext *get_cur_exec_ctx() { return cur_exec_ctx_; }
  const ObExecContext *get_cur_exec_ctx() const { return cur_exec_ctx_; }
  int begin_nested_session(StmtSavedValue &saved_value, bool skip_cur_stmt_tables = false);
  int end_nested_session(StmtSavedValue &saved_value);

  //package state related
  inline  ObPackageStateMap &get_package_state_map() { return package_state_map_; }
  inline int get_package_state(uint64_t package_id, pl::ObPLPackageState *&package_state)
  {
    return package_state_map_.get_refactored(package_id, package_state);
  }
  inline int add_package_state(uint64_t package_id, pl::ObPLPackageState *package_state)
  {
    return package_state_map_.set_refactored(package_id, package_state);
  }
  inline int del_package_state(uint64_t package_id)
  {
    return package_state_map_.erase_refactored(package_id);
  }
  void reset_pl_debugger_resource();
  void reset_all_package_changed_info();
  void reset_all_package_state();
  int reset_all_package_state_by_dbms_session(bool need_set_sync_var);
  int reset_all_serially_package_state();
  bool is_package_state_changed() const;
  int add_changed_package_info(ObExecContext &exec_ctx);
  // Current session occurred sequence.nextval read sequence value,
  // All will be saved on the current session by the ObSequence operator
  int get_sequence_value(uint64_t seq_id,
                         share::ObSequenceValue &value);
  int set_sequence_value(uint64_t seq_id,
                         const share::ObSequenceValue &value);
  int drop_sequence_value_if_exists(uint64_t seq_id);
  int get_next_sequence_id(uint64_t &seq_id);
  void reuse_all_sequence_value()
  {
    sequence_currval_map_.reuse();
  }
  int set_context_values(const common::ObString &context_name,
                        const common::ObString &attribute,
                        const common::ObString &value);
  int clear_all_context(const common::ObString &context_name);
  int clear_context(const common::ObString &context_name,
                    const common::ObString &attribute);
  int64_t get_curr_session_context_size() const { return curr_session_context_size_; }
  void reuse_context_map()
  {
    for (auto it = contexts_map_.begin(); it != contexts_map_.end(); ++it) {
      if (OB_NOT_NULL(it->second)) {
        it->second->destroy();
        mem_context_->get_malloc_allocator().free(it->second);
      }
    }
    contexts_map_.reuse();
    curr_session_context_size_ = 0;
  }

  int set_client_id(const common::ObString &client_identifier);
  int set_module_name(const common::ObString &mod);
  int set_action_name(const common::ObString &act);
  int set_client_info(const common::ObString &client_info);
  ApplicationInfo& get_client_app_info() { return client_app_info_; }
  const common::ObString& get_module_name() const { return client_app_info_.module_name_; }
  const common::ObString& get_action_name() const  { return client_app_info_.action_name_; }
  const common::ObString& get_client_info() const { return client_app_info_.client_info_; }
  ObContextsMap &get_contexts_map() { return contexts_map_; }
  ObSequenceCurrvalMap &get_sequence_currval_map() { return sequence_currval_map_; }
  ObSockFdMap &get_sock_fd_map() { return sock_fd_map_; }
  const common::ObString &get_audit_filter_name() const { return audit_filter_name_; }
  int get_mem_ctx_alloc(common::ObIAllocator *&alloc);
  int prepare_ps_stmt(const ObPsStmtId inner_stmt_id,
                      const ObPsStmtInfo *stmt_info,
                      ObPsStmtId &client_stmt_id,
                      bool &already_exists,
                      bool is_inner_sql);
  int get_inner_ps_stmt_id(ObPsStmtId cli_stmt_id, ObPsStmtId &inner_stmt_id);
  int close_ps_stmt(ObPsStmtId stmt_id);
  void reset_ps_session_info() { ps_session_info_map_.reuse(); }
  void reset_ps_name() 
  {
    ps_name_id_map_.reuse();
    next_client_ps_stmt_id_ = 0;
  }

  int is_force_temp_table_inline(bool &force_inline) const;
  int is_force_temp_table_materialize(bool &force_materialize) const;
  int is_groupby_placement_transformation_enabled(bool &transformation_enabled) const;
  bool is_in_range_optimization_enabled() const;
  int64_t get_inlist_rewrite_threshold() const;
  int is_better_inlist_enabled(bool &enabled) const;
  bool is_index_skip_scan_enabled() const;
  bool is_qualify_filter_enabled() const;
  int is_enable_range_extraction_for_not_in(bool &enabled) const;
  bool is_var_assign_use_das_enabled() const;
  int is_adj_index_cost_enabled(bool &enabled, int64_t &stats_cost_percent) const;
  bool is_spf_mlj_group_rescan_enabled() const;
  bool enable_parallel_das_dml() const;
  int is_preserve_order_for_pagination_enabled(bool &enabled) const;
  int is_preserve_order_for_groupby_enabled(bool &enabled) const;
  bool is_sqlstat_enabled();
  int has_ccl_rules(share::schema::ObSchemaGetterGuard *&schema_guard, bool &has_ccl_rules);
  ObSessionDDLInfo &get_ddl_info() { return ddl_info_; }
  const ObSessionDDLInfo &get_ddl_info() const { return ddl_info_; }
  void set_ddl_info(const ObSessionDDLInfo &ddl_info) { ddl_info_ = ddl_info; }
  bool is_table_name_hidden() const { return is_table_name_hidden_; }
  void set_table_name_hidden(const bool is_hidden) { is_table_name_hidden_ = is_hidden; }

  ObTenantCachedSchemaGuardInfo &get_cached_schema_guard_info() { return cached_schema_guard_info_; }
  int set_enable_role_array(const common::ObIArray<uint64_t> &role_id_array);
  common::ObIArray<uint64_t>& get_enable_role_array() { return get_enable_role_ids(); }
  const common::ObIArray<uint64_t>& get_enable_role_array() const { return get_enable_role_ids(); }
  void set_in_definer_named_proc(bool in_proc) {in_definer_named_proc_ = in_proc; }
  bool get_in_definer_named_proc() {return in_definer_named_proc_; }
  bool get_prelock() { return prelock_; }
  void set_prelock(bool prelock) { prelock_ = prelock; }

  void set_priv_user_id(uint64_t priv_user_id) { priv_user_id_ = priv_user_id; }
  uint64_t get_priv_user_id() const {
    return (priv_user_id_ == OB_INVALID_ID) ? get_user_id() : priv_user_id_; }
  uint64_t get_priv_user_id_allow_invalid() { return priv_user_id_; }
  // For performance optimization, tenant-level configuration items do not need to be retrieved in real time, they are cached in the session, and refreshed every 5s
  void refresh_tenant_config() { cached_tenant_config_info_.refresh(); }
  bool is_enable_batched_multi_statement()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_enable_batched_multi_statement();
  }
  bool is_enable_bloom_filter()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_enable_bloom_filter();
  }
  int64_t get_px_join_skew_minfreq()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_px_join_skew_minfreq();
  }
  bool get_px_join_skew_handling()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_px_join_skew_handling();
  }

  bool is_enable_sql_extension()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_enable_sql_extension();
  }
  bool is_varparams_sql_prepare() const { return is_varparams_sql_prepare_; }
  void set_is_varparams_sql_prepare(bool v) { is_varparams_sql_prepare_ = v; }
  int64_t get_tenant_hash_area_size()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_hash_area_size();
  }
  int64_t get_tenant_sort_area_size()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_sort_area_size();
  }
  uint64_t get_data_version()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_data_version();
  }
  bool enable_query_response_time_stats()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.enable_query_response_time_stats();
  }
  bool enable_immediate_row_conflict_check()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.enable_immediate_row_conflict_check();
  }
  int64_t get_range_optimizer_max_mem_size()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_range_optimizer_max_mem_size();
  }
  int64_t get_tenant_print_sample_ppm()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_print_sample_ppm();
  }
  bool is_enable_mysql_compatible_dates()
  {
    return enable_mysql_compatible_dates();
  }
  bool get_enable_mysql_compatible_dates_from_config()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_enable_mysql_compatible_dates();
  }
  bool is_enable_decimal_int_type()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_enable_decimal_int_type();
  }
  int64_t get_tenant_query_record_size_limit()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_query_record_size_limit();
  }
  bool enable_enhanced_cursor_validation()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.enable_enhanced_cursor_validation();
  }
  bool is_enable_enum_set_with_subschema()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.enable_enum_set_subschema();
  }
  bool get_tenant_ob_sqlstat_enable()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.get_ob_sqlstat_enable();
  }
  bool force_enable_plan_tracing()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.force_enable_plan_tracing();
  }
  const AdaptivePCConf get_adaptive_pc_conf()
  {
    AdaptivePCConf conf;
    cached_tenant_config_info_.refresh();
    conf.enable_adaptive_plan_cache_ = cached_tenant_config_info_.enable_plan_cache_adaptive();
    conf.pc_adaptive_effectiveness_ratio_threshold_ =
      cached_tenant_config_info_.get_pc_adaptive_effectiveness_ratio_threshold();
    conf.pc_adaptive_min_exec_time_threshold_ = 
      cached_tenant_config_info_.get_pc_adaptive_min_exec_time_threshold();
    return conf;
  }
  bool enable_plan_cache_adaptive()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.enable_plan_cache_adaptive();
  }
  bool is_enable_sql_ccl_rule()
  {
    cached_tenant_config_info_.refresh();
    return cached_tenant_config_info_.enable_sql_ccl_rule();
  }
  int get_tmp_table_size(uint64_t &size);
  int ps_use_stream_result_set(bool &use_stream);

  void set_ignore_stmt(bool v) { is_ignore_stmt_ = v; }
  bool is_ignore_stmt() const { return is_ignore_stmt_; }

  // piece
  observer::ObPieceCache *get_piece_cache(bool need_init = false);
  void set_piece_cache(void* piece_cache) { piece_cache_ = reinterpret_cast<observer::ObPieceCache*>(piece_cache); }

  share::schema::ObUserLoginInfo get_login_info () { return login_info_; }
  int set_login_info(const share::schema::ObUserLoginInfo &login_info);
  int set_login_auth_data(const ObString &auth_data);
  template <typename Function>
  int for_each_ps_session_info(Function &fn)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(!ps_session_info_map_.created())) {
      // do nothing
    } else if (OB_FAIL(ps_session_info_map_.foreach_refactored(fn))) {
      SQL_ENG_LOG(WARN, "failed to read each ps session info", K(ret));
    }
    return ret;
  }
  void set_load_data_exec_session(bool v) { is_load_data_exec_session_ = v; }
  bool is_load_data_exec_session() const { return is_load_data_exec_session_; }
  inline ObSqlString &get_pl_exact_err_msg() { return pl_exact_err_msg_; }
  void set_got_tenant_conn_res(bool v) { got_tenant_conn_res_ = v; }
  bool has_got_tenant_conn_res() const { return got_tenant_conn_res_; }
  void set_got_user_conn_res(bool v) { got_user_conn_res_ = v; }
  bool has_got_user_conn_res() const { return got_user_conn_res_; }
  void set_conn_res_user_id(uint64_t v) { conn_res_user_id_ = v; }
  uint64_t get_conn_res_user_id() const { return conn_res_user_id_; }
  int on_user_connect(share::schema::ObSessionPrivInfo &priv_info, const ObUserInfo *user_info);
  int on_user_disconnect();
  virtual void reset_tx_variable(bool reset_next_scope = true);
  ObOptimizerTraceImpl& get_optimizer_tracer() { return optimizer_tracer_; }
  void set_is_lock_session(bool v) { is_lock_session_ = v; }
  bool is_lock_session() const { return is_lock_session_; }
  int64_t get_plsql_exec_time();
  int64_t get_plsql_compile_time() { return plsql_compile_time_; }
  void update_pure_sql_exec_time(int64_t elapsed_time);
  bool get_failover_mode() const { return failover_mode_; }
  void set_failover_mode(const bool failover_mode) { failover_mode_ = failover_mode; }
  bool has_ccl_rule() const { return has_ccl_rule_; }
  int check_service_name_and_failover_mode() const;
  int64_t get_tx_id_with_thread_data_lock() { 
    ObSQLSessionInfo::LockGuard guard(get_thread_data_lock());
    return tx_desc_ != NULL ? tx_desc_->get_tx_id().get_id() : transaction::ObTransID().get_id();
  }
public:
  bool has_tx_level_temp_table() const { return tx_desc_ && tx_desc_->with_temporary_table(); }
  int close_all_ps_stmt();
  void destory_mem_context();
private:
  void destroy_contexts_map(ObContextsMap &map, common::ObIAllocator &alloc);
  inline int init_mem_context();
  void set_cur_exec_ctx(ObExecContext *cur_exec_ctx) { cur_exec_ctx_ = cur_exec_ctx; }

  static const int64_t MAX_STORED_PLANS_COUNT = 10240;
  static const int64_t MAX_IPADDR_LENGTH = 64;
private:
  bool is_inited_;
  // store the warning message from the most recent statement in the current session
  common::ObWarningBuffer warnings_buf_;
  common::ObWarningBuffer show_warnings_buf_;
  sql::ObEndTransAsyncCallback end_trans_cb_;
  ObAuditRecordData audit_record_;

  ObPrivSet user_priv_set_;
  ObPrivSet db_priv_set_;
  int64_t curr_trans_start_time_;
  int64_t curr_trans_last_stmt_time_;
  int64_t sess_create_time_;  // session creation time, currently only used for temporary table cleanup judgment
  bool has_temp_table_flag_;  // Whether the session has created a temporary table
  bool has_accessed_session_level_temp_table_;  // Whether accessed Session temporary table
  bool enable_early_lock_release_;
  // trigger.
  bool is_for_trigger_package_;
  transaction::ObTxClass trans_type_;
  const common::ObVersionProvider *version_provider_;
  const ObSQLConfigProvider *config_provider_;
  char tenant_buff_[sizeof(share::ObTenantSpaceFetcher)];
  ObPlanCache *plan_cache_;
  ObPsCache *ps_cache_;
  // Record the number of rows scanned in the select stmt result set for use with found_row() when setting sql_calc_found_row;
  int64_t found_rows_;
  // Record affected_row in dml operations, for use by row_count()
  int64_t affected_rows_;
  int64_t global_sessid_;
  bool read_uncommited_; // record whether the current statement reads uncommitted modifications
  common::ObTraceEventRecorder *trace_recorder_;
  // Mark whether there is a write operation in a transaction, used to determine if the commit statement can succeed when setting read_only
  // if has_write_stmt_in_trans_ && read_only => can't not commit
  // else can commit
  // in_transaction_ has been merged into trans_flags_.
//  int64_t has_write_stmt_in_trans_;
  bool inner_flag_; // whether it is a virtual session for an internal request
  // After version 2.2, this variable is no longer used
  bool is_max_availability_mode_;
  typedef common::hash::ObHashMap<ObPsStmtId, ObPsSessionInfo *,
                                  common::hash::SpinReadWriteDefendMode> PsSessionInfoMap;
  PsSessionInfoMap ps_session_info_map_;
  inline int try_create_ps_session_info_map()
  {
    int ret = OB_SUCCESS;
    static const int64_t PS_BUCKET_NUM = 64;
    if (OB_UNLIKELY(!ps_session_info_map_.created())) {
      ret = ps_session_info_map_.create(common::hash::cal_next_prime(PS_BUCKET_NUM),
                                        common::ObModIds::OB_HASH_BUCKET_PS_SESSION_INFO,
                                        common::ObModIds::OB_HASH_NODE_PS_SESSION_INFO);
    }
    return ret;
  }
  common::hash::ObHashSet<ObPsStmtId> in_use_ps_stmt_id_set_;
  inline int try_create_in_use_ps_stmt_id_set()
  {
    int ret = OB_SUCCESS;
    static const int64_t PS_BUCKET_NUM = 64;
    if (OB_UNLIKELY(!in_use_ps_stmt_id_set_.created())) {
      ret = in_use_ps_stmt_id_set_.create(common::hash::cal_next_prime(PS_BUCKET_NUM),
                                   common::ObModIds::OB_HASH_BUCKET_PS_SESSION_INFO,
                                   common::ObModIds::OB_HASH_NODE_PS_SESSION_INFO);
    }
    return ret;
  }

  typedef common::hash::ObHashMap<common::ObString, ObPsStmtId,
                                  common::hash::NoPthreadDefendMode> PsNameIdMap;
  PsNameIdMap ps_name_id_map_;
  inline int try_create_ps_name_id_map()
  {
    int ret = OB_SUCCESS;
    static const int64_t PS_BUCKET_NUM = 64;
    if (OB_UNLIKELY(!ps_name_id_map_.created())) {
      ret = ps_name_id_map_.create(common::hash::cal_next_prime(PS_BUCKET_NUM),
                                   common::ObModIds::OB_HASH_BUCKET_PS_SESSION_INFO,
                                   common::ObModIds::OB_HASH_NODE_PS_SESSION_INFO);
    }
    return ret;
  }

  ObPsStmtId next_client_ps_stmt_id_;
  bool is_remote_session_;//used to record whether the session is created for executing a remote plan
  SessionType session_type_;
  ObPackageStateMap package_state_map_;
  ObSequenceCurrvalMap sequence_currval_map_;
  ObSockFdMap sock_fd_map_;
  ObContextsMap contexts_map_;
  int64_t curr_session_context_size_;

  pl::ObPLContext *pl_context_;
  CursorCache pl_cursor_cache_;
  // if any commit executed, the PL block can not be retried as a whole.
  // otherwise the PL block can be retried in all.
  // if false == pl_can_retry_, we can only retry query in PL blocks locally
  bool pl_can_retry_; // Mark whether the current executing PL can be retried as a whole
  int64_t plsql_exec_time_;
  int64_t plsql_compile_time_;

  uint32_t pl_attach_session_id_; // If the current session has executed dbms_debug.attach_session, record the target session's ID

  observer::ObQueryDriver *pl_query_sender_; // send query result in mysql pl
  bool pl_ps_protocol_; // send query result use this protocol

  common::hash::ObHashSet<common::ObString> *pl_sync_pkg_vars_ = NULL;

  void *inner_conn_;  // ObInnerSQLConnection * will cause .h included from each other.

  ObSessionStat session_stat_;

  common::ObSEArray<uint64_t, 8> enable_role_array_;
  ObTenantCachedSchemaGuardInfo cached_schema_guard_info_;
  bool in_definer_named_proc_;
  uint64_t priv_user_id_;
  // For performance optimization, tenant-level configuration items do not need to be retrieved in real time and are cached in the session, with a refresh triggered every 5s
  ObCachedTenantConfigInfo cached_tenant_config_info_;
  bool prelock_;
  // New engine expression type inference requires using ignore_stmt to determine cast_mode,
  // Due to many interfaces not being able to get the ignore flag from stmt, it can only be passed through the session, so this can only be used during the plan generation phase
  // After CG this status will be cleared
  bool is_ignore_stmt_;
  ObSessionDDLInfo ddl_info_;
  bool is_table_name_hidden_;
  observer::ObPieceCache* piece_cache_;
  bool is_load_data_exec_session_;
  ObSqlString pl_exact_err_msg_;
  bool is_varparams_sql_prepare_;
  // Record whether this session has got connection resource, which means it increased connections count.
  // It's used for on_user_disconnect.
  // No matter whether apply for resource successfully, a session will call on_user_disconnect when disconnect.
  // While only session got connection resource can release connection resource and decrease connections count.
  bool got_tenant_conn_res_;
  bool got_user_conn_res_;
  uint64_t conn_res_user_id_;
  bool tx_level_temp_table_;
  // get_session_allocator can only apply for fixed-length memory.
  // To customize the memory length, you need to use malloc_alloctor of mem_context
  lib::MemoryContext mem_context_;
  ApplicationInfo client_app_info_;
  char module_buf_[common::OB_MAX_MOD_NAME_LENGTH];
  char action_buf_[common::OB_MAX_ACT_NAME_LENGTH];
  char client_info_buf_[common::OB_MAX_CLIENT_INFO_LENGTH];
  bool is_lock_session_ = false;

public:
  inline int64_t get_vid() const { return vid_; }
  inline void set_vid(int64_t vid) { vid_ = vid; }
  inline const common::ObString get_vip() const { return ObString::make_string(vip_buf_);; }
  inline void set_vip(char *vip_buf) { MEMCPY(vip_buf_, vip_buf, sizeof(vip_buf_)); }
  inline int32_t get_vport() const { return vport_; }
  inline void set_vport(int32_t vport) { vport_ = vport; }
  inline int64_t get_in_bytes() const { return ATOMIC_LOAD(&in_bytes_); }
  inline void inc_in_bytes(int64_t in_bytes) { IGNORE_RETURN ATOMIC_FAA(&in_bytes_, in_bytes); }
  inline int64_t get_out_bytes() const { return ATOMIC_LOAD(&out_bytes_); }
  inline void inc_out_bytes(int64_t out_bytes) { IGNORE_RETURN ATOMIC_FAA(&out_bytes_, out_bytes); }
  bool is_pl_prepare_stage() const;
  inline ObExecutingSqlStatRecord& get_executing_sql_stat_record() {return executing_sql_stat_record_; }
  int sql_sess_record_sql_stat_start_value(ObExecutingSqlStatRecord& executing_sqlstat);
  dbms_scheduler::ObDBMSSchedJobInfo *get_job_info() const { return job_info_; }
  void set_job_info(dbms_scheduler::ObDBMSSchedJobInfo *job_info) { job_info_ = job_info; }
private:
  //save the current sql exec context in session
  //and remove the record when the SQL execution ends
  //in order to access exec ctx through session during SQL execution
  ObExecContext *cur_exec_ctx_;
  ObOptimizerTraceImpl optimizer_tracer_;
  int64_t vid_;
  char vip_buf_[MAX_IP_ADDR_LENGTH];
  int32_t vport_;
  int64_t in_bytes_;
  int64_t out_bytes_;
  share::schema::ObUserLoginInfo login_info_;
  dbms_scheduler::ObDBMSSchedJobInfo *job_info_; // dbms_scheduler related.
  memtable::ObBtreeIterCache *btree_iter_cache_;
  bool failover_mode_;
  common::ObString audit_filter_name_;
  ObExecutingSqlStatRecord executing_sql_stat_record_;
  bool has_ccl_rule_;
  int64_t last_update_ccl_cnt_time_;
#ifdef __ANDROID__
  int64_t last_ccl_check_schema_version_;
#endif
};


inline bool ObSQLSessionInfo::is_terminate(int &ret) const
{
  bool bret = false;
  if (QUERY_KILLED == get_session_state()) {
    bret = true;
    SQL_ENG_LOG(WARN, "query interrupted session",
                "query", get_current_query_string(),
                "key", get_server_sid());
    ret = common::OB_ERR_QUERY_INTERRUPTED;
  } else if (QUERY_DEADLOCKED == get_session_state()) {
    bret = true;
    SQL_ENG_LOG(ERROR, "query deadlocked",
                "query", get_current_query_string(),
                "key", get_server_sid());
    ret = common::OB_DEAD_LOCK;
  } else if (SESSION_KILLED == get_session_state()) {
    bret = true;
    ret = common::OB_ERR_SESSION_INTERRUPTED;
  }
  return bret;
}

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_SESSION_OB_SQL_SESSION_INFO_
