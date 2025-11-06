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
