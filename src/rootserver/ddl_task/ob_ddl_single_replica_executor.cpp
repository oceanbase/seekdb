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

#define USING_LOG_PREFIX RS
#include "lib/stat/ob_diagnostic_info_guard.h"
#include <vector>
#include "share/ob_ex_rpc.h"
#include "ob_ddl_single_replica_executor.h"
#include "storage/ob_storage_rpc_arg.h"
#include "rootserver/ob_root_service.h"
#include "observer/ob_service.h"
#include "share/ob_ddl_sim_point.h"

using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::rootserver;
using namespace oceanbase::storage;

int ObSingleReplicaBuildCtx::init(
    const ObAddr& addr,
    const share::ObDDLType ddl_type,
    const int64_t src_table_id,
    const int64_t dest_table_id,
    const int64_t src_schema_version,
    const int64_t dest_schema_version,
    const int64_t tablet_task_id,
    const ObTabletID &src_tablet_id,
    const ObTabletID &dest_tablet_id)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("already inited", K(ret));
  } else if (!addr.is_valid() ||
             src_table_id == OB_INVALID_ID ||
             dest_table_id == OB_INVALID_ID ||
             tablet_task_id == 0 ||
             !src_tablet_id.is_valid() ||
             !dest_tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(addr), K(src_table_id), K(dest_table_id),
                                 K(tablet_task_id), K(src_tablet_id), K(dest_tablet_id), K(ddl_type));
  } else {
    addr_ = addr;
    ddl_type_ = ddl_type;
    src_table_id_ = src_table_id;
    dest_table_id_ = dest_table_id;
    src_schema_version_ = src_schema_version;
    dest_schema_version_ = dest_schema_version;
    tablet_task_id_ = tablet_task_id;
    src_tablet_id_ = src_tablet_id;
    dest_tablet_id_ = dest_tablet_id;
    reset_build_stat();
    is_inited_ = true;
  }
  return ret;
}

void ObSingleReplicaBuildCtx::reset_build_stat()
{
  stat_ = ObReplicaBuildStat::BUILD_INIT;
  ret_code_ = OB_SUCCESS;
  heart_beat_time_ = 0;
  row_inserted_ = 0;
  row_scanned_ = 0;
  physical_row_count_ = 0;
}

bool ObSingleReplicaBuildCtx::is_valid() const
{
  bool valid =  is_inited_ && addr_.is_valid() && src_table_id_ != OB_INVALID_ID &&
                dest_table_id_ != OB_INVALID_ID && src_schema_version_ != 0 &&
                dest_schema_version_ != 0 && tablet_task_id_ != 0 &&
                src_tablet_id_.is_valid() && dest_tablet_id_.is_valid();
  return valid;
}

int ObSingleReplicaBuildCtx::assign(const ObSingleReplicaBuildCtx &other)
{
  int ret = OB_SUCCESS;
  {
    is_inited_ = other.is_inited_;
    addr_ = other.addr_;
    ddl_type_ = other.ddl_type_;
    src_table_id_ = other.src_table_id_;
    dest_table_id_ = other.dest_table_id_;
    src_schema_version_ = other.src_schema_version_;
    dest_schema_version_ = other.dest_schema_version_;
    tablet_task_id_ = other.tablet_task_id_;
    src_tablet_id_ = other.src_tablet_id_;
    stat_ = other.stat_;
    ret_code_ = other.ret_code_;
    heart_beat_time_ = other.heart_beat_time_;
    row_inserted_ = other.row_inserted_;
    row_scanned_ = other.row_scanned_;
    physical_row_count_ = other.physical_row_count_;
    dest_tablet_id_ = other.dest_tablet_id_;
  }
  return ret;
}

int ObSingleReplicaBuildCtx::check_need_schedule(bool &need_schedule) const
{
  int ret = OB_SUCCESS;
  need_schedule = false;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else {
    const int64_t elapsed_time = ObTimeUtility::current_time() - heart_beat_time_;
    const bool timeout = (elapsed_time > REPLICA_BUILD_HEART_BEAT_TIME);
    if (stat_ == ObReplicaBuildStat::BUILD_INIT ||
        stat_ == ObReplicaBuildStat::BUILD_RETRY ||
        (stat_ == ObReplicaBuildStat::BUILD_REQUESTED && timeout)) {
      need_schedule = true;
    }
  }
  return ret;
}

