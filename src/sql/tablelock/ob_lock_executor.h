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

#ifndef OCEANBASE_OB_LOCK_EXECUTOR_H_
#define OCEANBASE_OB_LOCK_EXECUTOR_H_

#include "sql/session/ob_basic_session_info.h"
#include "data_plane/tablelock/ob_session_table_lock.h"
#include "common/mysqlclient/ob_isql_connection.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
class ObExecContext;
}

namespace common
{
class ObISQLClient;
class ObMySQLProxy;
namespace sqlclient
{
}
}

namespace transaction
{
class ObTransID;
class ObTxDesc;
struct ObTxParam;

namespace tablelock
{
class ObLockContext
{
public:
  ObLockContext() { reset(); }
  virtual ~ObLockContext() { reset(); }
  void reset()
  {
    reset_autocommit_ = false;
    has_inner_dml_write_ = false;
    have_saved_session_ = false;
    has_inner_tx_ = false;
    old_worker_timeout_ts_ = 0;
    old_phy_plan_timeout_ts_ = 0;
    last_insert_id_ = 0;
    database_id_ = OB_INVALID_ID;
    database_name_.reset();
    inner_conn_ = nullptr;
    store_inner_conn_ = nullptr;
    inner_conn_guard_.reset();
    my_exec_ctx_ = nullptr;
    saved_session_.reset();
  }

  int init(sql::ObExecContext &ctx,
           const int64_t timeout_us = 0);
  int destroy(sql::ObExecContext &ctx,
              bool is_rollback);
  bool is_inited() { return my_exec_ctx_ != NULL; }

  static int valid_execute_context(sql::ObExecContext &ctx);
  int execute_write(const ObSqlString &sql,
                    int64_t &affected_rows);
  int execute_read(const ObSqlString &sql,
                   common::ObMySQLProxy::MySQLResult &res);
private:
  int open_inner_conn_();
  int close_inner_conn_();
  static void register_for_deadlock_(sql::ObSQLSessionInfo &session_info,
                                     const transaction::ObTransID &parent_tx_id);
  static int implicit_end_trans_(sql::ObSQLSessionInfo &session,
                                 sql::ObExecContext &ctx,
                                 bool is_rollback,
                                 bool can_async = false);
private:
  friend class ObLockExecutor;
  bool reset_autocommit_;
  bool has_inner_dml_write_;
  bool have_saved_session_;
  bool has_inner_tx_;
  int64_t old_worker_timeout_ts_;
  int64_t old_phy_plan_timeout_ts_;
  uint64_t last_insert_id_;
  uint64_t database_id_;
  ObSqlString database_name_;
  common::sqlclient::ObISQLConnection *inner_conn_;
  common::sqlclient::ObISQLConnection *store_inner_conn_;
  common::sqlclient::ObISQLConnectionGuard inner_conn_guard_;
  sql::ObExecContext *my_exec_ctx_; //my exec context
  sql::ObBasicSessionInfo::TransSavedValue saved_session_;
};

class ObLockExecutor
{
public:
  static constexpr int64_t MAX_FULL_TABLE_NAME_LENGTH = (OB_MAX_TABLE_NAME_LENGTH
                                                         + OB_MAX_DATABASE_NAME_LENGTH
                                                         + 1);
  static constexpr int64_t MAX_LOCK_HANDLE_LEGNTH = 65;
  static constexpr int64_t WHERE_CONDITION_BUFFER_SIZE = 512;
  static constexpr int64_t LOCK_ID_LENGTH = 10;
  static constexpr int64_t MIN_LOCK_HANDLE_ID = 0x40000000;
  static constexpr int64_t MAX_LOCK_HANDLE_ID = 1999999999;
  static constexpr int64_t DEFAULT_EXPIRATION_US = 60 * 1000 * 1000L; // 1min

  static constexpr int64_t INVALID_RELEASE_CNT = -2;
  static constexpr int64_t LOCK_NOT_EXIST_RELEASE_CNT = -1;
  static constexpr int64_t LOCK_NOT_OWN_RELEASE_CNT = 0;

public:
  int remove_expired_lock_id();
  int clear_lock_session_if_no_lock_(ObLockContext &ctx,
                                     const uint32_t session_id,
                                     const uint64_t session_create_ts);

protected:
  int query_lock_id_(common::ObISQLClient &sql_client,
                     const ObString &lock_name,
                     uint64_t &lock_id);
  int query_lock_id_and_lock_handle_(common::ObISQLClient &sql_client,
                                     const ObString &lock_name,
                                     uint64_t &lock_id,
                                     char *lock_handle_buf);
  int extract_lock_id_(const ObString &lock_handle,
                       uint64_t &lock_id);
  void mark_lock_session_(sql::ObSQLSessionInfo *session,
                          const bool is_lock_session);
};

class ObUnLockExecutor : public ObLockExecutor
{
public:
  // new one
  enum ReleaseType
  {
    RELEASE_OBJ_LOCK,
    RELEASE_TABLE_LOCK,
    RELEASE_ALL_LOCKS,
  };
  int execute(sql::ObExecContext &ctx,
              const ReleaseType release_type,
              int64_t &release_cnt);
  // used internal, release all the lock that required by the session.
  int execute(uint8_t owner_type, int64_t owner_id);
private:
  int execute_(sql::ObExecContext &ctx,
               const data_plane::ObPersistedLockOwner &owner,
               int64_t &release_cnt);
  int execute_(sql::ObExecContext &ctx,
               const uint32_t session_id,
               const uint64_t session_create_ts,
               const ReleaseType release_type,
               int64_t &release_cnt);
  static data_plane::ObSessionLockScope to_scope_(ReleaseType release_type);
};
} // tablelock
} // transaction
} // oceanbase
#endif
