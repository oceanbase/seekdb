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

#define USING_LOG_PREFIX STORAGE

#include "ob_restore_complete_dag_net.h"
#include "lib/ob_define.h"
#include "lib/oblog/ob_log_module.h"
#include "observer/ob_server.h"
#include "share/rc/ob_tenant_base.h"
#include "share/scheduler/ob_tenant_dag_scheduler.h"
#include "share/scheduler/ob_dag_warning_history_mgr.h"
#include "share/config/ob_server_config.h"
#include "lib/utility/ob_tracepoint.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "storage/high_availability/ob_storage_ha_utils.h"
#include "storage/high_availability/ob_storage_ha_dag.h"
#include "storage/high_availability/ob_restore_status.h"
#include "storage/tablet/ob_tablet.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "logservice/palf/palf_handle.h"
#include "logservice/ob_log_service.h"
#include "common/ob_smart_call.h"
#include "lib/time/ob_time_utility.h"
#include "lib/ob_errno.h"
#include "observer/omt/ob_tenant_config_mgr.h"
#include "share/ob_time_utility2.h"

namespace oceanbase
{
using namespace share;
using namespace storage;
namespace restore
{

/****************************ObCompleteRestoreCtx************************************/
ObCompleteRestoreCtx::ObCompleteRestoreCtx()
  : ObIHADagNetCtx(),
    task_(),
    start_ts_(0),
    finish_ts_(0)
{
}

ObCompleteRestoreCtx::~ObCompleteRestoreCtx()
{
}

bool ObCompleteRestoreCtx::is_valid() const
{
  return task_.is_valid();
}

void ObCompleteRestoreCtx::reset()
{
  ObIHADagNetCtx::reset();
  task_.reset();
  start_ts_ = 0;
  finish_ts_ = 0;
}

void ObCompleteRestoreCtx::reuse()
{
  ObIHADagNetCtx::reuse();
}

int ObCompleteRestoreCtx::fill_comment(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_ERR_UNDEFINED;
    LOG_WARN("complete restore ctx is invalid", K(ret));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else {
    int64_t pos = 0;
    ObCStringHelper helper;
    if (OB_FAIL(databuff_printf(buf, buf_len, pos, "task_id=%s, task_type=%s",
                                  helper.convert(task_.task_id_), ObRestoreTaskType::get_str(task_.type_)))) {
      LOG_WARN("failed to fill comment", K(ret), KPC(this));
    }
  }
  return ret;
}

/****************************ObCompleteRestoreParam************************************/
ObCompleteRestoreParam::ObCompleteRestoreParam()
  : task_(),
    handler_(nullptr),
    result_(OB_SUCCESS)
{
}

bool ObCompleteRestoreParam::is_valid() const
{
  return task_.is_valid() && OB_NOT_NULL(handler_);
}

void ObCompleteRestoreParam::reset()
{
  task_.reset();
  handler_ = nullptr;
  result_ = OB_SUCCESS;
}

/****************************ObCompleteRestoreDagNet************************************/
ObCompleteRestoreDagNet::ObCompleteRestoreDagNet()
  : ObIDagNet(ObDagNetType::DAG_NET_TYPE_RESTORE_COMPLETE),
    is_inited_(false),
    ctx_(),
    handler_(nullptr)
{
}

ObCompleteRestoreDagNet::~ObCompleteRestoreDagNet()
{
}

int ObCompleteRestoreDagNet::init_by_param(const share::ObIDagInitParam *param)
{
  int ret = OB_SUCCESS;
  const ObCompleteRestoreParam *init_param = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("complete restore dag net is init twice", K(ret));
  } else if (OB_ISNULL(param) || !param->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("param is null or invalid", K(ret));
  } else if (FALSE_IT(init_param = static_cast<const ObCompleteRestoreParam *>(param))) {
  } else if (OB_FAIL(this->set_dag_id(init_param->task_.task_id_))) {
    LOG_WARN("failed to set dag id", K(ret), KPC(init_param));
  } else {
    ctx_.task_ = init_param->task_;
    handler_ = init_param->handler_;
    if (OB_SUCCESS != init_param->result_) {
      if (OB_FAIL(ctx_.set_result(init_param->result_, false /*allow_retry*/))) {
        LOG_WARN("failed to set complete restore ctx result", K(ret), KPC(&ctx_));
      }
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    }
  }

  return ret;
}

bool ObCompleteRestoreDagNet::is_valid() const
{
  return is_inited_ && ctx_.is_valid() && OB_NOT_NULL(handler_);
}

int ObCompleteRestoreDagNet::start_running()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("complete restore dag net is not init", K(ret));
  } else if (OB_FAIL(start_running_for_complete_())) {
    LOG_WARN("failed to start complete restore dag net", K(ret));
  }
  return ret;
}

int ObCompleteRestoreDagNet::start_running_for_complete_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObInitialCompleteRestoreDag *initial_dag = nullptr;
  ObTenantDagScheduler *scheduler = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("complete restore dag net do not init", K(ret));
  } else if (FALSE_IT(ctx_.start_ts_ = ObTimeUtil::current_time())) {
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", K(ret));
  } else if (OB_FAIL(scheduler->alloc_dag(initial_dag))) {
    LOG_WARN("failed to alloc initial complete restore dag", K(ret));
  } else if (OB_FAIL(initial_dag->init(this))) {
    LOG_WARN("failed to init initial complete restore dag", K(ret));
  } else if (OB_FAIL(add_dag_into_dag_net(*initial_dag))) {
    LOG_WARN("failed to add initial dag into dag net", K(ret));
  } else if (OB_FAIL(initial_dag->create_first_task())) {
    LOG_WARN("failed to create first task", K(ret));
  } else if (OB_FAIL(scheduler->add_dag(initial_dag))) {
    LOG_WARN("failed to add initial dag", K(ret), KPC(initial_dag));
    if (OB_SIZE_OVERFLOW == ret || OB_EAGAIN == ret) {
      ret = OB_EAGAIN;
    }
  } else {
    initial_dag = nullptr;
  }

  if (OB_NOT_NULL(initial_dag) && OB_NOT_NULL(scheduler)) {
    if (OB_TMP_FAIL(erase_dag_from_dag_net(*initial_dag))) {
      LOG_WARN("failed to erase dag from dag net", K(tmp_ret), KPC(initial_dag));
    }
    scheduler->free_dag(*initial_dag);
    initial_dag = nullptr;
  }

  if (OB_FAIL(ret)) {
    const bool need_retry = false;
    if (OB_TMP_FAIL(ctx_.set_result(ret, need_retry))) {
      LOG_ERROR("failed to set complete restore ctx result", K(ret), K(tmp_ret), KPC(&ctx_));
    }
  }

  return ret;
}

bool ObCompleteRestoreDagNet::operator==(const share::ObIDagNet &other) const
{
  bool is_same = true;
  if (this == &other) {
    // same
  } else if (get_type() != other.get_type()) {
    is_same = false;
  } else {
    const ObCompleteRestoreDagNet &other_dag_net = static_cast<const ObCompleteRestoreDagNet &>(other);
    if (ctx_.task_.task_id_ != other_dag_net.ctx_.task_.task_id_) {
      is_same = false;
    }
  }
  return is_same;
}

uint64_t ObCompleteRestoreDagNet::hash() const
{
  uint64_t hash_value = 0;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    tmp_ret = OB_NOT_INIT;
    LOG_ERROR_RET(tmp_ret, "complete restore dag net do not init", K(tmp_ret), K(ctx_));
  } else {
    hash_value = common::murmurhash(&ctx_.task_.task_id_, sizeof(ctx_.task_.task_id_), hash_value);
  }
  return hash_value;
}

