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

#include <fstream>
#include <iterator>

#include <gtest/gtest.h>

#include "lib/allocator/page_arena.h"
#include "lib/worker.h"
#include "observer/ob_server.h"
#include "rootserver/ob_local_management_service.h"
#include "share/ob_server_struct.h"
#include "sql/engine/cmd/ob_table_executor.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/ob_sql_context.h"
#include "sql/resolver/ddl/ob_fork_table_stmt.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{
namespace
{

class MockForkTableLocalManagementService : public rootserver::ObLocalManagementService
{
public:
  int fork_table_call_count_ = 0;
  int fork_table_ret_ = OB_SUCCESS;
  int64_t async_task_id_ = 12345;

  int fork_table(const obcall::ObForkTableArg &arg, obcall::ObDDLRes &res) override
  {
    UNUSED(arg);
    ++fork_table_call_count_;
    res.task_id_ = async_task_id_;
    res.schema_id_ = 42;
    return fork_table_ret_;
  }
};

class TestObForkTableExecutor : public ::testing::Test
{
protected:
  void SetUp() override
  {
    OBSERVER.init_schema();
    OBSERVER.init_tz_info_mgr();
    ASSERT_EQ(OB_SUCCESS, ObPreProcessSysVars::init_sys_var());
    ASSERT_EQ(OB_SUCCESS, session_.test_init(0, 0, &allocator_));
    ASSERT_EQ(OB_SUCCESS, session_.init_runtime(ObString::make_string("sys")));
    exec_ctx_.set_my_session(&session_);
    THIS_WORKER.set_timeout_ts(ObTimeUtility::current_time() + 600L * 1000L * 1000L);

    saved_local_management_service_ = GCTX.local_management_service_;
    saved_sql_proxy_ = GCTX.sql_proxy_;
    GCTX.local_management_service_ = &mock_lms_;
    GCTX.sql_proxy_ = nullptr;
  }

  void TearDown() override
  {
    GCTX.local_management_service_ = saved_local_management_service_;
    GCTX.sql_proxy_ = saved_sql_proxy_;
  }

  int init_fork_table_stmt(ObForkTableStmt &stmt, const char *sql)
  {
    int ret = OB_SUCCESS;
    const char *src_db = "test_db";
    const char *src_table = "src_tbl";
    const char *dst_table = "dst_tbl";
    obcall::ObForkTableArg &arg = stmt.get_fork_table_arg();
    query_ctx_.set_sql_stmt(sql, static_cast<int32_t>(strlen(sql)));
    query_ctx_.set_sql_stmt_coll_type(CS_TYPE_UTF8MB4_BIN);
    stmt.set_query_ctx(&query_ctx_);
    arg.src_database_name_.assign_ptr(src_db, static_cast<int32_t>(strlen(src_db)));
    arg.src_table_name_.assign_ptr(src_table, static_cast<int32_t>(strlen(src_table)));
    arg.dst_database_name_.assign_ptr(src_db, static_cast<int32_t>(strlen(src_db)));
    arg.dst_table_name_.assign_ptr(dst_table, static_cast<int32_t>(strlen(dst_table)));
    if (!arg.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    }
    return ret;
  }

  common::ObArenaAllocator allocator_;
  ObSQLSessionInfo session_;
  ObExecContext exec_ctx_{allocator_};
  ObQueryCtx query_ctx_;
  MockForkTableLocalManagementService mock_lms_;
  rootserver::ObLocalManagementService *saved_local_management_service_ = nullptr;
  common::ObMySQLProxy *saved_sql_proxy_ = nullptr;
};

TEST_F(TestObForkTableExecutor, returns_unexpected_when_session_missing)
{
  ObForkTableStmt stmt;
  ObExecContext ctx_without_session(allocator_);
  ObForkTableExecutor executor;
  ASSERT_EQ(OB_ERR_UNEXPECTED, executor.execute(ctx_without_session, stmt));
}

TEST_F(TestObForkTableExecutor, dispatches_fork_table_without_waiting_ddl_finish)
{
  ObForkTableStmt stmt;
  ObForkTableExecutor executor;
  const char *sql = "fork table test_db.src_tbl to test_db.dst_tbl";
  ASSERT_EQ(OB_SUCCESS, init_fork_table_stmt(stmt, sql));
  ASSERT_EQ(OB_SUCCESS, executor.execute(exec_ctx_, stmt));
  EXPECT_EQ(1, mock_lms_.fork_table_call_count_);
  EXPECT_EQ(ObString::make_string(sql), stmt.get_fork_table_arg().ddl_stmt_str_);
  EXPECT_EQ(session_.get_sessid_for_table(), stmt.get_fork_table_arg().session_id_);
}

TEST_F(TestObForkTableExecutor, propagates_fork_table_failure)
{
  ObForkTableStmt stmt;
  ObForkTableExecutor executor;
  mock_lms_.fork_table_ret_ = OB_ERR_UNEXPECTED;
  ASSERT_EQ(OB_SUCCESS, init_fork_table_stmt(stmt, "fork table test_db.src_tbl to test_db.dst_tbl"));
  ASSERT_EQ(OB_ERR_UNEXPECTED, executor.execute(exec_ctx_, stmt));
  EXPECT_EQ(1, mock_lms_.fork_table_call_count_);
}

TEST(TestObForkTableExecutorPolicy, uses_master_style_dispatch_without_embed_fork)
{
#ifndef SEEKDB_SOURCE_DIR
#define SEEKDB_SOURCE_DIR "."
#endif
  const char *path = SEEKDB_SOURCE_DIR "/src/sql/engine/cmd/ob_table_executor.cpp";
  std::ifstream in(path);
  ASSERT_TRUE(in.good()) << "failed to open " << path;
  const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const size_t begin = content.find("int ObForkTableExecutor::execute");
  ASSERT_NE(std::string::npos, begin);
  const size_t end = content.find("int ObRecyclebinRestoreTableExecutor::execute", begin);
  ASSERT_NE(std::string::npos, end);
  const std::string body = content.substr(begin, end - begin);
  EXPECT_EQ(std::string::npos, body.find("#ifdef OB_BUILD_EMBED_MODE"));
  EXPECT_EQ(std::string::npos, body.find("wait_ddl_finish"));
  EXPECT_NE(std::string::npos, body.find("GET_SQL_EXECUTOR_CTX"));
  EXPECT_NE(std::string::npos, body.find("local_ddl_serial_call"));
}

} // namespace
} // namespace sql
} // namespace oceanbase
