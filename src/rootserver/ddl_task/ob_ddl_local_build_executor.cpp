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
#include "ob_ddl_local_build_executor.h"
#include "rootserver/ob_rootserver_local_runtime.h"
#include "storage/ob_storage_rpc_arg.h"
#include "share/ob_ddl_sim_point.h"
#include "share/ob_ddl_task_executor.h"

using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::rootserver;
using namespace oceanbase::storage;

int ObDDLBuildCtx::init(
    const ObDDLLocalBuildExecutorParam &param,
    const int64_t tablet_idx)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("already inited", K(ret));
  } else if (!param.is_valid() || tablet_idx < 0 || tablet_idx >= param.source_tablet_ids_.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(param), K(tablet_idx));
  } else if (OB_INVALID_ID == param.source_table_ids_.at(tablet_idx) ||
             OB_INVALID_ID == param.dest_table_ids_.at(tablet_idx) ||
             0 == param.source_schema_versions_.at(tablet_idx) ||
             0 == param.dest_schema_versions_.at(tablet_idx) ||
             !param.source_tablet_ids_.at(tablet_idx).is_valid() ||
             !param.dest_tablet_ids_.at(tablet_idx).is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tablet build item", K(ret), K(param), K(tablet_idx));
  } else {
    ddl_type_ = param.ddl_type_;
    src_table_id_ = param.source_table_ids_.at(tablet_idx);
    dest_table_id_ = param.dest_table_ids_.at(tablet_idx);
    src_schema_version_ = param.source_schema_versions_.at(tablet_idx);
    dest_schema_version_ = param.dest_schema_versions_.at(tablet_idx);
    tablet_task_id_ = tablet_idx + 1;
    src_tablet_id_ = param.source_tablet_ids_.at(tablet_idx);
    dest_tablet_id_ = param.dest_tablet_ids_.at(tablet_idx);
    reset_build_stat();
    is_inited_ = true;
  }
  return ret;
}

void ObDDLBuildCtx::reset_build_stat()
{
  stat_ = ObDDLBuildStat::BUILD_INIT;
  ret_code_ = OB_SUCCESS;
  heart_beat_time_ = 0;
  row_inserted_ = 0;
  row_scanned_ = 0;
  physical_row_count_ = 0;
}

bool ObDDLBuildCtx::is_valid() const
{
  bool valid =  is_inited_ && src_table_id_ != OB_INVALID_ID &&
                dest_table_id_ != OB_INVALID_ID && src_schema_version_ != 0 &&
                dest_schema_version_ != 0 && tablet_task_id_ != 0 &&
                src_tablet_id_.is_valid() && dest_tablet_id_.is_valid();
  return valid;
}

int ObDDLBuildCtx::check_need_schedule(bool &need_schedule) const
{
  int ret = OB_SUCCESS;
  need_schedule = false;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else {
    const int64_t elapsed_time = ObTimeUtility::current_time() - heart_beat_time_;
    const bool timeout = (elapsed_time > BUILD_HEART_BEAT_TIME);
    if (stat_ == ObDDLBuildStat::BUILD_INIT ||
        stat_ == ObDDLBuildStat::BUILD_RETRY ||
        (stat_ == ObDDLBuildStat::BUILD_REQUESTED && timeout)) {
      need_schedule = true;
    }
  }
  return ret;
}