int ObCompleteRestoreDagNet::fill_comment(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("complete restore dag net do not init", K(ret));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(ctx_.fill_comment(buf, buf_len))) {
    LOG_WARN("failed to fill comment", K(ret), KPC(&ctx_));
  }
  return ret;
}

int ObCompleteRestoreDagNet::fill_dag_net_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  char task_id_str[OB_MAX_TRACE_ID_BUFFER_SIZE] = { 0 };
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("complete restore dag net do not init", K(ret));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_UNLIKELY(0 > ctx_.task_.task_id_.to_string(task_id_str, OB_MAX_TRACE_ID_BUFFER_SIZE))) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_WARN("failed to get trace id string", K(ret), KPC(&ctx_));
  } else {
    int64_t pos = 0;
    ret = databuff_printf(buf, buf_len, pos, "ObCompleteRestoreDagNet: task_id=");
    OB_SUCCESS != ret ? : ret = databuff_printf(buf, buf_len, pos, "%s", task_id_str);
    OB_SUCCESS != ret ? : ret = databuff_printf(buf, buf_len, pos, ", task_type=");
    OB_SUCCESS != ret ? : ret = databuff_printf(buf, buf_len, pos, "%s", ObRestoreTaskType::get_str(ctx_.task_.type_));
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to fill dag net key", K(ret), KPC(&ctx_));
    }
  }
  return ret;
}

int ObCompleteRestoreDagNet::clear_dag_net_ctx()
{
  int ret = OB_SUCCESS;
  int32_t result = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  LOG_INFO("start clear complete restore dag net ctx", KPC(&ctx_));
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("complete restore dag net do not init", K(ret));
  } else if (OB_FAIL(ObStorageHADagUtils::get_ls(sys_ls_id, ls_handle))) {
    LOG_WARN("failed to get ls", K(ret), KPC(&ctx_));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be null", K(ret), KPC(&ctx_));
  } else {
    if (OB_TMP_FAIL(update_restore_status_(ls))) {
      LOG_WARN("failed to finalize update restore status", K(ret), K(tmp_ret), KPC(&ctx_));
    }
    if (OB_FAIL(ctx_.get_result(result))) {
      LOG_WARN("failed to get ls complate migration ctx result", K(ret), K(ctx_));
    } else if (OB_FAIL(handler_->set_result(result))) {
      LOG_WARN("failed to set result", K(ret), K(result), K(ctx_));
    }
    ctx_.finish_ts_ = ObTimeUtil::current_time();
    const int64_t cost_ts = ctx_.finish_ts_ - ctx_.start_ts_;
    FLOG_INFO("finish complete restore dag net", "task_id", ctx_.task_.task_id_,
        "task_type", ctx_.task_.type_, K(cost_ts), K(result));
  }
  return ret;
}

int ObCompleteRestoreDagNet::update_restore_status_(ObLS *ls)
{
  int ret = OB_SUCCESS;
  bool is_finish = false;
  static const int64_t UPDATE_RESTORE_STATUS_INTERVAL_100_MS = 100 * 1000; // 100ms
  ObTenantDagScheduler *scheduler = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("complete restore dag net do not init", K(ret));
  } else if (OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("update restore status get invalid argument", K(ret), KP(ls));
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", K(ret));
  } else {
    while (!is_finish) {
      ObRestoreStatus current_restore_status(ObRestoreStatus::Status::RESTORE_STATUS_MAX);
      ObRestoreStatus next_restore_status(ObRestoreStatus::Status::RESTORE_STATUS_MAX);
      bool need_update_status = true;
      if (ls->is_stopped()) {
        ret = OB_NOT_RUNNING;
        LOG_WARN("ls is not running, stop restore dag net", K(ret), KPC(&ctx_));
        break;
      } else if (scheduler->has_set_stop()) {
        ret = OB_SERVER_IS_STOPPING;
        LOG_WARN("tenant dag scheduler has set stop, stop restore dag net", K(ret), KPC(&ctx_));
        break;
      } else {
        next_restore_status = ObRestoreStatus::Status::NONE;
        if (OB_FAIL(ls->get_restore_status(current_restore_status))) {
          LOG_WARN("failed to get restore status", K(ret), KPC(ls), KPC(&ctx_));
        } else if (current_restore_status.is_none() || current_restore_status.is_restore_failed()) {
          need_update_status = false;
          if (!ctx_.is_failed()) {
            int32_t result = OB_ERR_UNEXPECTED;
            if (OB_FAIL(ctx_.set_result(result, false/*need_retry*/))) {
              LOG_WARN("failed to set result", K(ret), K(result), K(ctx_));
            }
          }
          LOG_INFO("restore status is none or failed, no need to update status", K(current_restore_status), KPC(&ctx_));
        } else if (OB_FAIL(get_next_complete_status_(current_restore_status, next_restore_status))) {
          LOG_WARN("failed to get next complete status", K(ret), KPC(&ctx_), K(current_restore_status));
        }

        if (OB_FAIL(ret)) {
          // do nothing
         } else if (!need_update_status) {
          is_finish = true;
        } else if (OB_FAIL(ls->set_restore_status(next_restore_status))) {
          LOG_WARN("failed to set restore status", K(ret),
              K(current_restore_status), K(next_restore_status), KPC(&ctx_), KPC(ls));
        } else {
          is_finish = true;
        }
      }

      if (OB_FAIL(ret)) {
        ob_usleep(UPDATE_RESTORE_STATUS_INTERVAL_100_MS);
      }
    }
  }

  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    const bool need_retry = false;
    if (OB_SUCCESS != (tmp_ret = ctx_.set_result(ret, need_retry))) {
      LOG_ERROR("failed to set result", K(ret), K(tmp_ret), KPC(&ctx_));
    }
  }
  return ret;
}

int ObCompleteRestoreDagNet::deal_with_cancel()
{
  int ret = OB_SUCCESS;
  const int32_t result = OB_CANCELED;
  const bool need_retry = false;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("complete restore dag net do not init", K(ret));
  } else if (OB_FAIL(ctx_.set_result(result, need_retry))) {
    LOG_WARN("failed to set complete restore ctx result", K(ret), KPC(&ctx_));
  }
  return ret;
}

int ObCompleteRestoreDagNet::get_next_complete_status_(
    const ObRestoreStatus &current_complete_status,
    ObRestoreStatus &next_complete_status)
{
  int ret = OB_SUCCESS;
  next_complete_status = current_complete_status;
  if (ctx_.is_failed()) {
    next_complete_status = ObRestoreStatus::Status::RESTORE_FAILED;
    LOG_WARN("restore task failed, transition to RESTORE_FAILED", K(current_complete_status), KPC(&ctx_));
  } else {
    next_complete_status = ObRestoreStatus::Status::NONE;
  }
  return ret;
}

/****************************ObCompleteRestoreDag************************************/
ObCompleteRestoreDag::ObCompleteRestoreDag(const share::ObDagType::ObDagTypeEnum &dag_type)
  : ObRestoreDag(dag_type)
{
}

ObCompleteRestoreDag::~ObCompleteRestoreDag()
{
}

