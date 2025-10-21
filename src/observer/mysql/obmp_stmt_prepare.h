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

#ifndef OCEANBASE_OBSERVER_MYSQL_OBMP_STMT_PREPARE_H_
#define OCEANBASE_OBSERVER_MYSQL_OBMP_STMT_PREPARE_H_

#include "sql/ob_sql_context.h"
#include "observer/mysql/obmp_base.h"
#include "observer/mysql/ob_query_retry_ctrl.h"

namespace oceanbase
{
namespace sql
{
class ObMultiStmtItem;
}
namespace observer
{

class ObMPStmtPrepare : public ObMPBase
{
public:
  static const obmysql::ObMySQLCmd COM = obmysql::COM_STMT_PREPARE;

  explicit ObMPStmtPrepare(const ObGlobalContext &gctx);
  virtual ~ObMPStmtPrepare() {}
  int64_t get_single_process_timestamp() const { return single_process_timestamp_; }
  int64_t get_exec_start_timestamp() const { return exec_start_timestamp_; }
  int64_t get_exec_end_timestamp() const { return exec_end_timestamp_; }
  int64_t get_send_timestamp() const { return get_receive_timestamp(); }
  static int multiple_query_check(sql::ObSQLSessionInfo &session,
                                  ObString &sql,
                                  bool &force_sync_resp,
                                  bool &need_response_error);
protected:
  virtual int deserialize();
  virtual int before_process() override;
  virtual int process();

private:
  int do_process(sql::ObSQLSessionInfo &session,
                 const bool has_more_result,
                 const bool force_sync_resp,
                 bool &async_resp_used);
  int process_prepare_stmt(const sql::ObMultiStmtItem &multi_stmt_item,
                           sql::ObSQLSessionInfo &session,
                           bool has_more_result,
                           bool fore_sync_resp,
                           bool &async_resp_used);
  int check_and_refresh_schema(uint64_t login_tenant_id,
                               uint64_t effective_tenant_id);
  int response_result(ObMySQLResultSet &result,
                      sql::ObSQLSessionInfo &session,
                      bool force_sync_resp,
                      bool &async_resp_used);

  int send_prepare_packet(const ObMySQLResultSet &result);
  int send_column_packet(const sql::ObSQLSessionInfo &session, ObMySQLResultSet &result);
  int send_param_packet(const sql::ObSQLSessionInfo &session, ObMySQLResultSet &result);

private:
  ObQueryRetryCtrl retry_ctrl_;
  sql::ObSqlCtx ctx_;
  common::ObString sql_;
  int64_t sql_len_;
  int64_t single_process_timestamp_;
  int64_t exec_start_timestamp_;
  int64_t exec_end_timestamp_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObMPStmtPrepare);


}; // end of class ObMPStmtPrepare

} // end of namespace observer
} // end of namespace oceanbase

#endif //OCEANBASE_OBSERVER_MYSQL_OBMP_STMT_PREPARE_H_
