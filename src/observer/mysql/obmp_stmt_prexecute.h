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

#ifndef OCEANBASE_OBSERVER_MYSQL_OBMP_STMT_PREXECUTE_H_
#define OCEANBASE_OBSERVER_MYSQL_OBMP_STMT_PREXECUTE_H_


#include "lib/container/ob_2d_array.h"
#include "sql/ob_sql_context.h"
#include "observer/mysql/obmp_base.h"
#include "observer/mysql/ob_query_retry_ctrl.h"
#include "sql/plan_cache/ob_prepare_stmt_struct.h"
#include "observer/mysql/obmp_stmt_execute.h"

namespace oceanbase
{
namespace observer
{

#define OB_OCI_EXEC_DEFAULT              0
#define OB_OCI_DESCRIBE_ONLY             0x00000001
#define OB_OCI_EXACT_FETCH               0x00000002
#define OB_OCI_STMT_SCROLLABLE_READONLY  0x00000008
#define OB_OCI_COMMIT_ON_SUCCESS         0x00000020
#define OB_OCI_BATCH_ERRORS              0x00000080
#define OB_OCI_PARSE_ONLY                0x00000100

#define SEND_LONG_DATA                1

class ObMPStmtPrexecute : public ObMPStmtExecute
{
public:
  static const obmysql::ObMySQLCmd COM = obmysql::COM_STMT_PREXECUTE;

  explicit ObMPStmtPrexecute(const ObGlobalContext &gctx);
  virtual ~ObMPStmtPrexecute() {}

  int send_prepare_packet(const ObMySQLResultSet &result,
                          int8_t has_result_set = 0,
                          int8_t has_pl_var_info = 0);
  int send_column_packet(sql::ObSQLSessionInfo &session,
                          const ColumnsFieldIArray *fields,
                          bool ps_out);
  //int send_pl_var_info();
  int send_param_field_packet(sql::ObSQLSessionInfo &session, const ParamsFieldIArray *input_params);
  int send_param_packet(sql::ObSQLSessionInfo &session, ParamStore *params);
  int send_eof_packet(sql::ObSQLSessionInfo &session,
                      uint16_t warning_count,
                      bool has_result,
                      bool cursor_exist,
                      bool last_row,
                      bool ps_out = false);
  int execute_response(sql::ObSQLSessionInfo &session,
                        ParamStore &params,
                        sql::ObSqlCtx &ctx,
                        ObMySQLResultSet &result,
                        ObQueryRetryCtrl &retry_ctrl,
                        const bool enable_perf_event,
                        bool &need_response_error,
                        bool &is_diagnostics_stmt,
                        int64_t &execution_id,
                        const bool force_sync_resp,
                        bool &async_resp_used,
                        ObPsStmtId &inner_stmt_id);
  int response_query_header(sql::ObSQLSessionInfo &session,
                            sql::ObResultSet &result,
                            bool need_flush_buffer = false);
  int response_param_query_header(sql::ObSQLSessionInfo &session,
                                const ColumnsFieldIArray *fields,
                                ParamStore *params,
                                int64_t stmt_id,
                                int8_t has_result,
                                int64_t warning_count = 0,
                                bool ps_out = false);
  inline int32_t get_iteration_count() { return iteration_count_; }
  virtual bool is_send_long_data() { return SEND_LONG_DATA & extend_flag_;}
  inline ObIAllocator *get_alloc() { return allocator_;}
  int clean_ps_stmt(sql::ObSQLSessionInfo &session, const bool is_local_retry, const bool is_batch);

protected:
  virtual int deserialize()  { return common::OB_SUCCESS; }
  int send_ok_packet(sql::ObSQLSessionInfo &session,
                     uint64_t affected_rows,
                     bool is_partition_hit,
                     bool has_more_result,
                     bool cursor_exist,
                     bool send_last_row);
  virtual int send_error_packet(int err,
                                const char* errmsg,
                                bool is_partition_hit = true,
                                void *extra_err_info = NULL)
  { return ObMPBase::send_error_packet(err, errmsg, is_partition_hit, extra_err_info); }
  virtual int send_ok_packet(sql::ObSQLSessionInfo &session, ObOKPParam &ok_param)
  { return ObMPBase::send_ok_packet(session, ok_param); }
  virtual int send_eof_packet(const sql::ObSQLSessionInfo &session, const ObMySQLResultSet &result)
  { return ObMPBase::send_eof_packet(session, result); }
  virtual bool need_send_extra_ok_packet()
  { return OB_NOT_NULL(get_conn()) && get_conn()->need_send_extra_ok_packet(); }
  virtual int response_packet(obmysql::ObMySQLPacket &pkt, sql::ObSQLSessionInfo* session)
  { return ObMPBase::response_packet(pkt, session); }
  int send_prepare_packet(uint32_t statement_id,
                          uint16_t column_num,
                          uint16_t param_num,
                          uint16_t warning_count,
                          int8_t has_result_set,
                          bool is_returning_into,
                          bool has_ps_out);

  int after_do_process_for_arraybinding(sql::ObSQLSessionInfo &session,
                                        ObMySQLResultSet &result);
  bool need_response_pkg_when_error_occur();
  int response_header_for_arraybinding(sql::ObSQLSessionInfo &session, ObMySQLResultSet &result);
  int response_arraybinding_result(sql::ObSQLSessionInfo &session, ObMySQLResultSet &result);
  int response_returning_rows(sql::ObSQLSessionInfo &session,
                              ObMySQLResultSet &result);
  int response_arraybinding_rows(sql::ObSQLSessionInfo &session,
                                 int64_t affect_rows);
  int response_fail_result(sql::ObSQLSessionInfo &session, int err_ret);

  inline bool is_arraybinding_has_result_type(sql::stmt::StmtType stmt_type) { 
    return sql::ObDMLStmt::is_dml_write_stmt(stmt_type) 
            || sql::stmt::T_ANONYMOUS_BLOCK == stmt_type
            || sql::stmt::T_CALL_PROCEDURE == stmt_type;
  }

  virtual void set_proxy_version(uint64_t v)
  { ObMPBase::set_proxy_version(v); }
  virtual uint64_t get_proxy_version()
  { return ObMPBase::get_proxy_version(); }
  virtual bool is_prexecute() const { return true; }

private:
  virtual int before_process();

private:
  common::ObString sql_;
  uint64_t sql_len_;
  /*
   * the meaning of iteration_count_
   *  1. DML statement + iteration_count_ > 1 indicates that the current mode is arraybinding mode
   *  2. in arraybinding mode, this value represents the size of the array
   *  3. in exact_fetch + select mode, this value represents the size of the result set returned
   *  4. in other scenarios, this value > 0 indicates that a result set needs to be returned
   **/ 
  int32_t iteration_count_;
  uint32_t exec_mode_;
  uint32_t close_stmt_count_;
  uint32_t extend_flag_;
  bool first_time_;
  bool is_commit_on_success_;
  ObIAllocator *allocator_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObMPStmtPrexecute);

}; //end of class

} //end of namespace observer
} //end of namespace oceanbase

#endif //OCEANBASE_OBSERVER_MYSQL_OBMP_STMT_PREXECUTE_H__