int ObDDLLocalBuildExecutor::build(const ObDDLLocalBuildExecutorParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(param));
  } else if (OB_FAIL(DDL_SIM(param.task_id_, LOCAL_BUILD_EXECUTOR_BUILD_FAILED))) {
  } else {
    ObSpinLockGuard guard(lock_);

    ddl_type_ = param.ddl_type_;
    ddl_task_id_ = param.task_id_;
    snapshot_version_ = param.snapshot_version_;
    parallelism_ = param.parallelism_;
    execution_id_ = param.execution_id_;
    data_format_version_ = param.data_format_version_;
    ObArray<ObDDLBuildCtx> build_ctxs;
    if (OB_FAIL(construct_build_ctxs(param, build_ctxs))) {
    } else if (OB_FAIL(lob_col_idxs_.assign(param.lob_col_idxs_))) {
    } else if (OB_FAIL(build_ctxs_.assign(build_ctxs))) {
    } else {
      is_inited_ = true;
    }
    if (OB_FAIL(ret)) {
      is_inited_ = false;
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("start to schedule task", K(build_ctxs_.count()), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));
    if (OB_FAIL(schedule_task())) {
    } else {
      LOG_INFO("start to schedule task", K(param.source_tablet_ids_));
    }
  } else {
    LOG_INFO("fail to start local build task", K(ret), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));
  }
  return ret;
}

int ObDDLLocalBuildExecutor::schedule_task()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("build executor not init", K(ret));
  } else if (OB_FAIL(DDL_SIM(ddl_task_id_, LOCAL_BUILD_EXECUTOR_SCHEDULE_TASK_FAILED))) {
  } else if (OB_ISNULL(rootserver_local_runtime())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("rootserver local runtime is null", K(ret));
  } else {
    ObArray<obcall::ObDDLLocalBuildArg> args;
    ObArray<ObTabletID> tablet_ids;
    {
      ObSpinLockGuard guard(lock_);
      for (int64_t i = 0; OB_SUCC(ret) && i < build_ctxs_.count(); ++i) {
        ObDDLBuildCtx &build_ctx = build_ctxs_.at(i);
        bool need_schedule = false;
        if (OB_FAIL(build_ctx.check_need_schedule(need_schedule))) {
        } else if (need_schedule) {
          obcall::ObDDLLocalBuildArg arg;
          if (OB_FAIL(construct_request_arg(build_ctx, arg))) {
          } else if (OB_FAIL(args.push_back(arg))) {
          } else if (OB_FAIL(tablet_ids.push_back(build_ctx.src_tablet_id_))) {
          }
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < args.count(); ++i) {
      obcall::ObDDLLocalBuildResult result;
      const int call_ret = rootserver_local_runtime()->build_ddl_local(args.at(i), result);
      ObSpinLockGuard guard(lock_);
      bool is_found = false;
      ObDDLBuildCtx *build_ctx = nullptr;
      if (OB_FAIL(get_build_ctx(tablet_ids.at(i), build_ctx, is_found))) {
      } else if (!is_found) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("local build context is missing", K(ret), K(tablet_ids.at(i)));
      } else if (ObDDLBuildStat::BUILD_SUCCEED != build_ctx->stat_ &&
                 OB_FAIL(update_build_ctx_status(*build_ctx, call_ret,
                     result.row_scanned_, result.row_inserted_, result.physical_row_count_, true))) {
        LOG_WARN("failed to update local build request result", K(ret), K(tablet_ids.at(i)));
      }
    }
  }
  return ret;
}

