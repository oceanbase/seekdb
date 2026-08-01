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

#ifndef OCEANBASE_OBSERVER_OB_INNER_SQL_CONNECTION_H_
#define OCEANBASE_OBSERVER_OB_INNER_SQL_CONNECTION_H_

#include "common/mysqlclient/ob_isql_connection.h"
#include "lib/guard/ob_weak_guard.h"
#include "storage/tx/ob_multi_data_source.h"  // ObRegisterMdsFlag complete type(previously hidden behind the rpc_struct include chain)
#include "lib/container/ob_2d_array.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/resolver/ob_stmt_type.h"
#include "sql/monitor/ob_exec_stat.h"
#include "observer/ob_restore_sql_modifier.h"
#include "observer/mysql/ob_query_retry_ctrl.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "storage/tablelock/ob_table_lock_common.h"   //ObTableLockMode
#include "sql/session/ob_sql_session_mgr.h"
#include "lib/stat/ob_diagnose_info.h"

namespace oceanbase
{
namespace common
{
class ObString;
namespace sqlclient
{
class ObISQLResultHandler;
}
}

namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
class ObSchemaGetterGuard;
}
}
namespace sql
{
class ObSql;
}
namespace transaction
{
enum class ObTxDataSourceType : int64_t;
struct ObRegisterMdsFlag;
namespace tablelock
{
class ObLockRequest;
class ObLockObjRequest;
class ObLockTableRequest;
class ObLockTabletRequest;
class ObLockPartitionRequest;
class ObUnLockObjRequest;
class ObUnLockTableRequest;
class ObUnLockPartitionRequest;
class ObUnLockTabletRequest;
}
}
namespace observer
{
class ObInnerSQLResult;
class ObVTIterCreator;
class ObVirtualTableIteratorFactory;
class ObInnerSQLReadContext;
class ObITimeRecord
{
public:
  virtual int64_t get_send_timestamp() const = 0;
  virtual int64_t get_receive_timestamp() const = 0;
  virtual int64_t get_enqueue_timestamp() const = 0;
  virtual int64_t get_run_timestamp() const = 0;
  virtual int64_t get_process_timestamp() const = 0;
  virtual int64_t get_single_process_timestamp() const = 0;
  virtual int64_t get_exec_start_timestamp() const = 0;
  virtual int64_t get_exec_end_timestamp() const = 0;
};

class ObInnerSQLConnection
    : public common::sqlclient::ObISQLConnection
{
public:
  static constexpr const char LABEL[] = "RPInnerSqlConn";
  class SavedValue
  {
  public:
    SavedValue()
    {
      reset();
    }
    inline void reset()
    {
      ref_ctx_ = NULL;
      execute_start_timestamp_ = 0;
      execute_end_timestamp_ = 0;
    }
  public:
    ObInnerSQLReadContext *ref_ctx_;
    int64_t execute_start_timestamp_;
    int64_t execute_end_timestamp_;
  };

  // Worker and session timeout may be altered in sql execution, restore to origin value after execution.
  class TimeoutGuard
  {
  public:
    TimeoutGuard(ObInnerSQLConnection &conn);
    ~TimeoutGuard();
  private:
    ObInnerSQLConnection &conn_;
    int64_t worker_timeout_;
    int64_t query_timeout_;
    int64_t trx_timeout_;
  };

public:
  class ObSqlQueryExecutor;

  ObInnerSQLConnection();
  virtual ~ObInnerSQLConnection();

  static int create_connection_with_owned_session(
      const bool use_static_engine,
      const int32_t group_id,
      common::sqlclient::ObISQLConnectionGuard &conn);
  static int create_connection_with_external_session(
      sql::ObSQLSessionInfo *session_info,
      common::sqlclient::ObISQLConnectionGuard &conn);
  static int create_spi_connection_with_external_session(
      sql::ObSQLSessionInfo *session_info,
      common::sqlclient::ObISQLConnectionGuard &conn);

  int init(sql::ObSql *ob_sql,
           ObVTIterCreator *vt_iter_creator,
           sql::ObSQLSessionInfo *extern_session = NULL,
           ObRestoreSQLModifier *sql_modifer = NULL,
           const bool use_static_engine = false,
           const int32_t group_id = 0);
  int destroy(void);
  inline void reset()
  {
    destroy();
    // rp_free() calls reset() instead of the destructor.
    self_weak_guard_.reset();
  }
  virtual int execute_read(const ObString &sql,
                           common::ObISQLClient::ReadResult &res, bool is_user_sql = false) override;
  virtual int execute_write(const ObString &sql,
                            int64_t &affected_rows, bool is_user_sql = false) override;
  virtual int execute_proc(ObIAllocator &allocator,
                          ParamStore &params,
                          ObString &sql,
                          const share::schema::ObRoutineInfo &routine_info,
                          const common::ObIArray<const pl::ObUserDefinedType *> &udts,
                          const ObTimeZoneInfo *tz_info,
                          ObObj *result,
                          bool is_sql) override;
  virtual int start_transaction(bool with_snap_shot = false) override;
  virtual int rollback() override;
  virtual int commit() override;
  sql::ObSQLSessionInfo &get_session() { return NULL == extern_session_ ? *inner_session_ : *extern_session_; }
  const sql::ObSQLSessionInfo &get_session() const { return NULL == extern_session_ ? *inner_session_ : *extern_session_; }
  const sql::ObSQLSessionInfo *get_extern_session() const { return extern_session_; }
  // session environment
  virtual int get_session_variable(const ObString &name, int64_t &val) override;
  virtual int set_session_variable(const ObString &name, int64_t val) override;
  virtual int set_session_variable(const ObString &name, const ObString &val) override;
  inline void set_spi_connection(bool is_spi_conn) { is_spi_conn_ = is_spi_conn; }
  int set_primary_schema_version(const common::ObIArray<int64_t> &primary_schema_versions);

  virtual int set_ddl_info(const void *ddl_info);
  virtual int set_tz_info_wrap(const ObTimeZoneInfoWrap &tz_info_wrap);
  virtual void set_is_load_data_exec(bool v);
  virtual void set_ob_enable_pl_cache(bool v) override;
  bool is_nested_conn();
  virtual void set_user_timeout(int64_t timeout) { user_timeout_ = timeout; }
  virtual int64_t get_user_timeout() const { return user_timeout_; }
  int try_acquire_query_lock();
  void try_release_query_lock();

  ObVTIterCreator *get_vt_iter_creator() const { return vt_iter_creator_; }
  ObInnerSQLReadContext *&get_prev_read_ctx() { return ref_ctx_; }
  common::sqlclient::ObISQLConnectionGuard get_shared_guard() const
  {
    return self_weak_guard_.upgrade();
  }
  void dump_conn_bt_info();
public:
  int64_t get_send_timestamp() const { return get_session().get_query_start_time(); }
  int64_t get_receive_timestamp() const { return get_session().get_query_start_time(); }
  int64_t get_enqueue_timestamp() const { return get_session().get_query_start_time(); }
  int64_t get_run_timestamp() const { return get_session().get_query_start_time(); }
  int64_t get_process_timestamp() const { return get_session().get_query_start_time(); }
  int64_t get_single_process_timestamp() const { return get_session().get_query_start_time(); }
  int64_t get_exec_start_timestamp() const { return execute_start_timestamp_; }
  int64_t get_exec_end_timestamp() const { return execute_end_timestamp_; }
  bool is_in_trans() const { return is_in_trans_; }
  void set_is_in_trans(const bool is_in_trans) { is_in_trans_ = is_in_trans; }

public:

  sql::ObSql *get_sql_engine() { return ob_sql_; }

  virtual int execute(sqlclient::ObIExecutor &executor) override;


public:
  // nested session and sql execute for foreign key.
  int begin_nested_session(sql::ObSQLSessionInfo::StmtSavedValue &saved_session,
                           SavedValue &saved_conn, bool skip_cur_stmt_tables);
  int end_nested_session(sql::ObSQLSessionInfo::StmtSavedValue &saved_session,
                         SavedValue &saved_conn);
  bool is_extern_session() const { return NULL != extern_session_; }
  bool is_inner_session() const { return NULL == extern_session_; }
  bool is_spi_conn() const { return is_spi_conn_; }
  // set timeout to session variable
  int set_session_timeout(int64_t query_timeout, int64_t trx_timeout);

public:// for mds
  int register_multi_data_source(
                                 const transaction::ObTxDataSourceType type,
                                 const char *buf,
                                 const int64_t buf_len,
                                 const transaction::ObRegisterMdsFlag &register_flag = transaction::ObRegisterMdsFlag());

public:
  static int process_record(sql::ObResultSet &result_set,
                            sql::ObSqlCtx &sql_ctx,
                            sql::ObSQLSessionInfo &session,
                            ObITimeRecord &time_record,
                            int last_ret,
                            int64_t execution_id,
                            int64_t ps_stmt_id,
                            ObWaitEventDesc &max_wait_desc,
                            ObWaitEventStat &total_wait_desc,
                            sql::ObExecRecord &exec_record,
                            sql::ObExecTimestamp &exec_timestamp,
                            const ObString &ps_sql,
                            bool is_from_pl = false,
                            ObString *pl_exec_params = NULL);
  static int process_audit_record(sql::ObResultSet &result_set,
                                  sql::ObSqlCtx &sql_ctx,
                                  sql::ObSQLSessionInfo &session,
                                  int last_ret,
                                  int64_t execution_id,
                                  int64_t ps_stmt_id,
                                  const ObString &ps_sql,
                                  bool is_from_pl = false);
  static void record_stat(sql::ObSQLSessionInfo &session,
                          const sql::stmt::StmtType type,
                          const int64_t ret,
                          bool is_from_pl = false);

  static int init_session_info(sql::ObSQLSessionInfo *session,
                               const bool is_extern_session,
                               const bool is_ddl);

  int64_t get_init_timestamp() const { return init_timestamp_; }
public:
  static const int64_t LOCK_RETRY_TIME = 1L * 1000 * 1000;
  static const uint32_t INNER_SQL_SESS_ID = 1;
  static const int64_t MAX_BT_SIZE = 20;
  static const int64_t EXTRA_REFRESH_LOCATION_TIME = 1L * 1000 * 1000;
private:
  int init_session(sql::ObSQLSessionInfo* session_info = NULL, const bool is_ddl = false);
  int init_result(ObInnerSQLResult &res,
                  ObVirtualTableIteratorFactory *vt_iter_factory,
                  int64_t retry_cnt,
                  share::schema::ObSchemaGetterGuard &schema_guard,
                  pl::ObPLBlockNS *secondary_namespace,
                  bool is_prepare_protocol = false,
                  bool is_prepare_stage = false,
                  bool is_dynamic_sql = false,
                  bool is_cursor = false);
  int process_retry(ObInnerSQLResult &res,
                    int do_ret,
                    int64_t abs_timeout_us,
                    bool &need_retry,
                    int64_t retry_cnt);
  template <typename T>
  int process_final(const T &sql,
                    ObInnerSQLResult &res,
                    int do_ret);
  // execute with retry
  int query(sqlclient::ObIExecutor &executor,
            ObInnerSQLResult &res,
            ObVirtualTableIteratorFactory *vt_iter_factory = NULL);
  int do_query(sqlclient::ObIExecutor &executor, ObInnerSQLResult &res);

  // set timeout to session variable
  int set_timeout(int64_t &abs_timeout_us);

  int execute_read_inner(const ObString &sql,
                         common::ObISQLClient::ReadResult &res, bool is_user_sql = false);
  int execute_write_inner(const ObString &sql, int64_t &affected_rows,
      bool is_user_sql = false);
  int start_transaction_inner(bool with_snap_shot = false);
  template <typename T>
  int execute_with_timeout(T function);

  int create_session_by_mgr();
  int create_default_session();
  bool is_inner_session_mgr_enable();
  int destroy_inner_session();
  static int create_impl(
                    sql::ObSQLSessionInfo *extern_session,
                    const bool use_static_engine,
                    const int32_t group_id,
                    const bool use_spi_allocator,
                    common::sqlclient::ObISQLConnectionGuard &conn);
  void free_self();
private:
  bool inited_;
  observer::ObQueryRetryCtrl retry_ctrl_;
  sql::ObSQLSessionInfo *extern_session_;   // nested sql and spi both use it, rename to extern.
  sql::ObSQLSessionInfo *inner_session_;
  common::ObWeakGuard<common::sqlclient::ObISQLConnection> self_weak_guard_;
  bool is_spi_conn_;
  sql::ObSql *ob_sql_;
  ObVTIterCreator *vt_iter_creator_;
  ObInnerSQLReadContext *ref_ctx_;
  ObRestoreSQLModifier *sql_modifier_;
  int64_t init_timestamp_;
  int64_t tid_;
  int bt_size_;
  void *bt_addrs_[MAX_BT_SIZE];
  int64_t execute_start_timestamp_;
  int64_t execute_end_timestamp_;

  // The inner SQL connection always executes in the local server runtime.
  bool is_in_trans_;

  // ask the inner sql connection to use external session instead of internal one
  // this enables show session / kill session using sql query command
  bool use_external_session_;
  int32_t group_id_;
  //support set user timeout of stream rpc but not depend on internal_sql_execute_timeout
  int64_t user_timeout_;
  sql::ObFreeSessionCtx free_session_ctx_;
  bool inner_sess_query_locked_;
  DISABLE_COPY_ASSIGN(ObInnerSQLConnection);
};

class ObInnerSqlWaitGuard
{
public:
  explicit ObInnerSqlWaitGuard(const bool is_inner_session,
      sql::ObSQLSessionInfo *inner_session);
  ~ObInnerSqlWaitGuard() = default;
private:
  common::ObWaitEventGuard wait_guard_;
};

class ObInnerSQLSessionGuard
{
public:
  ObInnerSQLSessionGuard(sql::ObSQLSessionInfo *session);
  ~ObInnerSQLSessionGuard();
private:
  sql::ObSQLSessionInfo *last_session_;
};

} // end of namespace observer
} // end of namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_INNER_SQL_CONNECTION_H_