int ObDDLReplicaBuildExecutor::build(const ObDDLReplicaBuildExecutorParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(param));
  } else if (OB_FAIL(DDL_SIM(param.task_id_, SINGLE_REPLICA_EXECUTOR_BUILD_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(param.task_id_));
  } else { // lock scope, keep construct_replica_build_ctxs() out of scope
    ObSpinLockGuard guard(lock_);
    
    ddl_type_ = param.ddl_type_;
    ddl_task_id_ = param.task_id_;
    snapshot_version_ = param.snapshot_version_;
    parallelism_ = param.parallelism_;
    execution_id_ = param.execution_id_;
    data_format_version_ = param.data_format_version_;
    is_no_logging_ = param.is_no_logging_;
    ObArray<ObSingleReplicaBuildCtx> replica_build_ctxs;
    if (OB_FAIL(construct_replica_build_ctxs(param, replica_build_ctxs))) {
      LOG_WARN("failed to construct replica build ctxs", K(ret));
    } else if (OB_FAIL(lob_col_idxs_.assign(param.lob_col_idxs_))) {
      LOG_WARN("failed to assign to lob col idxs", K(ret));
    } else if (OB_FAIL(src_tablet_ids_.assign(param.source_tablet_ids_))) {
      LOG_WARN("failed to assign to tablet ids", K(ret));
    } else if (OB_FAIL(dest_tablet_ids_.assign(param.dest_tablet_ids_))) {
      LOG_WARN("failed to assign to dest tablet ids", K(ret));
    } else if (OB_FAIL(replica_build_ctxs_.assign(replica_build_ctxs))) {
      LOG_WARN("failed to setup replica build ctxs", K(ret));
    } else {
      is_inited_ = true;
    }
    if (OB_FAIL(ret)) {
      is_inited_ = false; 
    }
  } // lock scope, keep schedule_task() out of lock scope

  // TODO(lihongqin.lhq)
  // char table_id_buffer[256];
  // snprintf(table_id_buffer, sizeof(table_id_buffer), "dest_table_id:%ld, source_table_id:%ld", dest_table_id_, source_table_id_);
  // ROOTSERVICE_EVENT_ADD("ddl scheduler", "build single replica",
  //   "tenant",sys tenant,
  //   "ret", ret,
  //   "trace_id", *ObCurTraceId::get_trace_id(),
  //   K_(task_id),
  //   "type", type_,
  //   K_(schema_version),
  //   table_id_buffer);

  if (OB_SUCC(ret)) {
    LOG_INFO("start to schedule task", K(src_tablet_ids_.count()), "ddl_event_info", ObDDLEventInfo());
    if (OB_FAIL(schedule_task())) {
      LOG_WARN("fail to schedule tasks", K(ret));
    } else {
      LOG_INFO("start to schedule task", K(param.source_tablet_ids_));
    }
  } else {
    LOG_INFO("fail to build single replica task", K(ret), "ddl_event_info", ObDDLEventInfo());
  }
  return ret;
}

