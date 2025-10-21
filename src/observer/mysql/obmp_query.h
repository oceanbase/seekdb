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

#ifndef _OBMP_QUERY_H_
#define _OBMP_QUERY_H_

#include "lib/string/ob_string.h"
#include "rpc/obmysql/ob_mysql_packet.h"
#include "sql/resolver/ob_stmt_type.h"
#include "sql/ob_sql_context.h"
#include "observer/mysql/obmp_base.h"
#include "observer/mysql/ob_query_retry_ctrl.h"
#include "observer/mysql/ob_mysql_result_set.h"
#include "observer/mysql/ob_mysql_request_manager.h"
namespace oceanbase
{
namespace sql
{
class ObMonitorInfoManager;
class ObPhyPlanMonitorInfo;
class ObMPParseStat;
}
namespace share
{
namespace schema
{
class ObTableSchema;
}
class ObPartitionLocation;
struct ObFBPartitionParam;
}
namespace observer
{
class ObMPQuery : public ObMPBase
{
public:
  static const obmysql::ObMySQLCmd COM = obmysql::COM_QUERY;

public:
  explicit ObMPQuery(const ObGlobalContext &gctx);
  virtual ~ObMPQuery();

public:
  int64_t get_single_process_timestamp() const { return single_process_timestamp_; }
  int64_t get_exec_start_timestamp() const { return exec_start_timestamp_; }
  int64_t get_exec_end_timestamp() const { return exec_end_timestamp_; }
  int64_t get_send_timestamp() const { return get_receive_timestamp(); }
  void set_is_com_filed_list() { is_com_filed_list_ = true; }
  bool get_is_com_filed_list() const { return is_com_filed_list_; }
protected:
  int process();
  int deserialize();
  int check_readonly_stmt(ObMySQLResultSet &result);
  int is_readonly_stmt(ObMySQLResultSet &result, bool &is_readonly);
  void assign_sql(const char * sql, int64_t sql_length) { sql_.assign_ptr(sql, sql_length); }
private:
  int response_result(ObMySQLResultSet &result, bool force_sync_resp, bool &async_resp_used);
  int get_tenant_schema_info_(const uint64_t tenant_id,
                      ObTenantCachedSchemaGuardInfo *cache_info,
                      share::schema::ObSchemaGetterGuard *&schema_guard,
                      int64_t &tenant_version,
                      int64_t &sys_version);
  int do_process(sql::ObSQLSessionInfo &session,
                 bool has_more_result,
                 bool force_sync_resp,
                 bool &async_resp_used,
                 bool &need_disconnect);
  int do_process_trans_ctrl(sql::ObSQLSessionInfo &session,
                            bool has_more_result,
                            bool force_sync_resp,
                            bool &async_resp_used,
                            bool &need_disconnect,
                            stmt::StmtType stmt_type);
  int process_trans_ctrl_cmd(ObSQLSessionInfo &session,
                             bool &need_disconnect,
                             bool &async_resp_used,
                             const bool is_rollback,
                             const bool force_sync_resp,
                             stmt::StmtType stmt_type);
  int process_with_tmp_context(sql::ObSQLSessionInfo &session,
                    bool has_more_result,
                    bool force_sync_resp,
                    bool &async_resp_used,
                    bool &need_disconnect);
  int process_single_stmt(const sql::ObMultiStmtItem &multi_stmt_item,
                          ObSMConnection *conn,
                          sql::ObSQLSessionInfo &session,
                          bool has_more_result,
                          bool force_sync_resp,
                          bool &async_resp_used,
                          bool &need_disconnect);
  void check_is_trans_ctrl_cmd(const ObString &sql,
                               bool &is_trans_ctrl_cmd,
                               stmt::StmtType &stmt_type);

  void record_stat(const sql::stmt::StmtType type, const int64_t end_time,
                   const sql::ObSQLSessionInfo& session,
                   const int64_t ret,
                   const bool is_commit_cmd,
                   const bool is_rollback_cmd) const;
  void update_audit_info(const ObWaitEventStat &total_wait_desc,
                         ObAuditRecordData &record);
  int fill_feedback_session_info(ObMySQLResultSet &result,
                                 sql::ObSQLSessionInfo &session);
  int build_fb_partition_param(
    const share::schema::ObTableSchema &table_schema,
    const share::ObPartitionLocation &partition_loc,
    share::ObFBPartitionParam &param);
  int try_batched_multi_stmt_optimization(sql::ObSQLSessionInfo &session,
                                          ObSMConnection *conn,
                                          common::ObIArray<ObString> &queries,
                                          const ObMPParseStat &parse_stat,
                                          bool &optimization_done,
                                          bool &async_resp_used,
                                          bool &need_disconnect,
                                          bool is_ins_multi_val_opt);
  int deserialize_com_field_list();
  int store_params_value_to_str(ObIAllocator &allocator,
                                sql::ObSQLSessionInfo &session,
                                common::ParamStore &params);
public:
  static const int64_t MAX_SELF_OBJ_SIZE = 2.5 * 1024L;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMPQuery);
private:
  //Lifecycle in process_single_stmt()
  sql::ObSqlCtx ctx_;
  ObQueryRetryCtrl retry_ctrl_;
  common::ObString sql_;
  int64_t single_process_timestamp_;
  int64_t exec_start_timestamp_;
  int64_t exec_end_timestamp_;
  //Since the MySQL COM_FIELD_LIST command essentially retrieves column definition information, only the column definitions need to be returned
  bool is_com_filed_list_;
  common::ObString wild_str_;//used to save wildware string in COM_FIELD_LIST
  int64_t params_value_len_;
  char *params_value_;
}; // end of class ObMPQuery
STATIC_ASSERT(sizeof(ObMPQuery) < ObMPQuery::MAX_SELF_OBJ_SIZE, "sizeof ObMPQuery not great to 2KB");
} // end of namespace observer
} // end of namespace oceanbase
#endif /* _OBMP_QUERY_H_ */