bool ObCompleteRestoreDag::operator==(const share::ObIDag &other) const
{
  bool is_same = true;
  if (this == &other) {
    // same
  } else if (get_type() != other.get_type()) {
    is_same = false;
  } else {
    const ObCompleteRestoreDag &other_dag = static_cast<const ObCompleteRestoreDag&>(other);
    ObCompleteRestoreDagNet *dag_net = static_cast<ObCompleteRestoreDagNet *>(dag_net_);
    ObCompleteRestoreDagNet *other_dag_net = static_cast<ObCompleteRestoreDagNet *>(other_dag.dag_net_);
    if (OB_ISNULL(dag_net) || OB_ISNULL(other_dag_net)) {
      is_same = false;
      LOG_ERROR_RET(OB_ERR_UNEXPECTED, "dag net should not be null", KP(dag_net), KP(other_dag_net));
    } else if (dag_net->get_complete_ctx()->task_.task_id_ != other_dag_net->get_complete_ctx()->task_.task_id_) {
      is_same = false;
    }
  }
  return is_same;
}

uint64_t ObCompleteRestoreDag::hash() const
{
  int ret = OB_SUCCESS;
  uint64_t hash_value = 0;
  if (OB_ISNULL(ha_dag_net_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ha dag net ctx should not be null", K(ret), KPC(ha_dag_net_ctx_));
  } else if (ObIHADagNetCtx::DagNetCtxType::RESTORE_COMPLETE != ha_dag_net_ctx_->get_dag_net_ctx_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ha dag net ctx type is unexpected", K(ret), KPC(ha_dag_net_ctx_));
  } else {
    ObCompleteRestoreCtx *ctx = static_cast<ObCompleteRestoreCtx *>(ha_dag_net_ctx_);
    hash_value = ctx->task_.task_id_.hash();
    hash_value = common::murmurhash(&ctx->task_.type_, sizeof(ctx->task_.type_), hash_value);
  }
  return hash_value;
}

int ObCompleteRestoreDag::fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const
{
  int ret = OB_SUCCESS;
  ObCompleteRestoreCtx *ctx = nullptr;

  if (OB_ISNULL(ha_dag_net_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ha dag net ctx should not be null", K(ret));
  } else if (ObIHADagNetCtx::DagNetCtxType::RESTORE_COMPLETE != ha_dag_net_ctx_->get_dag_net_ctx_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ha dag net ctx type is unexpected", K(ret), K(ha_dag_net_ctx_->get_dag_net_ctx_type()));
  } else if (FALSE_IT(ctx = static_cast<ObCompleteRestoreCtx *>(ha_dag_net_ctx_))) {
  } else {
    ObCStringHelper helper;
    if (OB_FAIL(ADD_DAG_WARN_INFO_PARAM(out_param, allocator, get_type(),
                                static_cast<int64_t>(ctx->task_.type_),
                                "src_info", helper.convert(ctx->task_.src_info_)))) {
      LOG_WARN("failed to fill info param", K(ret));
    }
  }
  return ret;
}

int ObCompleteRestoreDag::prepare_ctx(share::ObIDagNet *dag_net)
{
  int ret = OB_SUCCESS;
  ObCompleteRestoreDagNet *complete_dag_net = nullptr;
  ObCompleteRestoreCtx *self_ctx = nullptr;

  if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE_COMPLETE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), K(dag_net->get_type()));
  } else if (FALSE_IT(complete_dag_net = static_cast<ObCompleteRestoreDagNet *>(dag_net))) {
  } else if (FALSE_IT(self_ctx = complete_dag_net->get_complete_ctx())) {
  } else if (OB_ISNULL(self_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("complete restore ctx should not be NULL", K(ret), KP(self_ctx));
  } else {
    ha_dag_net_ctx_ = self_ctx;
  }
  return ret;
}

/****************************ObInitialCompleteRestoreDag************************************/
ObInitialCompleteRestoreDag::ObInitialCompleteRestoreDag()
  : ObCompleteRestoreDag(share::ObDagType::DAG_TYPE_INITIAL_COMPLETE_RESTORE),
    is_inited_(false)
{
}

ObInitialCompleteRestoreDag::~ObInitialCompleteRestoreDag()
{
}

int ObInitialCompleteRestoreDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  ObCompleteRestoreDagNet *dag_net = static_cast<ObCompleteRestoreDagNet *>(dag_net_);
  ObCStringHelper helper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("initial complete restore dag do not init", K(ret));
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be null", K(ret));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(databuff_printf(buf, buf_len, "ObInitialCompleteRestoreDag task_id=%s, task_type=%s, src_info=%s",
      helper.convert(dag_net->get_complete_ctx()->task_.task_id_),
      ObRestoreTaskType::get_str(dag_net->get_complete_ctx()->task_.type_),
      helper.convert(dag_net->get_complete_ctx()->task_.src_info_)))) {
    LOG_WARN("failed to fill dag key", K(ret), KPC(dag_net));
  }
  return ret;
}

int ObInitialCompleteRestoreDag::create_first_task()
{
  int ret = OB_SUCCESS;
  ObInitialCompleteRestoreTask *task = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("initial complete restore dag do not init", K(ret));
  } else if (OB_FAIL(alloc_task(task))) {
    LOG_WARN("Fail to alloc task", K(ret));
  } else if (OB_FAIL(task->init())) {
    LOG_WARN("failed to init initial complete restore task", K(ret), KPC(ha_dag_net_ctx_));
  } else if (OB_FAIL(add_task(*task))) {
    LOG_WARN("Fail to add task", K(ret));
  } else {
    LOG_DEBUG("success to create first task", K(ret), KPC(this));
  }
  return ret;
}

int ObInitialCompleteRestoreDag::init(share::ObIDagNet *dag_net)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("initial complete restore dag is init twice", K(ret));
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dag net should not be null", K(ret));
  } else if (OB_FAIL(ObCompleteRestoreDag::prepare_ctx(dag_net))) {
    LOG_WARN("failed to prepare ctx", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

/****************************ObInitialCompleteRestoreTask************************************/
ObInitialCompleteRestoreTask::ObInitialCompleteRestoreTask()
  : ObITask(ObITaskType::TASK_TYPE_RESTORE_COMPLETE_INITIAL),
    is_inited_(false),
    ctx_(nullptr),
    dag_net_(nullptr)
{
}

ObInitialCompleteRestoreTask::~ObInitialCompleteRestoreTask()
{
}

int ObInitialCompleteRestoreTask::init()
{
  int ret = OB_SUCCESS;
  ObIDagNet *dag_net = nullptr;
  ObCompleteRestoreDagNet *complete_dag_net = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("initial complete restore task init twice", K(ret));
  } else if (FALSE_IT(dag_net = this->get_dag()->get_dag_net())) {
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE_COMPLETE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), K(dag_net->get_type()));
  } else if (FALSE_IT(complete_dag_net = static_cast<ObCompleteRestoreDagNet *>(dag_net))) {
  } else if (OB_ISNULL(ctx_ = complete_dag_net->get_complete_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx_ is nullptr", K(ret), KPC(complete_dag_net));
  } else {
    dag_net_ = dag_net;
    is_inited_ = true;
    LOG_INFO("succeed init initial complete restore task", "task_id", ctx_->task_.task_id_,
        "dag_id", *ObCurTraceId::get_trace_id());
  }
  return ret;
}

int ObInitialCompleteRestoreTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("initial complete restore task do not init", K(ret));
  } else if (OB_FAIL(generate_restore_dags_())) {
    LOG_WARN("failed to generate restore dags", K(ret));
  }

  if (OB_TMP_FAIL(record_server_event_())) {
    LOG_WARN("failed to record server event", K(tmp_ret), K(ret));
  }

  if (OB_FAIL(ret)) {
    if (OB_TMP_FAIL(ObStorageHADagUtils::deal_with_fo(ret, this->get_dag()))) {
      LOG_WARN("failed to deal with fo", K(ret), K(tmp_ret), KPC(ctx_));
    }
  }

  return ret;
}

