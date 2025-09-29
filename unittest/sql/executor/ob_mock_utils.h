/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_SQL_EXECUTOR_OB_MOCK_SQL_EXECUTOR_RPC_
#define OCEANBASE_SQL_EXECUTOR_OB_MOCK_SQL_EXECUTOR_RPC_

#include "sql/executor/ob_executor_rpc_impl.h"
#include "lib/queue/ob_spop_mpush_queue.h"
#include "../engine/table/ob_fake_partition_service.h"
#include "create_op_util.h"

namespace oceanbase
{
namespace sql
{
static const int64_t TEST_MOCK_COL_NUM = 3;

class ObMockSqlExecutorRpc : public ObExecutorRpcImpl
{
public:
  ObMockSqlExecutorRpc();
  virtual ~ObMockSqlExecutorRpc();
  /*
   * Submit an asynchronous task execution request, receive result data driven by the OB_TASK_NOTIFY_FETCH message
   */
  virtual int task_submit(
      ObExecContext &ctx,
      ObTask &task,
      const common::ObAddr &svr);
  /*
   * Send a task and block waiting until the remote end returns the execution status
   * Save the execution handle in handler, and subsequently data can be received through handler
   * */
  virtual int task_execute(
      ObExecContext &ctx,
      ObTask &task,
      const common::ObAddr &svr,
      RemoteExecuteStreamHandle &handler);
  /*
   * Send a command to kill a task and block waiting for the remote end to return the execution status
   * */
  virtual int task_kill(
      ObTaskInfo &task,
      const common::ObAddr &svr);
  /*
   * Task execution completed on the Worker side, notify Scheduler to start Task reading results
   * */
  virtual int task_complete(
      ObTaskEvent &task_event,
      const common::ObAddr &svr);

  /*
   * Send the execution result of a task, do not wait for a response
   * */
  virtual int task_notify_fetch(
      ObTaskEvent &task_event,
      const common::ObAddr &svr);
  /*
   * Get all scanners of an intermediate result of a task, blocking until all scanners return
   * */
  virtual int task_fetch_result(
      const ObSliceID &ob_slice_id,
      const common::ObAddr &svr,
      FetchResultStreamHandle &handler);

public:
  storage::ObFakePartitionService partition_service_;

private:
  bool task_location_exist(ObTaskLocation task_loc);
private:
  common::ObArray<ObTaskLocation> task_loc_array_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMockSqlExecutorRpc);
};

class ObMockRemoteExecuteStreamHandle : public RemoteExecuteStreamHandle
{
public:
  ObMockRemoteExecuteStreamHandle(common::ObIAllocator &alloc) : RemoteExecuteStreamHandle(alloc)
  {}
  ~ObMockRemoteExecuteStreamHandle()
  {}
  virtual int get_more(ObScanner &scanner);
  virtual bool has_more();
};


class ObMockFetchResultStreamHandle : public FetchResultStreamHandle
{
public:
  ObMockFetchResultStreamHandle(common::ObIAllocator &alloc) : FetchResultStreamHandle(alloc)
  {}
  ~ObMockFetchResultStreamHandle()
  {}
  virtual int get_more(ObScanner &scanner);
  virtual bool has_more();

  void set_server(const common::ObAddr &server) { server_ = server; }
  void set_slice_id(const ObSliceID &ob_slice_id) { ob_slice_id_ = ob_slice_id; }
private:
  bool task_location_exist(ObTaskLocation task_loc);
private:
  // when task_submit is called, fill it in here
  common::ObArray<ObTaskLocation> task_loc_array_;
  common::ObAddr server_;
  ObSliceID ob_slice_id_;
};

/************************************simulate packet queue********************************/
class ObMockPacketQueueThread : public share::ObThreadPool
{
public:
  static const int64_t THREAD_COUNT = 1;
  static ObMockPacketQueueThread *get_instance();

  ObMockPacketQueueThread();
  virtual ~ObMockPacketQueueThread() {}

  void run1();

  common::ObSPopMPushQueue packet_queue_;
private:
  static ObMockPacketQueueThread *instance_;
  static obutil::Mutex locker_;
};

}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_MOCK_SQL_EXECUTOR_RPC_ */