int ObDDLLocalBuildExecutor::check_build_end(const bool need_checksum, bool &is_end, int64_t &ret_code)
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
    LOG_WARN("build executor not init", K(ret));
  }
  {
    ObSpinLockGuard guard(lock_);
    if (OB_SUCC(ret) && !build_ctxs_.empty()) {
      dest_table_id = build_ctxs_.at(0).dest_table_id_;
    }
    total_cnt = build_ctxs_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < total_cnt; ++i) {
      const ObDDLBuildCtx &build_ctx = build_ctxs_.at(i);
      bool need_schedule = false;
      if (build_ctx.stat_ == ObDDLBuildStat::BUILD_FAILED) {
        ++failed_cnt;
        LOG_WARN("check build end, task has failed", K(build_ctx.ret_code_), K(build_ctx));
        if (ret_code == OB_SUCCESS) {
          ret_code = build_ctx.ret_code_;
        }
      } else if (build_ctx.stat_ == ObDDLBuildStat::BUILD_SUCCEED) {
        ++succ_cnt;
      } else if (OB_FAIL(build_ctx.check_need_schedule(need_schedule))) {
      } else if (need_schedule) {
        ++reschedule_cnt;
        LOG_INFO("local build needs reschedule", K(build_ctx));
      } else {
        ++waiting_cnt;
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (failed_cnt != 0) {
    // ret_code already set in for loop
    is_end = true;
    LOG_INFO("local build task failed", K(failed_cnt), K(total_cnt));
  } else if (reschedule_cnt != 0) {
    if (OB_FAIL(schedule_task())) {
    } else {
      LOG_INFO("local build task scheduled again", K(reschedule_cnt), K(total_cnt));
    }
  } else if (succ_cnt == total_cnt) {
    is_end = true;
    ret_code = ret;
    LOG_INFO("all local builds finished", K(succ_cnt), K(total_cnt));
    if (need_checksum) {
      if (OB_FAIL(ObCheckTabletDataComplementOp::check_finish_report_checksum(
          *GCTX.schema_service_, *GCTX.sql_proxy_,
          dest_table_id, execution_id_, ddl_task_id_))) {
      }
    }
  }
  LOG_INFO("check build end:", K(succ_cnt), K(failed_cnt), K(reschedule_cnt),
      K(waiting_cnt), K(total_cnt));
  return ret;
}

int ObDDLLocalBuildExecutor::update_build_progress(
    const common::ObTabletID &tablet_id,
    const int ret_code,
    const int64_t row_scanned,
    const int64_t row_inserted,
    const int64_t physical_row_count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id));
  } else {
    ObSpinLockGuard guard(lock_);
    if (!is_inited_) {
      ret = OB_NOT_INIT;
      LOG_WARN("build executor not init", K(ret));
    } else {
      bool is_found = false;
      ObDDLBuildCtx *build_ctx = nullptr;
      if (OB_FAIL(get_build_ctx(tablet_id, build_ctx, is_found))) {
      } else if (is_found) {
        if (OB_FAIL(update_build_ctx_status(*build_ctx,
                ret_code, row_scanned, row_inserted, physical_row_count, false))) {
        }
        LOG_INFO("received local build progress", K(tablet_id), K(ret_code));
      } else {
        LOG_INFO("ignored build progress for an inactive tablet", K(tablet_id), K(ret_code));
      }
    }
  }
  return ret;
}

int ObDDLLocalBuildExecutor::get_progress(int64_t &row_inserted, int64_t &physical_row_count, double &percent)
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
    LOG_WARN("build executor not init", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < build_ctxs_.count(); ++i) {
    row_inserted += build_ctxs_.at(i).row_inserted_;
    physical_row_count += build_ctxs_.at(i).physical_row_count_;
    if (ObDDLBuildStat::BUILD_SUCCEED != build_ctxs_.at(i).stat_) {
      all_done = false;
    }
  }
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

int ObDDLLocalBuildExecutor::construct_request_arg(
    const ObDDLBuildCtx &build_ctx,
    obcall::ObDDLLocalBuildArg &arg) const
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("build executor not init", K(ret));
  } else if (!build_ctx.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(build_ctx));
  } else {
    arg.source_tablet_id_ = build_ctx.src_tablet_id_;
    arg.dest_tablet_id_ = build_ctx.dest_tablet_id_;
    arg.source_table_id_ = build_ctx.src_table_id_;
    arg.dest_schema_id_ = build_ctx.dest_table_id_;
    arg.schema_version_ = build_ctx.src_schema_version_;
    arg.dest_schema_version_ = build_ctx.dest_schema_version_;
    arg.snapshot_version_ = snapshot_version_;
    arg.ddl_type_ = ddl_type_;
    arg.task_id_ = ddl_task_id_;
    arg.execution_id_ = execution_id_;
    arg.tablet_task_id_ = build_ctx.tablet_task_id_;
    arg.data_format_version_ = data_format_version_;
    arg.parallelism_ = parallelism_;
    if (OB_FAIL(arg.lob_col_idxs_.assign(lob_col_idxs_))) {
    }
  }
  return ret;
}