int ObInitialCompleteRestoreTask::generate_restore_dags_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObWaitDataReadyRestoreDag *wait_dag = nullptr;
  ObFinishCompleteRestoreDag *finish_dag = nullptr;
  ObTenantDagScheduler *scheduler = nullptr;
  ObInitialCompleteRestoreDag *initial_dag = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("initial complete restore task do not init", K(ret));
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", K(ret));
  } else if (OB_ISNULL(initial_dag = static_cast<ObInitialCompleteRestoreDag *>(this->get_dag()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("initial complete restore dag should not be NULL", K(ret), KP(initial_dag));
  } else {
    if (OB_FAIL(scheduler->alloc_dag(wait_dag))) {
      LOG_WARN("failed to alloc wait data ready restore dag", K(ret));
    } else if (OB_FAIL(scheduler->alloc_dag(finish_dag))) {
      LOG_WARN("failed to alloc finish complete restore dag", K(ret));
    } else if (OB_FAIL(wait_dag->init(dag_net_))) {
      LOG_WARN("failed to init wait data ready restore dag", K(ret));
    } else if (OB_FAIL(finish_dag->init(dag_net_))) {
      LOG_WARN("failed to init finish complete restore dag", K(ret));
    } else if (OB_FAIL(this->get_dag()->add_child(*wait_dag))) {
      LOG_WARN("failed to add wait data ready restore dag", K(ret), KPC(wait_dag));
    } else if (OB_FAIL(wait_dag->create_first_task())) {
      LOG_WARN("failed to create first task", K(ret));
    } else if (OB_FAIL(wait_dag->add_child(*finish_dag))) {
      LOG_WARN("failed to add finish complete restore dag as child", K(ret));
    } else if (OB_FAIL(finish_dag->create_first_task())) {
      LOG_WARN("failed to create first task", K(ret));
    } else if (OB_FAIL(scheduler->add_dag(finish_dag))) {
      LOG_WARN("failed to add finish complete restore dag", K(ret), K(*finish_dag));
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_WARN("Fail to add task", K(ret));
        ret = OB_EAGAIN;
      }
    } else if (OB_FAIL(scheduler->add_dag(wait_dag))) {
      LOG_WARN("failed to add wait data ready restore dag", K(ret), K(*wait_dag));
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_WARN("Fail to add task", K(ret));
        ret = OB_EAGAIN;
      }

      if (OB_TMP_FAIL(scheduler->cancel_dag(finish_dag))) {
        LOG_WARN("failed to cancel finish complete restore dag", K(tmp_ret), KPC(finish_dag));
      }
      finish_dag = nullptr;
    } else {
      LOG_INFO("succeed to add all dag", K(*wait_dag), K(*finish_dag));
      wait_dag = nullptr;
      finish_dag = nullptr;
    }

    if (OB_FAIL(ret)) {
      if (OB_NOT_NULL(scheduler) && OB_NOT_NULL(finish_dag)) {
        scheduler->free_dag(*finish_dag);
        finish_dag = nullptr;
      }

      if (OB_NOT_NULL(scheduler) && OB_NOT_NULL(wait_dag)) {
        scheduler->free_dag(*wait_dag);
        wait_dag = nullptr;
      }

      if (OB_TMP_FAIL(ctx_->set_result(ret, true /*allow_retry*/, this->get_dag()->get_type()))) {
        LOG_WARN("failed to set complete restore result", K(ret), K(tmp_ret), KPC(ctx_));
      }
    }
  }
  return ret;
}

int ObInitialCompleteRestoreTask::record_server_event_()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("initial complete restore task do not init", K(ret));
  } else {
    SERVER_EVENT_ADD("storage_ha", "standby_restore_complete_initial_task",
        "task_id", ctx_->task_.task_id_,
        "task_type", ctx_->task_.type_,
        "src_addr", ctx_->task_.src_info_);
  }
  return ret;
}

/****************************ObWaitDataReadyRestoreDag************************************/
ObWaitDataReadyRestoreDag::ObWaitDataReadyRestoreDag()
  : ObCompleteRestoreDag(share::ObDagType::DAG_TYPE_WAIT_DATA_READY_RESTORE),
    is_inited_(false)
{
}

ObWaitDataReadyRestoreDag::~ObWaitDataReadyRestoreDag()
{
}

int ObWaitDataReadyRestoreDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  ObCompleteRestoreCtx *self_ctx = nullptr;
  ObCStringHelper helper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("wait data ready restore dag do not init", K(ret));
  } else if (ObIHADagNetCtx::DagNetCtxType::RESTORE_COMPLETE != ha_dag_net_ctx_->get_dag_net_ctx_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ha dag net ctx type is unexpected", K(ret), KPC(ha_dag_net_ctx_));
  } else if (FALSE_IT(self_ctx = static_cast<ObCompleteRestoreCtx *>(ha_dag_net_ctx_))) {
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(databuff_printf(buf, buf_len, "ObWaitDataReadyRestoreDag: task_id = %s, task_type = %s",
                                        helper.convert(self_ctx->task_.task_id_),
                                        ObRestoreTaskType::get_str(self_ctx->task_.type_)))) {
    LOG_WARN("failed to fill dag key", K(ret), KPC(self_ctx));
  }
  return ret;
}

int ObWaitDataReadyRestoreDag::init(share::ObIDagNet *dag_net)
{
  int ret = OB_SUCCESS;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("wait data ready restore dag init twice", K(ret));
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init wait data ready restore dag get invalid argument", K(ret), KP(dag_net));
  } else if (OB_FAIL(ObCompleteRestoreDag::prepare_ctx(dag_net))) {
    LOG_WARN("failed to prepare ctx", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObWaitDataReadyRestoreDag::create_first_task()
{
  int ret = OB_SUCCESS;
  ObWaitDataReadyRestoreTask *task = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("wait data ready restore dag do not init", K(ret));
  } else if (OB_FAIL(alloc_task(task))) {
    LOG_WARN("Fail to alloc task", K(ret));
  } else if (OB_FAIL(task->init())) {
    LOG_WARN("failed to init wait data ready restore task", K(ret), KPC(ha_dag_net_ctx_));
  } else if (OB_FAIL(add_task(*task))) {
    LOG_WARN("Fail to add task", K(ret));
  } else {
    LOG_DEBUG("success to create first task", K(ret), KPC(this));
  }
  return ret;
}

/****************************ObWaitDataReadyRestoreTask************************************/
ObWaitDataReadyRestoreTask::ObWaitDataReadyRestoreTask()
  : ObITask(ObITaskType::TASK_TYPE_RESTORE_COMPLETE_WAIT_DATA_READY),
    is_inited_(false),
    ctx_(nullptr),
    ls_handle_(),
    log_sync_lsn_(),
    max_minor_end_scn_(share::SCN::min_scn())
{
}

ObWaitDataReadyRestoreTask::~ObWaitDataReadyRestoreTask()
{
}

int ObWaitDataReadyRestoreTask::init()
{
  int ret = OB_SUCCESS;
  ObIDagNet *dag_net = nullptr;
  ObCompleteRestoreDagNet *complete_dag_net = nullptr;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("wait data ready restore task init twice", K(ret));
  } else if (FALSE_IT(dag_net = this->get_dag()->get_dag_net())) {
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE_COMPLETE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), K(dag_net->get_type()));
  } else if (OB_ISNULL(complete_dag_net = static_cast<ObCompleteRestoreDagNet *>(dag_net))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("complete dag net should not be NULL", K(ret), KP(complete_dag_net));
  } else if (OB_FAIL(ObStorageHADagUtils::get_ls(sys_ls_id, ls_handle_))) {
    LOG_WARN("failed to get ls", K(ret), K(sys_ls_id));
  } else {
    ctx_ = complete_dag_net->get_complete_ctx();
    is_inited_ = true;
    LOG_INFO("succeed init wait data ready restore task", "task_id", ctx_->task_.task_id_,
        "dag_id", *ObCurTraceId::get_trace_id());
  }
  return ret;
}

int ObWaitDataReadyRestoreTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("wait data ready restore task do not init", K(ret));
  } else if (ctx_->is_failed()) {
    //do nothing
  } else if (OB_FAIL(wait_log_sync_())) {
    LOG_WARN("failed wait log sync", K(ret), KPC(ctx_));
  } else if (OB_FAIL(wait_log_replay_sync_())) {
    LOG_WARN("failed to wait log replay sync", K(ret), KPC(ctx_));
  } else if (OB_FAIL(check_all_tablet_ready_())) {
    LOG_WARN("failed to check all tablet ready", K(ret), KPC(ctx_));
  } else if (OB_FAIL(wait_log_replay_to_max_minor_end_scn_())) {
    LOG_WARN("failed to wait log replay to max minor end scn", K(ret), KPC(ctx_));
  } else if (OB_FAIL(update_ls_restore_status_wait_())) {
    LOG_WARN("failed to update ls migration wait", K(ret), KPC(ctx_));
  } else if (OB_FAIL(ObStorageHAUtils::check_disk_space())) {
    LOG_WARN("failed to check disk space", K(ret), KPC(ctx_));
  }

  if (OB_TMP_FAIL(record_server_event_())) {
    LOG_WARN("failed to record server event", K(tmp_ret), K(ret));
  }

  if (OB_FAIL(ret)) {
    if (OB_TMP_FAIL(ObStorageHADagUtils::deal_with_fo(ret, this->get_dag()))) {
      LOG_WARN("failed to deal with fo", K(ret), K(tmp_ret), KPC(ctx_));
    }
  }

  return ret;
}

int ObWaitDataReadyRestoreTask::get_wait_timeout_(int64_t &timeout)
{
  int ret = OB_SUCCESS;
  timeout = 10_min;
  omt::ObTenantConfigGuard tenant_config(TENANT_CONF(MTL_ID()));
  if (tenant_config.is_valid()) {
    timeout = tenant_config->_ls_migration_wait_completing_timeout;
  }
  return ret;
}

int ObWaitDataReadyRestoreTask::wait_log_sync_()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  bool is_log_sync = false;
  bool is_need_rebuild = false;
  palf::LSN last_end_lsn(0);
  palf::LSN current_end_lsn(0);
  ObTimeoutCtx timeout_ctx;
  int64_t timeout = 10_min;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("wait data ready restore task do not init", K(ret));
  } else if (OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KP(ls), KPC(ctx_));
  } else if (OB_FAIL(get_wait_timeout_(timeout))) {
    LOG_WARN("failed to get wait timeout", K(ret));
  } else if (OB_FAIL(init_timeout_ctx_(timeout, timeout_ctx))) {
    LOG_WARN("failed to init timeout ctx", K(ret));
  } else {
    const int64_t wait_replay_start_ts = ObTimeUtility::current_time();
    int64_t current_ts = 0;
    int64_t last_wait_replay_ts = ObTimeUtility::current_time();
    while (OB_SUCC(ret) && !is_log_sync) {
      if (timeout_ctx.is_timeouted()) {
        ret = OB_LOG_NOT_SYNC;
        LOG_WARN("already timeout", K(ret), KPC(ctx_));
      } else if (OB_FAIL(check_ls_and_task_status_(ls))) {
        LOG_WARN("failed to check ls and task status", K(ret), KPC(ctx_));
      } else if (OB_FAIL(ls->is_in_sync(is_log_sync, is_need_rebuild))) {
        LOG_WARN("failed to check is in sync", K(ret), KPC(ctx_));
      }

      if (OB_FAIL(ret)) {
      } else if (is_log_sync) {
        if (OB_FAIL(ls->get_end_lsn(log_sync_lsn_))) {
          LOG_WARN("failed to get end lsn", K(ret), KPC(ctx_));
        } else {
          const int64_t cost_ts = ObTimeUtility::current_time() - wait_replay_start_ts;
          LOG_INFO("log is sync, stop wait_log_sync", "task_id", ctx_->task_.task_id_, K(cost_ts));
        }
      } else if (is_need_rebuild) {
        const int64_t cost_ts = ObTimeUtility::current_time() - wait_replay_start_ts;
        ret = OB_LOG_NOT_SYNC;
        LOG_WARN("log is not sync", K(ret), KPC(ctx_), K(cost_ts));
      } else if (OB_FAIL(ls->get_end_lsn(current_end_lsn))) {
        LOG_WARN("failed to get end lsn", K(ret), KPC(ctx_));
      } else {
        bool is_timeout = false;
        if (REACH_THREAD_TIME_INTERVAL(60 * 1000 * 1000)) {
          LOG_INFO("log is not sync, retry next loop", "task_id", ctx_->task_.task_id_);
        }

        if (current_end_lsn == last_end_lsn) {
          current_ts = ObTimeUtility::current_time();
          if ((current_ts - last_wait_replay_ts) > timeout) {
            is_timeout = true;
          }

          if (is_timeout) {
            if (OB_FAIL(ctx_->set_result(OB_LOG_NOT_SYNC, true /*allow_retry*/, this->get_dag()->get_type()))) {
              LOG_WARN("failed to set result", K(ret), KPC(ctx_));
            } else {
              ret = OB_LOG_NOT_SYNC;
              LOG_WARN("failed to check log sync. timeout, stop restore task",
                  K(ret), K(*ctx_), K(timeout), K(wait_replay_start_ts),
                  K(current_ts), K(current_end_lsn));
            }
          }
        } else if (last_end_lsn > current_end_lsn) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("last end log lsn should not smaller than current end log lsn", K(ret),
              K(last_end_lsn), K(current_end_lsn));
        } else {
          last_end_lsn = current_end_lsn;
          last_wait_replay_ts = ObTimeUtility::current_time();
        }

        if (OB_SUCC(ret)) {
          ob_usleep(CHECK_CONDITION_INTERVAL);
        }
      }
    }
    if (OB_SUCC(ret) && !is_log_sync) {
      const int64_t cost_ts = ObTimeUtility::current_time() - wait_replay_start_ts;
      ret = OB_LOG_NOT_SYNC;
      LOG_WARN("log is not sync", K(ret), KPC(ctx_), K(cost_ts));
    }
  }
  return ret;
}

