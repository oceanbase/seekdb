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

#pragma once

#include "lib/mysqlclient/ob_single_connection_proxy.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace storage
{
class ObMViewTransaction : public common::ObSingleConnectionProxy
{
  friend class ObMViewTransactionInnerMySQLGuard;

public:
  ObMViewTransaction();
  virtual ~ObMViewTransaction();
  DISABLE_COPY_ASSIGN(ObMViewTransaction);

  int start(sql::ObSQLSessionInfo *session_info,
            ObISQLClient *sql_client,
            const uint64_t database_id,
            const ObString &database_name);
  int end(const bool commit);
  bool is_started() const { return in_trans_; }
  sql::ObSQLSessionInfo *get_session_info() const { return session_info_; }
  ObCompatibilityMode get_compatibility_mode() const
  {
    return nullptr != session_info_ ? session_info_->get_compatibility_mode()
                                    : ObCompatibilityMode::OCEANBASE_MODE;
  }
  bool is_inner_session() const { return session_param_saved_.is_inner_session(); }
  sql::ObSQLSessionInfo *get_session_info() { return session_info_; }

protected:
  int connect(sql::ObSQLSessionInfo *session_info, ObISQLClient *sql_client);
  int start_transaction(uint64_t tenant_id);
  int end_transaction(const bool commit);

  int save_session_for_inner();
  int restore_session_for_inner();
  int set_compact_mode(ObCompatibilityMode compact_mode);

protected:
  class ObSessionParamSaved
  {
  public:
    ObSessionParamSaved();
    ~ObSessionParamSaved();
    DISABLE_COPY_ASSIGN(ObSessionParamSaved);

    int save(sql::ObSQLSessionInfo *session_info);
    int restore();

    bool is_inner_session() const { return is_inner_; }

  private:
    ObArenaAllocator allocator_;
    sql::ObSQLSessionInfo *session_info_;
    bool is_inner_;
    bool autocommit_;
    uint64_t database_id_;
    char *database_name_;
    ObObj collation_connection_var_;
  };

  class ObSessionSavedForInner
  {
  public:
    ObSessionSavedForInner();
    ~ObSessionSavedForInner();
    DISABLE_COPY_ASSIGN(ObSessionSavedForInner);

    int save(sql::ObSQLSessionInfo *session_info);
    int restore();

  private:
    ObArenaAllocator allocator_;
    sql::ObSQLSessionInfo *session_info_;
    sql::ObSQLSessionInfo::StmtSavedValue *session_saved_value_;
    uint64_t database_id_;
    char *database_name_;
  };

protected:
  sql::ObSQLSessionInfo *session_info_;
  ObSessionParamSaved session_param_saved_;
  ObSessionSavedForInner session_saved_for_inner_;
  int64_t start_time_;
  bool in_trans_;
};

class ObMViewTransactionInnerMySQLGuard
{
public:
  ObMViewTransactionInnerMySQLGuard(ObMViewTransaction &trans);
  ~ObMViewTransactionInnerMySQLGuard();
  DISABLE_COPY_ASSIGN(ObMViewTransactionInnerMySQLGuard);

  int get_error_ret() const { return error_ret_; }
  bool is_first_loop() const { return first_loop_; }
  void set_first_loop(bool flag) { first_loop_ = flag; }

private:
  ObMViewTransaction &trans_;
  ObCompatibilityMode old_compact_mode_;
  int error_ret_;
  bool need_reset_;
  bool first_loop_;
};

#define WITH_MVIEW_TRANS_INNER_MYSQL_GUARD(trans)                               \
  for (ObMViewTransactionInnerMySQLGuard guard(trans);                          \
       OB_SUCC(ret) && OB_SUCC(guard.get_error_ret()) && guard.is_first_loop(); \
       guard.set_first_loop(false))

} // namespace storage
} // namespace oceanbase
