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

#define USING_LOG_PREFIX SQL_EXE

#include "ob_task_executor_ctx.h"
#include "observer/ob_server.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
namespace oceanbase
{
namespace sql
{

int ObTaskExecutorCtx::CalcVirtualPartitionIdParams::init(uint64_t ref_table_id)
{
  int ret = common::OB_SUCCESS;
  if (true == inited_) {
    ret = common::OB_INIT_TWICE;
    LOG_ERROR("init twice", K(ret), K(inited_), K(ref_table_id));
  } else {
    inited_ = true;
    ref_table_id_ = ref_table_id;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObTaskExecutorCtx,
                    table_locations_,
                    retry_times_,
                    min_cluster_version_,
                    expected_worker_cnt_,
                    admited_worker_cnt_,
                    query_tenant_begin_schema_version_,
                    query_sys_begin_schema_version_,
                    minimal_worker_cnt_);

ObTaskExecutorCtx::ObTaskExecutorCtx(ObExecContext &exec_context)
    : task_resp_handler_(NULL),
      virtual_part_servers_(exec_context.get_allocator()),
      exec_ctx_(&exec_context),
      expected_worker_cnt_(0),
      minimal_worker_cnt_(0),
      admited_worker_cnt_(0),
      retry_times_(0),
      min_cluster_version_(ObExecutorRpcCtx::INVALID_CLUSTER_VERSION),
      sys_job_id_(-1),
      rs_rpc_proxy_(nullptr),
      query_tenant_begin_schema_version_(-1),
      query_sys_begin_schema_version_(-1),
      schema_service_(GCTX.schema_service_)
{
}

ObTaskExecutorCtx::~ObTaskExecutorCtx()
{
  if (NULL != task_resp_handler_) {
    task_resp_handler_->~RemoteExecuteStreamHandle();
    task_resp_handler_ = NULL;
  }
  if (rs_rpc_proxy_ != nullptr) {
    rs_rpc_proxy_->~ObCommonRpcProxy();
    rs_rpc_proxy_ = nullptr;
  }
}


int ObTaskExecutorCtx::set_table_locations(const ObTablePartitionInfoArray &table_partition_infos)
{
  int ret = OB_SUCCESS;
  //table_locations_ must be reset here to ensure that table partition infos are not added repeatedly
  table_locations_.reset();
  ObPhyTableLocation phy_table_loc;
  int64_t N = table_partition_infos.count();
  if (OB_FAIL(table_locations_.reserve(N))) {
    LOG_WARN("fail reserve locations", K(ret), K(N));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < N; ++i) {
    phy_table_loc.reset();
    ObTablePartitionInfo *partition_info = table_partition_infos.at(i);
    if (OB_ISNULL(partition_info)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error. table partition info is null", K(ret), K(i));
    } else if (partition_info->get_table_location().use_das()) {
      //do nothing, DAS's location is maintained and calculated by itself
    } else if (OB_FAIL(phy_table_loc.assign_from_phy_table_loc_info(partition_info->get_phy_tbl_location_info()))) {
      LOG_WARN("fail to assign_from_phy_table_loc_info", K(ret), K(i), K(partition_info->get_phy_tbl_location_info()), K(N));
    } else if (OB_FAIL(table_locations_.push_back(phy_table_loc))) {
      LOG_WARN("fail to push back into table locations", K(ret), K(i), K(phy_table_loc), K(N));
    }
  }
  return ret;
}

int ObTaskExecutorCtx::append_table_location(const ObCandiTableLoc &phy_location_info)
{
  int ret = OB_SUCCESS;
  ObPhyTableLocation phy_table_loc;
  if (OB_FAIL(phy_table_loc.assign_from_phy_table_loc_info(phy_location_info))) {
    LOG_WARN("assign from physical table location info failed", K(ret));
  } else if (OB_FAIL(table_locations_.push_back(phy_table_loc))) {
    LOG_WARN("store table location failed", K(ret));
  }
  return ret;
}

//
//
//  Utility
//
int ObTaskExecutorCtxUtil::get_stream_handler(
    ObExecContext &ctx,
    RemoteExecuteStreamHandle *&handler)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *executor_ctx = NULL;
  if (OB_ISNULL(executor_ctx = ctx.get_task_executor_ctx())) {
    ret = OB_NOT_INIT;
    LOG_WARN("fail get executor ctx");
  } else if (OB_ISNULL(handler = executor_ctx->get_stream_handler())) {
    ret = OB_NOT_INIT;
    LOG_WARN("stream handler is not inited", K(ret));
  }
  return ret;
}
int ObTaskExecutorCtxUtil::get_task_executor_rpc(
    ObExecContext &ctx,
    ObExecutorRpcImpl *&rpc)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *executor_ctx = NULL;
  if (OB_ISNULL(executor_ctx = ctx.get_task_executor_ctx())) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTaskExecutorCtx is null", K(ret));
  } else if (OB_ISNULL(rpc = executor_ctx->get_task_executor_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("rpc is null", K(ret));
  }
  return ret;
}

obrpc::ObCommonRpcProxy *ObTaskExecutorCtx::get_common_rpc()
{
  int ret = OB_SUCCESS;
  obrpc::ObCommonRpcProxy *ret_pointer = NULL;
  if (OB_FAIL(get_common_rpc(ret_pointer))) {
    LOG_WARN("get common rpc problem ", K(ret));
  }
  return ret_pointer;
}

int ObTaskExecutorCtx::get_common_rpc(obrpc::ObCommonRpcProxy *&common_rpc_proxy)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(exec_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("null pointer", K_(exec_ctx), K(ret));
  } else if (OB_ISNULL(exec_ctx_->get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("physical plan ctx is null", K(ret));
  } else {
    const int64_t timeout = exec_ctx_->get_physical_plan_ctx()->get_timeout_timestamp() -
        ObTimeUtility::current_time();
    if (rs_rpc_proxy_ == nullptr) {
      void *buf = nullptr;
      if (OB_ISNULL(buf = exec_ctx_->get_allocator().alloc(sizeof(ObCommonRpcProxy)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate rpc proxy memory failed", K(ret));
      } else {
        rs_rpc_proxy_ = new(buf) ObCommonRpcProxy();
        *rs_rpc_proxy_ = *GCTX.rs_rpc_proxy_;
      }
    }
    if (OB_SUCC(ret)) {
      if (timeout <= 0) {
        ret = OB_TIMEOUT;
        LOG_WARN("execute task timeout", K(timeout), K(ret));
      } else {
        rs_rpc_proxy_->set_timeout(timeout);
        common_rpc_proxy = rs_rpc_proxy_;
      }
    }
  }
  return ret;
}

int ObTaskExecutorCtx::reset_and_init_stream_handler()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(exec_ctx_)) {
    ret = OB_NOT_INIT;
    LOG_ERROR("unexpected error. exec ctx is not inited", K(ret));
  } else {
    if (NULL != task_resp_handler_) {
      // It might be caused by transaction_set_violation_and_retry leading to a retry at the executor level,
      // ObTaskExecutorCtx destructor is called multiple times before this function,
      // So here we need to destruct the previous memory first
      task_resp_handler_->~RemoteExecuteStreamHandle();
      task_resp_handler_ = NULL;
    }
    RemoteExecuteStreamHandle *buffer = NULL;
    if (OB_ISNULL(buffer = static_cast<RemoteExecuteStreamHandle*>(exec_ctx_->get_allocator().//is this allocator ok ?
        alloc(sizeof(RemoteExecuteStreamHandle))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory for RemoteExecuteStreamHandle", K(ret));
    } else {
      task_resp_handler_ = new (buffer) RemoteExecuteStreamHandle("RemoteExecStream", MTL_ID());
    }
  }
  return ret;
}



const common::ObAddr ObTaskExecutorCtx::get_self_addr() const
{
  return MYADDR;
}
}/* ns sql*/
}/* ns oceanbase */