int ObWaitDataReadyRestoreTask::wait_log_replay_sync_()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  logservice::ObLogService *log_service = nullptr;
  logservice::ObLogReplayService *log_replay_service = nullptr;
  bool wait_log_replay_success = false;
  share::SCN current_replay_scn;
  share::SCN last_replay_scn;
  bool is_done = false;
  ObTimeoutCtx timeout_ctx;
  int64_t timeout = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("wait data ready restore task do not init", K(ret));
  } else if (OB_ISNULL(log_service = MTL(logservice::ObLogService*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log service should not be NULL", K(ret), KP(log_service));
  } else if (OB_ISNULL(log_replay_service = log_service->get_log_replay_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log replay service should not be NULL", K(ret), KP(log_replay_service));
  } else if (!ls_handle_.is_valid() || OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls handle is invalid or ls is null", K(ret), KP(ls));
  } else if (OB_FAIL(get_wait_timeout_(timeout))) {
    LOG_WARN("failed to get wait timeout", K(ret));
  } else if (OB_FAIL(init_timeout_ctx_(timeout, timeout_ctx))) {
    LOG_WARN("failed to init timeout ctx", K(ret));
  } else {
    const int64_t wait_replay_start_ts = ObTimeUtility::current_time();
    int64_t last_replay_ts = ObTimeUtility::current_time();
    int64_t current_ts = 0;
    while (OB_SUCC(ret) && !wait_log_replay_success) {
      if (timeout_ctx.is_timeouted()) {
        ret = OB_WAIT_REPLAY_TIMEOUT;
        LOG_WARN("already timeout", K(ret), KPC(ctx_));
      } else if (OB_FAIL(check_ls_and_task_status_(ls))) {
        LOG_WARN("failed to check ls and task status", K(ret), KPC(ctx_));
      } else if (OB_FAIL(log_replay_service->is_replay_done(ls->get_ls_id(), log_sync_lsn_, is_done))) {
        LOG_WARN("failed to check is replay done", K(ret), KPC(ls), K(log_sync_lsn_));
      } else if (is_done) {
        wait_log_replay_success = true;
        const int64_t cost_ts = ObTimeUtility::current_time() - wait_replay_start_ts;
        LOG_INFO("wait replay log sync success, stop wait", "task_id", ctx_->task_.task_id_, K(cost_ts));
      } else if (OB_FAIL(ls->get_max_decided_scn(current_replay_scn))) {
        LOG_WARN("failed to get current replay scn", K(ret), KPC(ctx_));
      }

      if (OB_SUCC(ret) && !wait_log_replay_success) {
        current_ts = ObTimeUtility::current_time();
        bool is_timeout = false;
        if (REACH_THREAD_TIME_INTERVAL(60 * 1000 * 1000)) {
          LOG_INFO("replay log is not sync, retry next loop", "task_id", ctx_->task_.task_id_,
              "current_replay_scn", current_replay_scn,
              "log_sync_lsn", log_sync_lsn_);
        }

        if (current_replay_scn == last_replay_scn) {
          if (current_ts - last_replay_ts > timeout) {
            is_timeout = true;
          }
          if (is_timeout) {
            if (OB_FAIL(ctx_->set_result(OB_WAIT_REPLAY_TIMEOUT, true /*allow_retry*/, this->get_dag()->get_type()))) {
              LOG_WARN("failed to set result", K(ret), KPC(ctx_));
            } else {
              ret = OB_WAIT_REPLAY_TIMEOUT;
              STORAGE_LOG(WARN, "failed to check log replay sync. timeout, stop restore task",
                  K(ret), K(*ctx_), K(timeout), K(wait_replay_start_ts),
                  K(current_ts), K(current_replay_scn));
            }
          }
        } else if (last_replay_scn > current_replay_scn) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("last replay scn should not smaller than current replay scn", K(ret),
              K(last_replay_scn), K(current_replay_scn));
        } else {
          last_replay_scn = current_replay_scn;
          last_replay_ts = current_ts;
        }

        if (OB_SUCC(ret)) {
          ob_usleep(CHECK_CONDITION_INTERVAL);
        }
      }
    }

    if (OB_SUCC(ret) && !wait_log_replay_success) {
      const int64_t cost_ts = ObTimeUtility::current_time() - wait_replay_start_ts;
      ret = OB_LOG_NOT_SYNC;
      LOG_WARN("log is not sync", K(ret), KPC(ctx_), K(cost_ts));
    }
  }
  return ret;
}

int ObWaitDataReadyRestoreTask::update_ls_restore_status_wait_()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObRestoreStatus restore_status(ObRestoreStatus::Status::RESTORE_WAIT);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("wait data ready restore task do not init", K(ret));
  } else if (OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be null", K(ret));
  } else if (OB_FAIL(ls->set_restore_status(restore_status))) {
    LOG_WARN("failed to get migration status", K(ret), KPC(ls));
  }
  return ret;
}

int ObWaitDataReadyRestoreTask::check_all_tablet_ready_()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  const int64_t check_all_tablet_start_ts = ObTimeUtility::current_time();
  const bool need_initial_state = false;
  const bool need_sorted_tablet_id = false;
  ObTimeoutCtx timeout_ctx;
  int64_t timeout = 10_min;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("wait data ready restore task do not init", K(ret));
  } else if (!ls_handle_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls handle is invalid", K(ret));
  } else if (OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be null", K(ret));
  } else if (OB_FAIL(get_wait_timeout_(timeout))) {
    LOG_WARN("failed to get wait timeout", K(ret));
  } else if (OB_FAIL(init_timeout_ctx_(timeout, timeout_ctx))) {
    LOG_WARN("failed to init timeout ctx", K(ret));
  } else {
    ObHALSTabletIDIterator iter(ls->get_ls_id(), need_initial_state, need_sorted_tablet_id);
    ObTabletID tablet_id;
    if (OB_FAIL(ls->get_tablet_svr()->build_tablet_iter(iter))) {
      LOG_WARN("failed to build tablet iter", K(ret), KPC(ctx_));
    } else {
      max_minor_end_scn_.set_min();
      while (OB_SUCC(ret)) {
        if (OB_FAIL(iter.get_next_tablet_id(tablet_id))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else {
            LOG_WARN("failed to get tablet id", K(ret));
          }
        } else if (OB_FAIL(check_tablet_ready_(tablet_id, ls, timeout))) {
          LOG_WARN("failed to check tablet ready", K(ret), K(tablet_id), KPC(ls));
        }
      }
      const int64_t cost_ts = ObTimeUtility::current_time() - check_all_tablet_start_ts;
      LOG_INFO("check all tablet ready finish", K(ret),
          "ls_id", ls->get_ls_id(), K_(max_minor_end_scn),
          "cost ts", cost_ts);
    }
  }
  return ret;
}