int ObDDLReplicaBuildExecutor::schedule_task()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("replica build executor not init", K(ret));
  } else if (OB_FAIL(DDL_SIM(ddl_task_id_, SINGLE_REPLICA_EXECUTOR_SCHEDULE_TASK_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(ddl_task_id_));
  } else {
    ObArray<obcall::ObDDLBuildSingleReplicaRequestArg> args;
    ObArray<ObAddr> addrs;
    ObArray<ObTabletID> tablet_ids;
    { // lock scope
      ObSpinLockGuard guard(lock_); // ensure build ctxs will not change
      for (int64_t i = 0; OB_SUCC(ret) && i < replica_build_ctxs_.count(); ++i) {
        ObSingleReplicaBuildCtx &replica_build_ctx = replica_build_ctxs_.at(i);
        bool need_schedule = false;
        if (OB_FAIL(replica_build_ctx.check_need_schedule(need_schedule))) {
          LOG_WARN("failed to check need schedule", K(ret));
        } else if (need_schedule) {
          obcall::ObDDLBuildSingleReplicaRequestArg arg;
          if (OB_FAIL(construct_rpc_arg(replica_build_ctx, arg))) {
            LOG_WARN("failed to construct single replica request arg", K(ret));
          } else if (OB_FAIL(args.push_back(arg))) {
            LOG_WARN("failed to push back arg", K(ret));
          } else if (OB_FAIL(addrs.push_back(replica_build_ctx.addr_))) {
            LOG_WARN("failed to push back addr", K(ret));
          } else if (OB_FAIL(tablet_ids.push_back(replica_build_ctx.src_tablet_id_))) {
            LOG_WARN("failed to push back tablet id", K(ret));
          }
        }
      }
    } // lock scope
    // async_call preserves parallelism (replaces proxy.call + wait_all).
    using H = std::shared_ptr<ex_rpc::AsyncHandle<obcall::ObDDLBuildSingleReplicaRequestResult>>;
    std::vector<H> handles;
    ObArray<int> ret_array;
    ObArray<obcall::ObDDLBuildSingleReplicaRequestResult> results;
    for (int64_t i = 0; OB_SUCC(ret) && i < args.count(); ++i) {
      auto h = ex_rpc::async_call<obcall::ObDDLBuildSingleReplicaRequestResult>(args.at(i),
          [](const obcall::ObDDLBuildSingleReplicaRequestArg &req,
             obcall::ObDDLBuildSingleReplicaRequestResult &res) -> int {
            return GCTX.ob_service_->build_ddl_single_replica_request(req, res);
          });
      if (OB_ISNULL(h)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        handles.push_back(h);
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < (int64_t)handles.size(); ++i) {
      int call_ret = handles[i]->wait();
      if (OB_FAIL(ret_array.push_back(call_ret))) {
        LOG_WARN("push_back ret failed", K(ret));
      } else if (OB_FAIL(results.push_back(handles[i]->result()))) {
        LOG_WARN("push_back result failed", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      ObArray<const obcall::ObDDLBuildSingleReplicaRequestResult *> result_ptrs;
      for (int64_t i = 0; OB_SUCC(ret) && i < results.count(); ++i) {
        if (OB_FAIL(result_ptrs.push_back(&results.at(i)))) {
          LOG_WARN("push_back result ptr failed", K(ret));
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(process_rpc_results(tablet_ids, addrs, result_ptrs, ret_array))) {
        LOG_WARN("failed to process result", K(ret));
      }
    }
  }
  return ret;
}

/* before check if build is finished, get refreshed replica addrs for all tablets,
 * after that, refresh replica build ctxs, finally check each replica build status
 */
int ObDDLReplicaBuildExecutor::check_build_end(const bool need_checksum, bool &is_end, int64_t &ret_code)
{
  int ret = OB_SUCCESS;
  is_end = false;
  ret_code = OB_SUCCESS;
  int64_t succ_cnt = 0;
  int64_t failed_cnt = 0;
  int64_t reschedule_cnt = 0;
  int64_t waiting_cnt = 0;
  int64_t total_cnt = 0;
  int64_t dest_table_id = OB_INVALID_ID;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("replica build executor not init", K(ret));
  }
  { // lock scope
    ObSpinLockGuard guard(lock_);
    if (OB_FAIL(ret)) {
    } else if (!replica_build_ctxs_.empty()) {
      dest_table_id = replica_build_ctxs_.at(0).dest_table_id_;
    }
    total_cnt = replica_build_ctxs_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < total_cnt; ++i) {
      const ObSingleReplicaBuildCtx &replica_build_ctx = replica_build_ctxs_.at(i);
      bool need_schedule = false;
      if (replica_build_ctx.stat_ == ObReplicaBuildStat::BUILD_FAILED) {
        ++failed_cnt;
        LOG_WARN("check build end, task has failed", K(replica_build_ctx.ret_code_),
            K(replica_build_ctx));
        if (ret_code == OB_SUCCESS) {
          ret_code = replica_build_ctx.ret_code_;
        }
      } else if (replica_build_ctx.stat_ == ObReplicaBuildStat::BUILD_SUCCEED) {
        ++succ_cnt;
      } else if (OB_FAIL(replica_build_ctx.check_need_schedule(need_schedule))) {
        LOG_WARN("failed to check need schedule", K(ret));
      } else if (need_schedule) {
        ++reschedule_cnt;
        LOG_INFO("replica build need reschedule", K(replica_build_ctx));
      } else { // rpc requested, waiting for report
        ++waiting_cnt;
      }
    }
  } // lock scope, keep schedule task out of lock scope
  if (OB_FAIL(ret)) {
  } else if (failed_cnt != 0) {
    // ret_code already set in for loop
    is_end = true;
    LOG_INFO("exist replica build task failed", K(failed_cnt), K(total_cnt));
  } else if (reschedule_cnt != 0) {
    if (OB_FAIL(schedule_task())) {
      LOG_WARN("fail to schedule task", K(ret));
    } else {
      LOG_INFO("replica build task schedule again", K(reschedule_cnt), K(total_cnt));
    }
  } else if (succ_cnt == total_cnt) {
    is_end = true;
    ret_code = ret;
    LOG_INFO("all replica build finished", K(succ_cnt), K(total_cnt));
    if (need_checksum) {
      if (OB_FAIL(ObCheckTabletDataComplementOp::check_finish_report_checksum(dest_table_id, execution_id_, ddl_task_id_))) {
        LOG_WARN("fail to check sstable checksum_report_finish", 
            K(ret), K(dest_table_id), K(execution_id_), K(ddl_task_id_));
      }
    }
  }
  LOG_INFO("check build end:", K(succ_cnt), K(failed_cnt), K(reschedule_cnt),
      K(waiting_cnt), K(total_cnt));
  return ret;
}

// update replica build ctx if tablet_id && addr is matched
// do nothing if no one matches
int ObDDLReplicaBuildExecutor::update_build_progress(
    const common::ObTabletID &tablet_id,
    const ObAddr &addr,
    const int ret_code,
    const int64_t row_scanned,
    const int64_t row_inserted,
    const int64_t physical_row_count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid() || !addr.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(addr));
  } else { // lock scope
    ObSpinLockGuard guard(lock_);
    if (!is_inited_) { // hold lock before access is_inited_
      ret = OB_NOT_INIT;
      LOG_WARN("replica build executor not init", K(ret));
    } else {
      bool is_found = false;
      ObSingleReplicaBuildCtx *replica_build_ctx = nullptr;
      if (OB_FAIL(get_replica_build_ctx(tablet_id, addr,
              replica_build_ctx, is_found))) {
        LOG_WARN("failed to get replica build ctx", K(ret), K(tablet_id), K(addr));
      } else if (is_found) {
        if (OB_FAIL(update_replica_build_ctx(*replica_build_ctx,
                ret_code, row_scanned, row_inserted, physical_row_count, false/*is_rpc_request*/,
                true/*is_observer_report*/))) {
          LOG_WARN("failed to update replica build ctx", K(ret), K(tablet_id), K(addr), K(ret_code));
        }
        LOG_INFO("receive build progress report from replica", K(tablet_id), K(addr), K(ret_code));
      } else { // not found
        LOG_INFO("ignore build progress report from expired replica", K(tablet_id), K(addr), K(ret_code));
      }
    }
  } // lock scope
  return ret;
}

