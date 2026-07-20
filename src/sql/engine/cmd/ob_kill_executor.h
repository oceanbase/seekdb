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

#ifndef OCEANBASE_SQL_ENGINE_CMD_OB_KILL_EXECUTOR_H__
#define OCEANBASE_SQL_ENGINE_CMD_OB_KILL_EXECUTOR_H__
namespace oceanbase
{
namespace common
{
class ObSqlString;
class ObAddr;
}
namespace sql
{
class ObExecContext;
class ObKillStmt;
class ObSQLSessionInfo;
class ObSQLSessionMgr;
class ObKillSessionArg;

class ObKillSession
{
public:
  ObKillSession() {}
  virtual ~ObKillSession() {}
protected:
  int kill_session(const ObKillSessionArg &arg, ObSQLSessionMgr &sess_mgr);
private:
  DISALLOW_COPY_AND_ASSIGN(ObKillSession);
};

class ObKillExecutor : public ObKillSession
{
public:
  ObKillExecutor() {}
  virtual ~ObKillExecutor() {}
  int execute(ObExecContext &ctx, ObKillStmt &stmt);
private:
  int get_remote_session_location(const ObKillSessionArg &arg, ObExecContext &ctx, common::ObAddr &addr);
  int generate_read_sql(uint32_t sess_id, common::ObSqlString &sql);
  int generate_read_sql_from_session_info(uint32_t sess_id, common::ObSqlString &sql);
  int kill_remote_session(ObExecContext &ctx, const common::ObAddr &addr, const ObKillSessionArg &arg);
  DISALLOW_COPY_AND_ASSIGN(ObKillExecutor);
};
}
}
#endif /* OCEANBASE_SQL_ENGINE_CMD_OB_KILL_EXECUTOR_H__ */
//// end of header file