int ObDDLLocalBuildExecutor::construct_build_ctxs(
    const ObDDLLocalBuildExecutorParam &param,
    ObArray<ObDDLBuildCtx> &build_ctxs) const
{
  int ret = OB_SUCCESS;
  build_ctxs.reuse();
  if (!param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(param));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < param.source_tablet_ids_.count(); ++i) {
      ObDDLBuildCtx build_ctx;
      if (OB_FAIL(build_ctx.init(param, i))) {
      } else if (OB_FAIL(build_ctxs.push_back(build_ctx))) {
      }
    }
  }
  return ret;
}

int ObDDLLocalBuildExecutor::get_build_ctx(
    const ObTabletID &tablet_id,
    ObDDLBuildCtx *&build_ctx,
    bool &is_found)
{
  int ret = OB_SUCCESS;
  build_ctx = nullptr;
  is_found = false;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("build executor not init", K(ret));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  }
  for (int64_t i = 0; OB_SUCC(ret) && !is_found && i < build_ctxs_.count(); ++i) {
    if (build_ctxs_.at(i).src_tablet_id_ == tablet_id) {
      build_ctx = &build_ctxs_.at(i);
      is_found = true;
    }
  }
  return ret;
}

int ObDDLLocalBuildExecutor::update_build_ctx_status(
    ObDDLBuildCtx &build_ctx,
    const int64_t ret_code,
    const int64_t row_scanned,
    const int64_t row_inserted,
    const int64_t physical_row_count,
    const bool is_schedule_result)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("build executor not init", K(ret));
  } else if (ret_code == OB_SUCCESS) {
    build_ctx.ret_code_ = OB_SUCCESS;
    if (is_schedule_result) {
      build_ctx.row_inserted_ = MAX(build_ctx.row_inserted_, row_inserted);
      build_ctx.row_scanned_ = MAX(build_ctx.row_scanned_, row_scanned);
      build_ctx.physical_row_count_ = MAX(build_ctx.physical_row_count_, physical_row_count);
      build_ctx.stat_ = ObDDLBuildStat::BUILD_REQUESTED;
      LOG_INFO("local build request accepted", K(build_ctx.src_tablet_id_), K(build_ctx.dest_tablet_id_));
    } else {
      build_ctx.row_inserted_ = row_inserted;
      build_ctx.row_scanned_ = row_scanned;
      build_ctx.physical_row_count_ = physical_row_count;
      build_ctx.stat_ = ObDDLBuildStat::BUILD_SUCCEED;
      LOG_INFO("local build completed", K(build_ctx.src_tablet_id_), K(build_ctx.dest_tablet_id_));
    }
  } else if (ObIDDLTask::in_ddl_retry_white_list(ret_code)) {
    build_ctx.ret_code_ = OB_SUCCESS;
    build_ctx.row_inserted_ = 0;
    build_ctx.row_scanned_ = 0;
    build_ctx.physical_row_count_ = 0;
    build_ctx.stat_ = ObDDLBuildStat::BUILD_RETRY;
    LOG_INFO("local build needs retry", K(ret_code),
        K(build_ctx.src_tablet_id_), K(build_ctx.dest_tablet_id_), K(is_schedule_result));
  } else {
    build_ctx.ret_code_ = ret_code;
    build_ctx.row_inserted_ = 0;
    build_ctx.row_scanned_ = 0;
    build_ctx.physical_row_count_ = 0;
    build_ctx.stat_ = ObDDLBuildStat::BUILD_FAILED;
    LOG_INFO("local build failed", K(build_ctx.src_tablet_id_),
        K(build_ctx.dest_tablet_id_), K(is_schedule_result), K(build_ctx));
  }
  if (OB_FAIL(ret)) {
  } else if (is_schedule_result) {
    build_ctx.heart_beat_time_ = ObTimeUtility::current_time();
  }
  return ret;
}