int ObDDLReplicaBuildExecutor::get_progress(int64_t &row_inserted, int64_t &physical_row_count, double &percent)
{
  int ret = OB_SUCCESS;
  bool all_done = true;
  row_inserted = 0;
  physical_row_count = 0;
  percent = 0;
  // lock scope
  ObSpinLockGuard guard(lock_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("replica build executor not init", K(ret));
  }  
  for (int64_t i = 0; OB_SUCC(ret) && i < replica_build_ctxs_.count(); ++i) {
    row_inserted += replica_build_ctxs_.at(i).row_inserted_;
    physical_row_count += replica_build_ctxs_.at(i).physical_row_count_;
    if (ObReplicaBuildStat::BUILD_SUCCEED != replica_build_ctxs_.at(i).stat_) {
      all_done = false;
    }
  }
  // 100% if all replica_build_ctxs_.at(i).stat_ == BUILD_SUCCEED
  if (OB_FAIL(ret)){
    // error occurred
  } else if (all_done) { // lob meta maybe 0 rows, percent should be 0; (in row storing)
    percent = 100.0;
  } else if (physical_row_count == 0) {
    percent = 0.0;
  } else {
    percent = row_inserted * 100.0 / physical_row_count;
  }
  return ret;
}

// as caller, schedule_task() will hold lock
int ObDDLReplicaBuildExecutor::construct_rpc_arg(
    const ObSingleReplicaBuildCtx &replica_build_ctx,
    obcall::ObDDLBuildSingleReplicaRequestArg &arg) const
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("replica build executor not init", K(ret));
  } else if (!replica_build_ctx.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(replica_build_ctx));
  } else {
    
    
    arg.source_tablet_id_ = replica_build_ctx.src_tablet_id_;
    arg.dest_tablet_id_ = replica_build_ctx.dest_tablet_id_;
    arg.source_table_id_ = replica_build_ctx.src_table_id_;
    arg.dest_schema_id_ = replica_build_ctx.dest_table_id_;
    arg.schema_version_ = replica_build_ctx.src_schema_version_;
    arg.dest_schema_version_ = replica_build_ctx.dest_schema_version_;
    arg.snapshot_version_ = snapshot_version_;
    arg.ddl_type_ = ddl_type_;
    arg.task_id_ = ddl_task_id_;
    arg.execution_id_ = execution_id_;
    arg.tablet_task_id_ = replica_build_ctx.tablet_task_id_;
    arg.data_format_version_ = data_format_version_;
    arg.parallelism_ = parallelism_;
    arg.is_no_logging_ = is_no_logging_;
    if (OB_FAIL(arg.lob_col_idxs_.assign(lob_col_idxs_))) {
      LOG_WARN("failed to assign to lob col idxs", K(ret));
    }
  }
  return ret;
}

