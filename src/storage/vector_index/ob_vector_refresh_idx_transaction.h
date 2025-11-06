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

namespace oceanbase {
namespace storage {

class ObVectorRefreshIdxTransaction : public common::ObSingleConnectionProxy {
  friend class ObVectorRefreshIdxTxnInnerMySQLGuard;

public:
  ObVectorRefreshIdxTransaction();
  virtual ~ObVectorRefreshIdxTransaction();
  DISABLE_COPY_ASSIGN(ObVectorRefreshIdxTransaction);

  int start(sql::ObSQLSessionInfo *session_info, ObISQLClient *sql_client);
  int end(const bool commit);
  bool is_started() const { return in_trans_; }
  sql::ObSQLSessionInfo *get_session_info() const { return session_info_; }
  ObCompatibilityMode get_compatibility_mode() const {
    return nullptr != session_info_ ? session_info_->get_compatibility_mode()
                                    : ObCompatibilityMode::OCEANBASE_MODE;
  }

protected:
  int connect(sql::ObSQLSessionInfo *session_info, ObISQLClient *sql_client);
  int start_transaction(uint64_t tenant_id);
  int end_transaction(const bool commit);

protected:
  class ObSessionParamSaved {
  public:
    ObSessionParamSaved();
    ~ObSessionParamSaved();
    DISABLE_COPY_ASSIGN(ObSessionParamSaved);

    int save(sql::ObSQLSessionInfo *session_info);
    int restore();

  private:
    sql::ObSQLSessionInfo *session_info_;
    bool is_inner_;
    bool autocommit_;
  };

private:
  sql::ObSQLSessionInfo *session_info_;
  ObSessionParamSaved session_param_saved_;
  int64_t start_time_;
  bool in_trans_;
};

} // namespace storage
} // namespace oceanbase
