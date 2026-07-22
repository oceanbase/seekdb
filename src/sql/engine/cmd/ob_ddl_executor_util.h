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

#ifndef OCEANBASE_SQL_OB_DDL_EXECUTOR_UTIL_
#define OCEANBASE_SQL_OB_DDL_EXECUTOR_UTIL_
#include "lib/utility/ob_tracepoint.h"
#include "observer/ob_server.h"
#include "share/ob_ddl_error_message_table_operator.h"
#include "sql/session/ob_sql_session_info.h"
namespace oceanbase
{
namespace share
{
namespace schema
{
struct ObPartition;
struct ObSubPartition;
struct ObBasePartition;
class ObMultiVersionSchemaService;
}
}
namespace obcall
{
struct ObAlterTableArg;
struct ObCreateTableArg;
struct ObCreateTableRes;
}
namespace common
{
class ObIAllocator;
struct ObExprCtx;
class ObNewRow;
class ObMySQLProxy;
}
namespace sql
{
class ObExecContext;
class ObRawExpr;
class ObCreateTableStmt;
class ObTableStmt;

class ObDDLExecutorUtil final
{
public:
  static int wait_local_schema_visible(
      const ObTimeoutCtx &ctx,
      ObSQLSessionInfo *session,
      const int64_t schema_version);
  ObDDLExecutorUtil() {}
  virtual ~ObDDLExecutorUtil() {}
  static int wait_ddl_finish(const int64_t task_id,
      const bool ddl_need_retry_at_executor,
      ObSQLSessionInfo *session,
      const bool is_support_cancel = true);
  static int wait_ddl_retry_task_finish(const int64_t task_id,
      ObSQLSessionInfo &session,
      int64_t &affected_rows);
  static int wait_build_index_finish(const int64_t task_id, bool &is_finish);
  static int wait_ddl_task_to_status(
      const int64_t task_id,
      const share::ObDDLTaskStatus target_status,
      ObSQLSessionInfo *session,
      bool &is_reached);
  static int handle_session_exception(ObSQLSessionInfo &session);
  static int cancel_ddl_task();
  static int execute_pcreate_table(ObSQLSessionInfo *my_session, const char* parallel_ddl_type,
                                  const obcall::ObCreateTableArg &arg, obcall::ObCreateTableRes &res);
private:
  static inline bool is_server_stopped() { return observer::ObServer::get_instance().is_stopped(); }
private:
  DISALLOW_COPY_AND_ASSIGN(ObDDLExecutorUtil);
};

} //end namespace sql
} //end namespace oceanbase


#endif //OCEANBASE_SQL_OB_DDL_EXECUTOR_UTIL_