int ObDDLReplicaBuildExecutor::process_rpc_results(
    const ObArray<ObTabletID> &tablet_ids,
    const ObArray<ObAddr> addrs,
    const ObIArray<const obcall::ObDDLBuildSingleReplicaRequestResult *> &result_array,
    const ObArray<int> &ret_array)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("replica build executor not init", K(ret));
  } else if (tablet_ids.count() != addrs.count() ||
             ret_array.count() != addrs.count() ||
             result_array.count() != addrs.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, rets count is not equal to request count", K(ret),
        K(tablet_ids.count()), K(addrs.count()), K(ret_array.count()),
        K(result_array.count()));
  }
  { // lock scope
    ObSpinLockGuard guard(lock_);
    for (int64_t i = 0; OB_SUCC(ret) && i < result_array.count(); ++i) {
      const ObTabletID &tablet_id = tablet_ids.at(i);
      const ObAddr &addr = addrs.at(i);
      bool is_found = false;
      ObSingleReplicaBuildCtx *replica_build_ctx = nullptr;
      if (OB_FAIL(get_replica_build_ctx(tablet_id, addr, replica_build_ctx,
              is_found))) {
        LOG_WARN("failed to get replica build ctx", K(ret));
      } else if (is_found) {
        if (replica_build_ctx->stat_ != ObReplicaBuildStat::BUILD_INIT) {
          continue; // already handle respone rpc
        } else if (OB_FAIL(update_build_ctx(*replica_build_ctx,
                result_array.at(i), ret_array.at(i)))) {
          LOG_WARN("failed to update build progress", K(ret));
        }
      } else { // not found, replica addr refreshed, ignore result
        LOG_INFO("replica addr refreshed, ignore rpc result from ",
            K(tablet_id), K(addr));
      }
    }
  } // lock scope
  return ret;
}

// as caller, process_rpc_result() will hold lock
int ObDDLReplicaBuildExecutor::update_build_ctx(
    ObSingleReplicaBuildCtx &build_ctx,
    const oceanbase::obcall::ObDDLBuildSingleReplicaRequestResult *result,
    const int ret_code)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("replica build executor not init", K(ret));
  } else if (OB_FAIL(update_replica_build_ctx(build_ctx, ret_code,
          result->row_scanned_, result->row_inserted_, result->physical_row_count_, true/*is_rpc_request*/,
          false/*is_observer_report*/))) {
    LOG_WARN("failed to update replica build ctx", K(ret));
  }
  return ret;
}

// as caller, build() will hold lock
int ObDDLReplicaBuildExecutor::construct_replica_build_ctxs(
    const ObDDLReplicaBuildExecutorParam &param,
    ObArray<ObSingleReplicaBuildCtx> &replica_build_ctxs) const
{
  int ret = OB_SUCCESS;
  replica_build_ctxs.reuse();
  if (!param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(param));
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < param.source_tablet_ids_.count(); ++i) {
      const ObTabletID &src_tablet_id = param.source_tablet_ids_.at(i);
      const ObTabletID &dest_tablet_id = param.dest_tablet_ids_.at(i);
      const int64_t src_table_id = param.source_table_ids_.at(i);
      const int64_t dest_table_id = param.dest_table_ids_.at(i);
      const int64_t src_schema_version = param.source_schema_versions_.at(i);
      const int64_t dest_schema_version = param.dest_schema_versions_.at(i);
      const int64_t tablet_task_id = i + 1;
      ObSingleReplicaBuildCtx replica_build_ctx;
      if (OB_FAIL(replica_build_ctx.init(GCTX.self_addr(), ddl_type_,
              src_table_id, dest_table_id, src_schema_version, dest_schema_version,
              tablet_task_id, src_tablet_id, dest_tablet_id))) {
        LOG_WARN("failed to init replica build ctx", K(ret), K(src_tablet_id));
      } else if (OB_FAIL(replica_build_ctxs.push_back(replica_build_ctx))) {
        LOG_WARN("failed to push back replica build ctx", K(ret));
      }
    }
  }
  return ret;
}