int ObWaitDataReadyRestoreTask::check_tablet_ready_(
    const common::ObTabletID &tablet_id,
    ObLS *ls,
    const int64_t timeout)
{
  int ret = OB_SUCCESS;
  const ObMDSGetTabletMode read_mode = ObMDSGetTabletMode::READ_WITHOUT_CHECK;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("wait data ready restore task do not init", K(ret));
  } else if (!tablet_id.is_valid() || OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check tablet ready get invalid argument", K(ret), K(tablet_id), KP(ls));
  } else {
    const int64_t wait_tablet_start_ts = ObTimeUtility::current_time();

    while (OB_SUCC(ret)) {
      ObTabletHandle tablet_handle;
      ObTablet *tablet = nullptr;

      if (OB_FAIL(check_ls_and_task_status_(ls))) {
        LOG_WARN("failed to check ls and task status", K(ret), KPC(ctx_));
      } else if (OB_FAIL(ls->get_tablet(tablet_id, tablet_handle, 0/*timeout_us*/, read_mode))) {
        if (OB_TABLET_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
        }
      } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet should not be NULL", K(ret), KP(tablet), K(tablet_handle), K(tablet_id));
      } else if (tablet->is_empty_shell()) {
        max_minor_end_scn_ = MAX(max_minor_end_scn_, tablet->get_tablet_meta().get_max_replayed_scn());
        break;
      } else if (tablet->get_tablet_meta().ha_status_.is_data_status_complete()) {
        ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
        if (OB_FAIL(tablet->fetch_table_store(table_store_wrapper))) {
          LOG_WARN("fail to fetch table store", K(ret));
        } else {
          const ObSSTableArray &minor_sstables = table_store_wrapper.get_member()->get_minor_sstables();
          if (minor_sstables.empty()) {
            max_minor_end_scn_ = MAX(max_minor_end_scn_, tablet->get_tablet_meta().get_max_replayed_scn());
          } else {
            max_minor_end_scn_ = MAX3(max_minor_end_scn_,
                                     minor_sstables.get_boundary_table(true)->get_end_scn(),
                                     tablet->get_tablet_meta().get_max_replayed_scn());
          }
        }
        break;
      } else {
        const int64_t current_ts = ObTimeUtility::current_time();
        if (REACH_THREAD_TIME_INTERVAL(60 * 1000 * 1000)) {
          LOG_INFO("tablet not ready, retry next loop", "task_id", ctx_->task_.task_id_,
              "tablet_id", tablet_id,
              "ha_status", tablet->get_tablet_meta().ha_status_,
              "wait_tablet_start_ts", wait_tablet_start_ts,
              "current_ts", current_ts);
        }

        if (current_ts - wait_tablet_start_ts < timeout) {
        } else {
          if (OB_FAIL(ctx_->set_result(OB_WAIT_TABLET_READY_TIMEOUT, true /*allow_retry*/, this->get_dag()->get_type()))) {
            LOG_WARN("failed to set result", K(ret), KPC(ctx_));
          } else {
            ret = OB_WAIT_TABLET_READY_TIMEOUT;
            STORAGE_LOG(WARN, "failed to check tablet ready, timeout, stop restore task",
                K(ret), K(*ctx_), KPC(tablet), K(current_ts), K(wait_tablet_start_ts));
          }
        }

        if (OB_SUCC(ret)) {
          ob_usleep(CHECK_CONDITION_INTERVAL);
        }
      }
    }
  }
  return ret;
}

int ObWaitDataReadyRestoreTask::wait_log_replay_to_max_minor_end_scn_()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  share::SCN current_replay_scn = share::SCN::min_scn();
  int64_t timeout = 0;
  ObTimeoutCtx timeout_ctx;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("wait data ready restore task do not init", K(ret));
  } else if (!ls_handle_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls handle is invalid", K(ret));
  } else if (OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KP(ls), KPC(ctx_));
  } else if (OB_FAIL(get_wait_timeout_(timeout))) {
    LOG_WARN("failed to get wait timeout", K(ret));
  } else if (OB_FAIL(init_timeout_ctx_(timeout, timeout_ctx))) {
    LOG_WARN("failed to init timeout ctx", K(ret));
  } else {
    const int64_t wait_replay_start_ts = ObTimeUtility::current_time();
    while (OB_SUCC(ret)) {
      if (timeout_ctx.is_timeouted()) {
        ret = OB_WAIT_REPLAY_TIMEOUT;
        LOG_WARN("already timeout", K(ret), KPC(ctx_));
        break;
      } else if (OB_FAIL(check_ls_and_task_status_(ls))) {
        LOG_WARN("failed to check ls and task status", K(ret), KPC(ctx_));
      } else if (OB_FAIL(ls->get_max_decided_scn(current_replay_scn))) {
        LOG_WARN("failed to get current replay log ts", K(ret), KPC(ctx_));
      } else if (current_replay_scn >= max_minor_end_scn_) {
        const int64_t cost_ts = ObTimeUtility::current_time() - wait_replay_start_ts;
        LOG_INFO("wait replay log ts push to max minor end scn success, stop wait", "task_id", ctx_->task_.task_id_,
            K(cost_ts), K(max_minor_end_scn_), K(current_replay_scn));
        break;
      } else {
        const int64_t current_ts = ObTimeUtility::current_time();
        if (REACH_THREAD_TIME_INTERVAL(60 * 1000 * 1000)) {
          LOG_INFO("ls wait replay to max minor sstable end log ts, retry next loop", "task_id", ctx_->task_.task_id_,
              "wait_replay_start_ts", wait_replay_start_ts,
              "current_ts", current_ts,
              "max_minor_end_scn", max_minor_end_scn_,
              "current_replay_scn", current_replay_scn);
        }

        if (current_ts - wait_replay_start_ts < timeout) {
        } else {
          if (OB_FAIL(ctx_->set_result(OB_WAIT_REPLAY_TIMEOUT, true /*allow_retry*/,
              this->get_dag()->get_type()))) {
            LOG_WARN("failed to set result", K(ret), KPC(ctx_));
          } else {
            ret = OB_WAIT_REPLAY_TIMEOUT;
            LOG_WARN("failed to wait replay to max minor end scn, timeout, stop restore task",
                K(ret), K(*ctx_), K(current_ts), K(wait_replay_start_ts));
          }
        }

        if (OB_SUCC(ret)) {
          ob_usleep(CHECK_CONDITION_INTERVAL);
        }
      }
    }
  }
  return ret;
}

int ObWaitDataReadyRestoreTask::check_ls_and_task_status_(ObLS *ls)
{
  int ret = OB_SUCCESS;
  int32_t result = OB_SUCCESS;
  ObIDagNet *dag_net = nullptr;

  if (OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check ls and task status get invalid argument", K(ret), KP(ls));
  } else if (ctx_->is_failed()) {
    ret = OB_CANCELED;
    STORAGE_LOG(WARN, "ls restore task is failed", K(ret), KPC(ctx_));
  } else if (ls->is_stopped()) {
    ret = OB_NOT_RUNNING;
    LOG_WARN("ls is not running, stop restore dag net", K(ret), KPC(ctx_));
  } else if (OB_ISNULL(this->get_dag())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag should not be nullptr", K(ret), KP(this->get_dag()));
  } else if (OB_ISNULL(dag_net = this->get_dag()->get_dag_net())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be nullptr", K(ret), KP(dag_net));
  } else if (dag_net->is_cancel()) {
    ret = OB_CANCELED;
    LOG_WARN("task is cancelled", K(ret), K(*this));
  } else if (OB_FAIL(ObStorageHAUtils::check_log_status(MTL_ID(), ls->get_ls_id(), result))) {
    LOG_WARN("failed to check log status", K(ret), KPC(ls));
  } else if (OB_SUCCESS != result) {
    LOG_INFO("can not replay log, it will retry", K(result));
    if (OB_FAIL(ctx_->set_result(result/*result*/, false/*need_retry*/, this->get_dag()->get_type()))) {
      LOG_WARN("failed to set result", K(ret), KPC(ctx_));
    } else {
      ret = result;
      LOG_WARN("log sync or replay error, need retry", K(ret), KPC(ls));
    }
  }
  return ret;
}

int ObWaitDataReadyRestoreTask::record_server_event_()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("wait data ready restore task do not init", K(ret));
  } else {
    SERVER_EVENT_ADD("storage_ha", "standby_restore_wait_data_ready_task",
        "task_id", ctx_->task_.task_id_,
        "task_type", ctx_->task_.type_,
        "src_info", ctx_->task_.src_info_);
  }
  return ret;
}

