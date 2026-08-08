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

#ifndef OCEANBASE_DATA_PLANE_VECTOR_OB_VECTOR_REFRESH_TRANSACTION_H_
#define OCEANBASE_DATA_PLANE_VECTOR_OB_VECTOR_REFRESH_TRANSACTION_H_

#include <stdint.h>

#include "common/mysqlclient/ob_single_connection_proxy.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace data_plane
{

// Transactional inner-SQL session used by vector refresh/rebuild. Observer
// owns the native connection and lock implementation; Storage sees only the
// transaction lifecycle, SQL-client interface, and vector-domain lock intent.
class ObVectorRefreshTransaction final : public common::ObSingleConnectionProxy
{
public:
  ObVectorRefreshTransaction();
  ~ObVectorRefreshTransaction() override;
  DISABLE_COPY_ASSIGN(ObVectorRefreshTransaction);

  int start(sql::ObSQLSessionInfo *session_info,
            common::ObISQLClient *sql_client);
  int end(bool commit);
  int lock_domain_table(uint64_t domain_table_id, bool try_lock = false);
  bool is_started() const { return in_transaction_; }

private:
  class ObSessionParamSaved
  {
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

  int connect_(sql::ObSQLSessionInfo *session_info,
               common::ObISQLClient *sql_client);
  int start_transaction_();
  int end_transaction_(bool commit);

private:
  ObSessionParamSaved session_param_saved_;
  bool in_transaction_;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_VECTOR_OB_VECTOR_REFRESH_TRANSACTION_H_