// look up repica build ctx by tablet id && addr
// NOTE as caller, update_build_progress() process_rpc_results()
int ObDDLReplicaBuildExecutor::get_replica_build_ctx(
    const ObTabletID &tablet_id,
    const ObAddr &addr,
    ObSingleReplicaBuildCtx *&replica_build_ctx,
    bool &is_found)
{
  int ret = OB_SUCCESS;
  replica_build_ctx = nullptr;
  is_found = false;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("replica build executor not init", K(ret));
  } else if (!tablet_id.is_valid() || !addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(addr));
  }
  for (int64_t i = 0; OB_SUCC(ret) && !is_found && i < replica_build_ctxs_.count(); ++i) {
    if (replica_build_ctxs_.at(i).src_tablet_id_ == tablet_id &&
        replica_build_ctxs_.at(i).addr_ == addr) {
      replica_build_ctx = &replica_build_ctxs_.at(i);
      is_found = true;
    }
  }
  return ret;
}

// NOTE as caller, update_build_progress(), update_build_ctx() will hold lock
int ObDDLReplicaBuildExecutor::update_replica_build_ctx(
    ObSingleReplicaBuildCtx &build_ctx,
    const int64_t ret_code,
    const int64_t row_scanned,
    const int64_t row_inserted,
    const int64_t physical_row_count,
    const bool is_rpc_request,
    const bool is_observer_report)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("replica build executor not init", K(ret));
  } else if (ret_code == OB_SUCCESS) {
    build_ctx.ret_code_ = OB_SUCCESS;
    if (is_rpc_request) {
      build_ctx.row_inserted_ = MAX(build_ctx.row_inserted_, row_inserted);
      build_ctx.row_scanned_ = MAX(build_ctx.row_scanned_, row_scanned);
      build_ctx.physical_row_count_ = MAX(build_ctx.physical_row_count_, physical_row_count);
      build_ctx.stat_ = ObReplicaBuildStat::BUILD_REQUESTED;
      LOG_INFO("rpc send successfully", K(build_ctx.addr_),
          K(build_ctx.src_tablet_id_), K(build_ctx.dest_tablet_id_));
    } else if (is_observer_report) {
      build_ctx.row_inserted_ = row_inserted;
      build_ctx.row_scanned_ = row_scanned;
      build_ctx.physical_row_count_ = physical_row_count;
      build_ctx.stat_ = ObReplicaBuildStat::BUILD_SUCCEED;
      LOG_INFO("receive observer build success report", K(build_ctx.addr_),
          K(build_ctx.src_tablet_id_), K(build_ctx.dest_tablet_id_));
    }
  } else if (ObIDDLTask::in_ddl_retry_white_list(ret_code)) {
    build_ctx.ret_code_ = OB_SUCCESS;
    build_ctx.row_inserted_ = 0;
    build_ctx.row_scanned_ = 0;
    build_ctx.physical_row_count_ = 0;
    build_ctx.stat_ = ObReplicaBuildStat::BUILD_RETRY;
    if (ret_code == common::OB_SESSION_NOT_FOUND) {
      build_ctx.sess_not_found_times_++;
    }
    LOG_INFO("task need retry", K(ret_code), K(build_ctx.addr_),
        K(build_ctx.src_tablet_id_), K(build_ctx.dest_tablet_id_),
        K(is_rpc_request), K(is_observer_report));
  } else { // other error ret_code
    build_ctx.ret_code_ = ret_code;
    build_ctx.row_inserted_ = 0;
    build_ctx.row_scanned_ = 0;
    build_ctx.physical_row_count_ = 0;
    build_ctx.stat_ = ObReplicaBuildStat::BUILD_FAILED;
    LOG_INFO("task is failed", K(build_ctx.addr_), K(build_ctx.src_tablet_id_),
        K(build_ctx.dest_tablet_id_), K(is_rpc_request), K(is_observer_report), K(build_ctx));
  }
  if (OB_FAIL(ret)) {
  } else if (is_rpc_request) {
    build_ctx.heart_beat_time_ = ObTimeUtility::current_time();
  }
  return ret;
}