int ObWaitDataReadyRestoreTask::init_timeout_ctx_(const int64_t timeout, ObTimeoutCtx &timeout_ctx)
{
  int ret = OB_SUCCESS;
  if (timeout <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("timeout is invalid", K(ret), K(timeout));
  } else if (OB_FAIL(timeout_ctx.set_timeout(timeout))) {
    LOG_WARN("failed to set timeout", K(ret), K(timeout));
  }
  return ret;
}

/****************************ObFinishCompleteRestoreDag************************************/
ObFinishCompleteRestoreDag::ObFinishCompleteRestoreDag()
  : ObCompleteRestoreDag(share::ObDagType::DAG_TYPE_FINISH_COMPLETE_RESTORE),
    is_inited_(false)
{
}

ObFinishCompleteRestoreDag::~ObFinishCompleteRestoreDag()
{
}

int ObFinishCompleteRestoreDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  ObCompleteRestoreCtx *self_ctx = nullptr;
  ObCStringHelper helper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("finish complete restore dag do not init", K(ret));
  } else if (ObIHADagNetCtx::DagNetCtxType::RESTORE_COMPLETE != ha_dag_net_ctx_->get_dag_net_ctx_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ha dag net ctx type is unexpected", K(ret), KPC(ha_dag_net_ctx_));
  } else if (FALSE_IT(self_ctx = static_cast<ObCompleteRestoreCtx *>(ha_dag_net_ctx_))) {
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(databuff_printf(buf, buf_len,
                                        "ObFinishCompleteRestoreDag: task_id = %s, task_type = %s, src_info = %s",
                                        helper.convert(self_ctx->task_.task_id_),
                                        ObRestoreTaskType::get_str(self_ctx->task_.type_),
                                        helper.convert(self_ctx->task_.src_info_)))) {
    LOG_WARN("failed to fill dag key", K(ret), KPC(self_ctx));
  }
  return ret;
}

int ObFinishCompleteRestoreDag::create_first_task()
{
  int ret = OB_SUCCESS;
  ObFinishCompleteRestoreTask *task = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("finish complete restore dag do not init", K(ret));
  } else if (OB_FAIL(alloc_task(task))) {
    LOG_WARN("Fail to alloc task", K(ret));
  } else if (OB_FAIL(task->init())) {
    LOG_WARN("failed to init finish complete restore task", K(ret), KPC(ha_dag_net_ctx_));
  } else if (OB_FAIL(add_task(*task))) {
    LOG_WARN("Fail to add task", K(ret));
  } else {
    LOG_DEBUG("success to create first task", K(ret), KPC(this));
  }
  return ret;
}

int ObFinishCompleteRestoreDag::init(share::ObIDagNet *dag_net)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("finish complete restore dag init twice", K(ret));
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init finish complete restore dag get invalid argument", K(ret), KP(dag_net));
  } else if (OB_FAIL(prepare_ctx(dag_net))) {
    LOG_WARN("failed to prepare ctx", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

/****************************ObFinishCompleteRestoreTask************************************/
ObFinishCompleteRestoreTask::ObFinishCompleteRestoreTask()
  : ObITask(ObITaskType::TASK_TYPE_RESTORE_COMPLETE_FINISH),
    is_inited_(false),
    ctx_(nullptr)
{
}

ObFinishCompleteRestoreTask::~ObFinishCompleteRestoreTask()
{
}

int ObFinishCompleteRestoreTask::init()
{
  int ret = OB_SUCCESS;
  ObIDagNet *dag_net = nullptr;
  ObCompleteRestoreDagNet *complete_dag_net = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("finish complete restore task init twice", K(ret));
  } else if (FALSE_IT(dag_net = this->get_dag()->get_dag_net())) {
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE_COMPLETE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (FALSE_IT(complete_dag_net = static_cast<ObCompleteRestoreDagNet *>(dag_net))) {
  } else {
    ctx_ = complete_dag_net->get_complete_ctx();
    is_inited_ = true;
    LOG_INFO("succeed init finish complete restore task", "task_id", ctx_->task_.task_id_,
        "dag_id", *ObCurTraceId::get_trace_id());
  }
  return ret;
}

int ObFinishCompleteRestoreTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  FLOG_INFO("start do finish complete restore task", KPC(ctx_));
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("finish complete restore task do not init", K(ret));
  } else {
    if (ctx_->is_failed()) {
      bool allow_retry = false;
      if (OB_FAIL(ctx_->check_allow_retry_with_stop(allow_retry))) {
        LOG_ERROR("failed to check need retry", K(ret), K(*ctx_));
      } else if (allow_retry) {
        ctx_->reuse();
        if (OB_FAIL(generate_initial_complete_restore_dag_())) {
          LOG_WARN("failed to generate initial complete restore dag", K(ret), KPC(ctx_));
        }
      }
    }
    if (OB_FAIL(ret)) {
      const bool need_retry = false;
      if (OB_TMP_FAIL(ctx_->set_result(ret, need_retry, this->get_dag()->get_type()))) {
        LOG_WARN("failed to set result", K(ret), K(tmp_ret), KPC(ctx_));
      }
    }
  }

  if (OB_TMP_FAIL(record_server_event_())) {
    LOG_WARN("failed to record server event", K(tmp_ret), K(ret));
  }
  return ret;
}

int ObFinishCompleteRestoreTask::generate_initial_complete_restore_dag_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObInitialCompleteRestoreDag *initial_dag = nullptr;
  ObTenantDagScheduler *scheduler = nullptr;
  ObIDag *dag = nullptr;
  ObIDagNet *dag_net = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("finish complete restore task do not init", K(ret));
  } else if (OB_ISNULL(dag = this->get_dag()) || ObDagType::DAG_TYPE_FINISH_COMPLETE_RESTORE != dag->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag should not be NULL or type is unexpected", K(ret), KP(dag));
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", K(ret));
  } else if (OB_ISNULL(dag_net = dag->get_dag_net())
                || ObDagNetType::DAG_NET_TYPE_RESTORE_COMPLETE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL or type is unexpected", K(ret), KP(dag_net));
  } else {
    if (OB_FAIL(scheduler->alloc_dag(initial_dag))) {
      LOG_WARN("failed to alloc initial complete restore dag", K(ret));
    } else if (OB_FAIL(initial_dag->init(dag_net))) {
      LOG_WARN("failed to init initial complete restore dag", K(ret));
    } else if (OB_FAIL(this->get_dag()->add_child(*initial_dag))) {
      LOG_WARN("failed to add initial complete dag as child", K(ret), KPC(initial_dag));
    } else if (OB_FAIL(initial_dag->create_first_task())) {
      LOG_WARN("failed to create first task", K(ret));
    } else if (OB_FAIL(scheduler->add_dag(initial_dag))) {
      LOG_WARN("failed to add initial complete restore dag", K(ret), K(*initial_dag));
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_WARN("Fail to add task", K(ret));
        ret = OB_EAGAIN;
      }
    } else {
      LOG_INFO("start create initial complete restore dag", K(ret), K(*ctx_));
      initial_dag = nullptr;
    }

    if (OB_NOT_NULL(initial_dag) && OB_NOT_NULL(scheduler)) {
      scheduler->free_dag(*initial_dag);
      initial_dag = nullptr;
    }
  }
  return ret;
}

int ObFinishCompleteRestoreTask::record_server_event_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx should not be null", K(ret));
  } else {
    SERVER_EVENT_ADD("storage_ha", "finish_complete_restore_task",
        "task_id", ctx_->task_.task_id_,
        "task_type", ctx_->task_.type_,
        "src_info", ctx_->task_.src_info_);
  }
  return ret;
}

} // namespace restore
} // namespace oceanbase
