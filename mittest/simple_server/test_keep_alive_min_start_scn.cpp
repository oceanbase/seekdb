// owner: gengli.wzy
// owner group: transaction

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

#include <gtest/gtest.h>
#define USING_LOG_PREFIX STORAGE
#define protected public
#define private public

#include "env/ob_simple_cluster_test_base.h"
#include "storage/tx/ob_tx_loop_worker.h"

namespace oceanbase
{

namespace storage
{
int64_t ObTxTable::UPDATE_MIN_START_SCN_INTERVAL = 0;
}

namespace unittest
{

using namespace oceanbase::transaction;
using namespace oceanbase::storage;



class TestRunCtx
{
public:
  uint64_t tenant_id_ = 0;
  int time_sec_ = 0;
};

TestRunCtx RunCtx;

class ObTestKeepAliveMinStartSCN : public ObSimpleClusterTestBase
{
public:
  ObTestKeepAliveMinStartSCN() : ObSimpleClusterTestBase("test_keep_alive_min_start_scn_") {}

  void test_min_start_scn();
  void loop_check_start_scn(SCN &prev_min_start_scn, SCN &prev_keep_alive_scn);

  ObLS *get_ls(const int64_t tenant_id, const ObLSID ls_id)
  {
    int ret = OB_SUCCESS;
    ObLS *ls = nullptr;
    MTL_SWITCH(tenant_id)
    {
      ObLSIterator *ls_iter = nullptr;
      ObLSHandle ls_handle;
      ObLSService *ls_svr = MTL(ObLSService *);
      OB_ASSERT(OB_SUCCESS == ls_svr->get_ls(ls_id, ls_handle, ObLSGetMod::STORAGE_MOD));
      OB_ASSERT(nullptr != (ls = ls_handle.get_ls()));
    }
    return ls;
  }
};

#define WRITE_SQL_BY_CONN(conn, sql_str)            \
  ASSERT_EQ(OB_SUCCESS, sql.assign(sql_str)); \
  ASSERT_EQ(OB_SUCCESS, conn->execute_write(OB_SYS_TENANT_ID, sql.ptr(), affected_rows));

#define WRITE_SQL_FMT_BY_CONN(conn, ...)                    \
  ASSERT_EQ(OB_SUCCESS, sql.assign_fmt(__VA_ARGS__)); \
  ASSERT_EQ(OB_SUCCESS, conn->execute_write(OB_SYS_TENANT_ID, sql.ptr(), affected_rows));

#define DEF_VAL_FOR_SQL      \
  int ret = OB_SUCCESS;      \
  ObSqlString sql;           \
  int64_t affected_rows = 0; \
  sqlclient::ObISQLConnection *connection = nullptr;

void ObTestKeepAliveMinStartSCN::loop_check_start_scn(SCN &prev_min_start_scn, SCN &prev_keep_alive_scn)
{
  int ret = OB_SUCCESS;
  MTL_SWITCH(RunCtx.tenant_id_)
  {
    ObLS *ls = get_ls(RunCtx.tenant_id_, ObLSID(1001));
    ObTxTable *tx_table = ls->get_tx_table();
    // Every 100 milliseconds loop once, corresponding to the single loop interval of tx loop worker, loop 200 times, corresponding to 20 seconds
    // Because tx loop worker will traverse the context every 15 seconds, which is slightly greater than the traversal interval
    int retry_times = 200;
    while (--retry_times >= 0) {
      // Each loop updates the min_start_scn in the tx data table
      tx_table->update_min_start_scn_info(SCN::max_scn());
      // Determine the size relationship of min_start_scn, if an error occurs, print to stdout
      if (prev_min_start_scn > tx_table->ctx_min_start_scn_info_.min_start_scn_in_ctx_) {
        ObCStringHelper helper;
        fprintf(stdout,
                "Incorrect min_start_scn in tx data table, prev_min_start_scn = %s, current_min_start_scn = %s\n",
                helper.convert(prev_min_start_scn),
                helper.convert(tx_table->ctx_min_start_scn_info_.min_start_scn_in_ctx_));
      }
      ASSERT_LE(prev_min_start_scn, tx_table->ctx_min_start_scn_info_.min_start_scn_in_ctx_);
      prev_min_start_scn = tx_table->ctx_min_start_scn_info_.min_start_scn_in_ctx_;
      ASSERT_LE(prev_keep_alive_scn, tx_table->ctx_min_start_scn_info_.keep_alive_scn_);
      prev_keep_alive_scn = tx_table->ctx_min_start_scn_info_.keep_alive_scn_;

      ::usleep(ObTxLoopWorker::LOOP_INTERVAL);
    }
  }
}

void ObTestKeepAliveMinStartSCN::test_min_start_scn()
{
  DEF_VAL_FOR_SQL
  ASSERT_EQ(OB_SUCCESS, get_curr_simple_server().get_sql_proxy2().acquire(connection));
  WRITE_SQL_BY_CONN(connection, "set ob_trx_timeout = 6000000000");
  WRITE_SQL_BY_CONN(connection, "set ob_trx_idle_timeout = 6000000000");

  int64_t current_ts = ObTimeUtil::current_time_ns();
  fprintf(stdout, "current ts : %ld\n", current_ts);

  SCN prev_min_start_scn = SCN::min_scn();
  SCN prev_keep_alive_scn = SCN::min_scn();
  // Execute loop check after sleeping for a while to ensure tx_loop_worker successfully completes one traversal, the keep_alive log will successfully write one NO_CTX,
  // In the error scenario, subsequent UNKNOW logs will modify keep_alive_handler's keep_alive_scn, but will not modify status
  ::sleep(15);
  // Loop check and use information in keep_alive_handler to update data in tx data table, in error scenarios, min_start_scn may be incorrectly increased
  loop_check_start_scn(prev_min_start_scn, prev_keep_alive_scn);

  // create test table
  WRITE_SQL_BY_CONN(connection, "create table if not exists test.test_keep_alive_min_start_scn_t (c1 int, c2 int)");

  // insert data and trigger redo log write
  WRITE_SQL_BY_CONN(connection, "begin");
  WRITE_SQL_BY_CONN(connection, "insert into test.test_keep_alive_min_start_scn_t values(1,1)");
  // Due to the system tenant setting _private_buffer_size, all writes will immediately generate CLOG, and the transaction also has start_scn
  // In the error scenario, since min_start_scn was incorrectly pushed forward earlier, it will now appear that min_start_scn is rolling back
  loop_check_start_scn(prev_min_start_scn, prev_keep_alive_scn);

  WRITE_SQL_BY_CONN(connection, "commit");
  WRITE_SQL_BY_CONN(connection, "alter system minor freeze");
  // After transaction commit, do one more check to ensure that min_start_scn does not retreat throughout the entire process
  loop_check_start_scn(prev_min_start_scn, prev_keep_alive_scn);
}

TEST_F(ObTestKeepAliveMinStartSCN, observer_start)
{
  DEF_VAL_FOR_SQL
  ASSERT_EQ(OB_SUCCESS, get_curr_simple_server().get_sql_proxy().acquire(connection));
  WRITE_SQL_BY_CONN(connection, "alter system set _private_buffer_size = '1B'");
  SERVER_LOG(INFO, "observer_start succ");
}

TEST_F(ObTestKeepAliveMinStartSCN, add_tenant)
{
  ASSERT_EQ(OB_SUCCESS, create_tenant());
  ASSERT_EQ(OB_SUCCESS, get_tenant_id(RunCtx.tenant_id_));
  ASSERT_NE(0, RunCtx.tenant_id_);
  ASSERT_EQ(OB_SUCCESS, get_curr_simple_server().init_sql_proxy2());
}

TEST_F(ObTestKeepAliveMinStartSCN, min_start_scn_test)
{
  test_min_start_scn();
}

TEST_F(ObTestKeepAliveMinStartSCN, end)
{
  if (RunCtx.time_sec_ > 0) {
    ::sleep(RunCtx.time_sec_);
  }
}

} // end unittest
} // end oceanbase


int main(int argc, char **argv)
{
  int c = 0;
  int time_sec = 0;
  char *log_level = (char*)"INFO";
  while(EOF != (c = getopt(argc,argv,"t:l:"))) {
    switch(c) {
    case 't':
      time_sec = atoi(optarg);
      break;
    case 'l':
     log_level = optarg;
     oceanbase::unittest::ObSimpleClusterTestBase::enable_env_warn_log_ = false;
     break;
    default:
      break;
    }
  }
  oceanbase::unittest::init_log_and_gtest(argc, argv);
  OB_LOGGER.set_log_level(log_level);

  LOG_INFO("main>>>");
  oceanbase::unittest::RunCtx.time_sec_ = time_sec;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
