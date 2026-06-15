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

#ifndef OCEANBASE_SQL_EXECUTOR_RPC_IMPL_
#define OCEANBASE_SQL_EXECUTOR_RPC_IMPL_

#include "share/ob_define.h"
#include "lib/container/ob_array.h"
#include "lib/allocator/ob_allocator.h"
#include "share/ob_scanner.h"
#include "sql/executor/ob_task.h"
#include "sql/executor/ob_task_info.h"
#include "sql/executor/ob_slice_id.h"
#include "lib/ob_define.h"

namespace oceanbase
{
namespace sql
{
class ObQueryRetryInfo;

// On single-replica seekdb the optimizer never emits a remote SQL plan
// (ObTableLocation::get_location_type always returns LOCAL because the
// only replica lives on self), so this stream handle is never driven by a
// real remote RPC. It is kept only so the (unreachable) remote-plan
// executor/direct-receive scaffolding compiles. Streaming is a no-op:
// has_more() is always false, and get_more()/abort() never run.
class RemoteExecuteStreamHandle
{
public:
  RemoteExecuteStreamHandle(const char *label, uint64_t tenant_id) :
    use_remote_protocol_v2_(false),
    result_(label, NULL, common::ObScanner::DEFAULT_MAX_SERIALIZE_SIZE, tenant_id),
    rc_(common::OB_SUCCESS),
    dst_addr_()
  {
  }
  ~RemoteExecuteStreamHandle() {}
  void set_use_remote_protocol_v2() { use_remote_protocol_v2_ = true; }
  void reset()
  {
    result_.reset();
    rc_ = common::OB_SUCCESS;
  }
  const common::ObAddr &get_dst_addr() const { return dst_addr_; }
  void set_dst_addr(const common::ObAddr &addr) { dst_addr_ = addr; }

  int reset_and_init_result()
  {
    int ret = common::OB_SUCCESS;
    result_.reset();
    if (!result_.is_inited() && OB_FAIL(result_.init())) {
      SQL_EXE_LOG(WARN, "fail to init result", K(ret));
    }
    return ret;
  }

  void set_result_code(int code) { rc_ = code; }
  int get_result_code() { return rc_; }

  void set_task_id(const ObTaskID &task_id) { task_id_ = task_id; }
  const ObTaskID &get_task_id() const { return task_id_; }

  common::ObScanner *get_result()
  {
    common::ObScanner *ret_result = NULL;
    if (!result_.is_inited()) {
      SQL_EXE_LOG_RET(ERROR, common::OB_NOT_INIT, "result_ is not inited");
    } else {
      ret_result = &result_;
    }
    return ret_result;
  }
  // No remote streaming on single replica: never more than the first scanner.
  bool has_more() { return false; }
  int abort() { return common::OB_SUCCESS; }
  int get_more(common::ObScanner &result)
  {
    UNUSED(result);
    int ret = common::OB_NOT_SUPPORTED;
    SQL_EXE_LOG(ERROR, "remote stream get_more is not supported on single replica", K(ret));
    return ret;
  }
private:
  bool use_remote_protocol_v2_;
  ObTaskID task_id_;
  common::ObScanner result_;
  int rc_;
  common::ObAddr dst_addr_;
};

class ObExecutorRpcCtx
{
public:
  //FIXME qianfu only for compatibility, remove after 1.4.0
  static const uint64_t INVALID_CLUSTER_VERSION = 0;
public:
  ObExecutorRpcCtx(uint64_t rpc_tenant_id,
                   int64_t timeout_timestamp,
                   uint64_t min_cluster_version,
                   ObQueryRetryInfo *retry_info,
                   ObSQLSessionInfo *session,
                   bool is_plain_select,
                   int32_t group_id)
    : rpc_tenant_id_(rpc_tenant_id),
      timeout_timestamp_(timeout_timestamp),
      min_cluster_version_(min_cluster_version),
      retry_info_(retry_info),
      session_(session),
      is_plain_select_(is_plain_select),
      group_id_(group_id)
  {
  }
  ~ObExecutorRpcCtx() {}

  uint64_t get_rpc_tenant_id() const { return rpc_tenant_id_; }
  inline int64_t get_timeout_timestamp() const { return timeout_timestamp_; }
  // The timeout provided to the storage layer will be reduced by 100ms
  // The timeout here needs to be aligned.
  inline int64_t get_ps_timeout_timestamp() const { return timeout_timestamp_ - ESTIMATE_PS_RESERVE_TIME; }
  // Equal to INVALID_CLUSTER_VERSION means it is serialized from an old observer on a remote node
  inline bool min_cluster_version_is_valid() const
  {
    return INVALID_CLUSTER_VERSION != min_cluster_version_;
  }
  inline uint64_t get_min_cluster_version() const { return min_cluster_version_; }
  inline const ObQueryRetryInfo *get_retry_info() const { return retry_info_; }
  inline ObQueryRetryInfo *get_retry_info_for_update() const { return retry_info_; }
  bool is_retry_for_rpc_timeout() const { return is_plain_select_; }
  int32_t get_group_id() const { return group_id_; }
  TO_STRING_KV(K_(rpc_tenant_id),
               K_(timeout_timestamp),
               K_(min_cluster_version),
               K_(retry_info),
               K_(is_plain_select),
               K_(group_id));
private:
  uint64_t rpc_tenant_id_;
  int64_t timeout_timestamp_;
  uint64_t min_cluster_version_;
  // retry_info_ == NULL indicates that no feedback information needs to be provided to the retry module for this rpc
  ObQueryRetryInfo *retry_info_;
  const ObSQLSessionInfo *session_;// The variables in this class will be accessed concurrently, note whether the session is correctly accessed concurrently
  bool is_plain_select_;//stmt_type == T_SELECT && not select...for update
  int32_t group_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExecutorRpcCtx);
};


#define OB_SQL_REMOTE_TASK_TYPE 1
#define OB_SQL_REMOTE_RESULT_TYPE 2
// Remote SQL plan execution over obcall RPC has been removed on single-replica
// seekdb (the optimizer never produces a remote plan, see
// ObTableLocation::get_location_type). These methods remain as defensive
// unreachable stubs for the dead remote-plan executor scaffolding.
class ObExecutorRpcImpl
{
public:
  ObExecutorRpcImpl() { }
  virtual ~ObExecutorRpcImpl() {}
  int init();
  virtual int task_execute(ObExecutorRpcCtx &rpc_ctx,
                           ObTask &task,
                           const common::ObAddr &svr,
                           RemoteExecuteStreamHandle &handler,
                           bool &has_sent_task,
                           bool &has_transfer_err);
  virtual int task_execute_v2(ObExecutorRpcCtx &rpc_ctx,
                              ObRemoteTask &task,
                              const common::ObAddr &svr,
                              RemoteExecuteStreamHandle &handler,
                              bool &has_sent_task,
                              bool &has_transfer_err);
  virtual int task_kill(
      ObExecutorRpcCtx &rpc_ctx,
      const ObTaskID &task_id,
      const common::ObAddr &svr);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExecutorRpcImpl);
};

}
}
#endif /* OCEANBASE_SQL_EXECUTOR_RPC_IMPL_ */
//// end of header file
