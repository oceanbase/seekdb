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

#include "ob_restore_dag_net.h"
#include "observer/ob_server.h"
#include "share/rc/ob_tenant_base.h"
#include "share/scheduler/ob_tenant_dag_scheduler.h"
#include "share/scheduler/ob_dag_warning_history_mgr.h"
#include "share/config/ob_server_config.h"
#include "lib/utility/ob_tracepoint.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "storage/tablet/ob_tablet_create_mds_helper.h"
#include "storage/high_availability/ob_storage_ha_utils.h"
#include "storage/ob_storage_schema_util.h"
#include "storage/tablet/ob_tablet_obj_load_helper.h"

namespace oceanbase
{
using namespace share;
namespace restore
{
using namespace storage;

ObRestoreDagNetCtx::ObRestoreDagNetCtx()
  : local_clog_checkpoint_scn_(SCN::min_scn()),
    task_(),
    allocator_("RestoreDagNet"),
    ha_table_info_mgr_(),
    tablet_group_mgr_(),
    src_ls_meta_package_(),
    sys_tablet_id_array_(),
    data_tablet_id_array_(),
    start_ts_(0),
    finish_ts_(0),
    tablet_simple_info_map_()
{
}

ObRestoreDagNetCtx::~ObRestoreDagNetCtx()
{
}

int ObRestoreDagNetCtx::fill_comment(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore dag net ctx is not valid", K(ret), KPC(this));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else {
    int64_t pos = 0;
    if (OB_FAIL(databuff_printf(buf, buf_len, pos, "task id = "))) {
      LOG_WARN("failed to print task id", K(ret));
    } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, task_.task_id_))) {
      LOG_WARN("failed to print task id", K(ret), K_(task));
    } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, ", local clog checkpoint scn = "))) {
      LOG_WARN("failed to print local clog checkpoint scn", K(ret));
    } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, local_clog_checkpoint_scn_))) {
      LOG_WARN("failed to print local clog checkpoint scn", K(ret));
    }
  }
  return ret;
}

bool ObRestoreDagNetCtx::is_valid() const
{
  return task_.is_valid();
}

void ObRestoreDagNetCtx::reset()
{
  local_clog_checkpoint_scn_.set_min();
  task_.reset();
  allocator_.reset();
  src_ls_meta_package_.reset();
  ha_table_info_mgr_.reuse();
  tablet_group_mgr_.reuse();
  src_ls_meta_package_.reset();
  sys_tablet_id_array_.reset();
  data_tablet_id_array_.reset();
  start_ts_ = 0;
  finish_ts_ = 0;
  ObIHADagNetCtx::reset();
  tablet_simple_info_map_.reuse();
}

void ObRestoreDagNetCtx::reuse()
{
  local_clog_checkpoint_scn_.set_min();
  src_ls_meta_package_.reset();
  ha_table_info_mgr_.reuse();
  tablet_group_mgr_.reuse();
  sys_tablet_id_array_.reset();
  data_tablet_id_array_.reset();
  ObIHADagNetCtx::reuse();
  tablet_simple_info_map_.reuse();
}

ObCopyTabletCtx::ObCopyTabletCtx()
  : tablet_id_(),
    tablet_handle_(),
    macro_block_reuse_mgr_(),
    extra_info_(),
    lock_(),
    status_(ObCopyTabletStatus::MAX_STATUS)
{
}

ObCopyTabletCtx::~ObCopyTabletCtx()
{
}

bool ObCopyTabletCtx::is_valid() const
{
  return tablet_id_.is_valid()
      && ObCopyTabletStatus::is_valid(status_)
      && ((ObCopyTabletStatus::TABLET_EXIST == status_ && tablet_handle_.is_valid())
          || ObCopyTabletStatus::TABLET_NOT_EXIST == status_);
}

void ObCopyTabletCtx::reset()
{
  tablet_id_.reset();
  tablet_handle_.reset();
  status_ = ObCopyTabletStatus::MAX_STATUS;
  extra_info_.reset();
}

int ObCopyTabletCtx::set_copy_tablet_status(const ObCopyTabletStatus::STATUS &status)
{
  int ret = OB_SUCCESS;
  if (!ObCopyTabletStatus::is_valid(status)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet src status is invalid", K(ret), K(status));
  } else {
    common::SpinWLockGuard guard(lock_);
    status_ = status;
  }
  return ret;
}

int ObCopyTabletCtx::get_copy_tablet_status(ObCopyTabletStatus::STATUS &status) const
{
  int ret = OB_SUCCESS;
  status = ObCopyTabletStatus::MAX_STATUS;
  common::SpinRLockGuard guard(lock_);
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("copy tablet ctx is invalid", K(ret), KPC(this));
  } else {
    status = status_;
  }
  return ret;
}

int ObCopyTabletCtx::get_copy_tablet_record_extra_info(ObCopyTabletRecordExtraInfo *&extra_info)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("copy tablet ctx is invalid", K(ret), KPC(this));
  } else {
    extra_info = &extra_info_;
  }
  return ret;
}

/*********************ObRestoreDagNetInitParam*********************/
ObRestoreDagNetInitParam::ObRestoreDagNetInitParam()
  : task_(),
    handler_(nullptr),
    bandwidth_throttle_(nullptr)
{
}

bool ObRestoreDagNetInitParam::is_valid() const
{
  return task_.is_valid() && OB_NOT_NULL(handler_) && OB_NOT_NULL(bandwidth_throttle_);
}

/*********************ObRestoreDagNet****************************/
ObRestoreDagNet::ObRestoreDagNet()
  : share::ObIDagNet(ObDagNetType::DAG_NET_TYPE_RESTORE),
    is_inited_(false),
    ctx_(nullptr),
    bandwidth_throttle_(nullptr),
    helper_(nullptr),
    handler_(nullptr)
{
}

ObRestoreDagNet::~ObRestoreDagNet()
{
  free_restore_ctx_();
  free_restore_helper_();
}

int ObRestoreDagNet::alloc_restore_ctx_()
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;

  if (OB_NOT_NULL(ctx_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("restore dag net ctx init twice", K(ret), KPC(ctx_));
  } else if (FALSE_IT(buf = mtl_malloc(sizeof(ObRestoreDagNetCtx), "RestoreCtx"))) {
  } else if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc memory", K(ret), KP(buf));
  } else if (FALSE_IT(ctx_ = new (buf) ObRestoreDagNetCtx())) {
  }
  return ret;
}

void ObRestoreDagNet::free_restore_ctx_()
{
  if (OB_NOT_NULL(ctx_)) {
    ctx_->~ObRestoreDagNetCtx();
    mtl_free(ctx_);
    ctx_ = nullptr;
  }
}

int ObRestoreDagNet::alloc_restore_helper_()
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = nullptr;
  if (OB_NOT_NULL(helper_) || OB_ISNULL(ctx_) || !ctx_->is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore helper should not be null and ctx should not be null and ctx should be valid",
                K(ret), KPC(helper_), KPC(ctx_));
  } else if (OB_ISNULL(ls_service = MTL(ObLSService*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service should not be null", K(ret), KP(ls_service));
  } else {
    void *buf = nullptr;
    const ObRestoreTaskType::TYPE task_type = ctx_->task_.type_;
    if (ObRestoreTaskType::STANDBY_RESTORE_TASK == task_type) {
      ObStandbyRestoreHelper *standby_helper = nullptr;
      if (OB_ISNULL(buf = ctx_->allocator_.alloc(sizeof(ObStandbyRestoreHelper)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc memory for standby restore helper", K(ret));
      } else if (FALSE_IT(standby_helper = new (buf) ObStandbyRestoreHelper())) {
      } else {
        if (OB_FAIL(standby_helper->init(ctx_->task_.src_info_, ctx_->task_.task_id_, bandwidth_throttle_))) {
          LOG_WARN("failed to init standby restore helper", K(ret), KPC(ctx_));
          standby_helper->destroy();
          ctx_->allocator_.free(standby_helper);
          standby_helper = nullptr;
        } else {
          helper_ = standby_helper;
        }
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected restore task type", K(ret), K(task_type), KPC(ctx_));
    }
  }
  return ret;
}

void ObRestoreDagNet::free_restore_helper_()
{
  if (OB_NOT_NULL(helper_) && OB_NOT_NULL(ctx_)) {
    helper_->destroy();
    ctx_->allocator_.free(helper_);
    helper_ = nullptr;
  }
}

int ObRestoreDagNet::init_by_param(const share::ObIDagInitParam *param)
{
  int ret = OB_SUCCESS;
  const ObRestoreDagNetInitParam *init_param = nullptr;
  const int64_t MAX_BUCKET_NUM = 8192;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("restore dag net is init twice", K(ret));
  } else if (OB_ISNULL(param) || !param->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("param is null or invalid", K(ret));
  } else if (FALSE_IT(init_param = static_cast<const ObRestoreDagNetInitParam *>(param))) {
  } else if (OB_FAIL(alloc_restore_ctx_())) {
    LOG_WARN("failed to alloc restore ctx", K(ret));
  } else if (OB_FAIL(this->set_dag_id(init_param->task_.task_id_))) {
    LOG_WARN("failed to set dag id", K(ret), KPC(init_param));
  } else if (OB_FAIL(ctx_->ha_table_info_mgr_.init())) {
    LOG_WARN("failed to init ha table info mgr", K(ret), KPC(init_param));
  } else if (OB_FAIL(ctx_->tablet_group_mgr_.init())) {
    LOG_WARN("failed to init tablet group mgr", K(ret), KPC(init_param));
  } else if (OB_FAIL(ctx_->tablet_simple_info_map_.create(MAX_BUCKET_NUM, "SHATaskBucket", "SHATaskNode", MTL_ID()))) {
    LOG_WARN("failed to create tablet simple info map", K(ret), KPC(init_param));
  } else {
    ctx_->task_ = init_param->task_;
    handler_ = init_param->handler_;
    bandwidth_throttle_ = init_param->bandwidth_throttle_;
    if (OB_FAIL(alloc_restore_helper_())) {
      LOG_WARN("failed to alloc restore helper", K(ret), KPC(init_param));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

bool ObRestoreDagNet::is_valid() const
{
  return is_inited_ && nullptr != ctx_ && ctx_->is_valid() && OB_NOT_NULL(helper_)
            && OB_NOT_NULL(handler_) && OB_NOT_NULL(bandwidth_throttle_);
}

int ObRestoreDagNet::start_running()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore dag net is not init", K(ret));
  } else if (OB_FAIL(start_running_for_restore_())) {
    LOG_WARN("failed to start restore dag net", K(ret));
  }
  return ret;
}

int ObRestoreDagNet::start_running_for_restore_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObInitialRestoreDag *initial_restore_dag = nullptr;
  ObTenantDagScheduler *scheduler = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore dag net do not init", K(ret));
  } else if (FALSE_IT(ctx_->start_ts_ = ObTimeUtil::current_time())) {
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", K(ret));
  } else if (OB_FAIL(scheduler->alloc_dag(initial_restore_dag))) {
    LOG_WARN("failed to alloc initial restore dag", K(ret));
  } else if (OB_FAIL(initial_restore_dag->init(this))) {
    LOG_WARN("failed to init initial restore dag", K(ret));
  } else if (OB_FAIL(add_dag_into_dag_net(*initial_restore_dag))) {
    LOG_WARN("failed to add initial restore dag into dag net", K(ret));
  } else if (OB_FAIL(initial_restore_dag->create_first_task())) {
    LOG_WARN("failed to create first task", K(ret));
  } else if (OB_FAIL(scheduler->add_dag(initial_restore_dag))) {
    LOG_WARN("failed to add initial restore dag", K(ret), K(*initial_restore_dag));
    if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
      LOG_WARN("Fail to add task", K(ret));
      ret = OB_EAGAIN;
    }
  } else {
    initial_restore_dag = nullptr;
  }

  if (OB_NOT_NULL(initial_restore_dag) && OB_NOT_NULL(scheduler)) {
    initial_restore_dag->reset_children();
    if (OB_TMP_FAIL(erase_dag_from_dag_net(*initial_restore_dag))) {
      LOG_WARN("failed to erase dag from dag net", K(tmp_ret), KPC(initial_restore_dag));
    }
    scheduler->free_dag(*initial_restore_dag);
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(ctx_)) {
    const bool need_retry = false;
    if (OB_TMP_FAIL(ctx_->set_result(ret, need_retry))) {
      LOG_ERROR("failed to set migration ctx result", K(ret), K(tmp_ret), K(ctx_));
    }
  }

  return ret;
}

bool ObRestoreDagNet::operator == (const share::ObIDagNet &other) const
{
  bool is_same = true;
  if (this == &other) {
    // same
  } else if (this->get_type() != other.get_type()) {
    is_same = false;
  } else {
    const ObRestoreDagNet &other_restore_dag = static_cast<const ObRestoreDagNet &>(other);
    if (OB_ISNULL(other_restore_dag.ctx_) || OB_ISNULL(ctx_)) {
      is_same = false;
      LOG_ERROR_RET(OB_INVALID_ARGUMENT, "restore ctx is NULL", KPC(ctx_), KPC(other_restore_dag.ctx_));
    } else if (ctx_->task_.task_id_ != other_restore_dag.ctx_->task_.task_id_) {
      is_same = false;
    }
  }
  return is_same;
}

uint64_t ObRestoreDagNet::hash() const
{
  uint64_t hash_value = 0;
  if (OB_ISNULL(ctx_)) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "restore ctx is NULL", KPC(ctx_));
  } else {
    hash_value = ctx_->task_.task_id_.hash();
  }
  return hash_value;
}

int ObRestoreDagNet::fill_comment(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore dag net do not init", K(ret));
  } else if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx should not be NULL", K(ret), KPC(ctx_));
  } else if (OB_FAIL(ctx_->fill_comment(buf, buf_len))) {
    LOG_WARN("failed to fill comment", K(ret), KPC(ctx_));
  }
  return ret;
}

int ObRestoreDagNet::fill_dag_net_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  char task_id_str[OB_MAX_TRACE_ID_BUFFER_SIZE] = { 0 };
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore dag net do not init", K(ret));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_UNLIKELY(0 > ctx_->task_.task_id_.to_string(task_id_str, OB_MAX_TRACE_ID_BUFFER_SIZE))) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_WARN("failed to get trace id string", K(ret), KPC(ctx_));
  } else {
    int64_t pos = 0;
    ret = databuff_printf(buf, buf_len, pos, "ObRestoreDagNet: task_id=");
    OB_SUCCESS != ret ? : ret = databuff_printf(buf, buf_len, pos, "%s", task_id_str);
    OB_SUCCESS != ret ? : ret = databuff_printf(buf, buf_len, pos, ", task_type=");
    OB_SUCCESS != ret ? : ret = databuff_printf(buf, buf_len, pos, "%s",
        ObRestoreTaskType::get_str(ctx_->task_.type_));
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to fill dag net key", K(ret), KPC(ctx_));
    }
  }
  return ret;
}

int ObRestoreDagNet::clear_dag_net_ctx()
{
  int ret = OB_SUCCESS;
  int32_t result = OB_SUCCESS;
  LOG_INFO("start clear dag net ctx", KPC(ctx_));

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore dag net do not init", K(ret));
  } else {
    if (OB_FAIL(ctx_->get_result(result))) {
      LOG_WARN("failed to get restore ctx result", K(ret), KPC(ctx_));
    } else if (OB_FAIL(handler_->set_result(result))) {
      LOG_WARN("failed to set result", K(ret), K(result));
    }
    ctx_->finish_ts_ = ObTimeUtil::current_time();
    const int64_t cost_ts = ctx_->finish_ts_ - ctx_->start_ts_;
    FLOG_INFO("finish restore dag net", "task_id", ctx_->task_.task_id_, "task_type", ctx_->task_.type_, K(cost_ts));
    handler_->set_dag_net_cleared();
  }
  return ret;
}

int ObRestoreDagNet::deal_with_cancel()
{
  int ret = OB_SUCCESS;
  const int32_t result = OB_CANCELED;
  const bool need_retry = false;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore dag net do not init", K(ret));
  } else if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx should not be NULL", K(ret));
  } else if (OB_FAIL(ctx_->set_result(result, need_retry))) {
    LOG_WARN("failed to set cancel result", K(ret), KPC(ctx_));
  }
  return ret;
}

/**********************ObRestoreDag***************************/
ObRestoreDag::ObRestoreDag(const share::ObDagType::ObDagTypeEnum &dag_type)
  : ObStorageHADag(dag_type)
{
}

ObRestoreDag::~ObRestoreDag()
{
}

int ObRestoreDag::fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const
{
  int ret = OB_SUCCESS;
  ObRestoreDagNetCtx *ctx = nullptr;

  if (OB_ISNULL(ctx = get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore dag ctx should not be NULL", K(ret), KP(ctx));
  } else {
    ObCStringHelper helper;
    if (OB_FAIL(ADD_DAG_WARN_INFO_PARAM(out_param, allocator, get_type(),
                                            share::ObLSID::SYS_LS_ID,
                                            static_cast<int64_t>(ctx->task_.type_),
                                            "task_id", helper.convert(ctx->task_.task_id_)))) {
      LOG_WARN("failed to fill info param", K(ret), KPC(ctx));
    }
  }
  return ret;
}

int ObRestoreDag::prepare_ctx(share::ObIDagNet *dag_net)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(dag_net)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else {
    ha_dag_net_ctx_ = static_cast<ObRestoreDagNet *>(dag_net)->get_ctx();
  }
  return ret;
}

/**********************ObInitialRestoreDag***************************/
ObInitialRestoreDag::ObInitialRestoreDag()
  : ObRestoreDag(share::ObDagType::DAG_TYPE_INITIAL_LS_RESTORE),
    is_inited_(false)
{
}

ObInitialRestoreDag::~ObInitialRestoreDag()
{
}

bool ObInitialRestoreDag::operator == (const share::ObIDag &other) const
{
  bool is_same = true;
  if (this == &other) {
    // same
  } else if (get_type() != other.get_type()) {
    is_same = false;
  } else {
    const ObInitialRestoreDag &other_dag = static_cast<const ObInitialRestoreDag&>(other);
    ObRestoreDagNetCtx *ctx = get_ctx();
    ObRestoreDagNetCtx *other_ctx = other_dag.get_ctx();
    if (OB_ISNULL(ctx) || OB_ISNULL(other_ctx)) {
      is_same = false;
    } else if (ctx->task_.task_id_ != other_ctx->task_.task_id_) {
      is_same = false;
    }
  }
  return is_same;
}

uint64_t ObInitialRestoreDag::hash() const
{
  uint64_t hash_value = 0;
  ObRestoreDagNetCtx *ctx = get_ctx();
  if (OB_ISNULL(ctx)) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "restore dag ctx should not be NULL", KPC(ctx));
  } else {
    hash_value = ctx->task_.task_id_.hash();
    ObDagType::ObDagTypeEnum dag_type = get_type();
    hash_value = common::murmurhash(&dag_type, sizeof(dag_type), hash_value);
  }
  return hash_value;
}

int ObInitialRestoreDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  ObRestoreDagNetCtx *ctx = get_ctx();
  ObCStringHelper helper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore dag net do not init", K(ret));
  } else if (OB_ISNULL(ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore dag ctx should not be null", K(ret), KPC(ctx));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(databuff_printf(buf, buf_len, "ObInitialRestoreDag task_id=%s, task_type=%s",
                                        helper.convert(ctx->task_.task_id_),
                                        ObRestoreTaskType::get_str(ctx->task_.type_)))) {
    LOG_WARN("failed to fill dag key", K(ret), KPC(ctx));
  }
  return ret;
}

int ObInitialRestoreDag::init(share::ObIDagNet *dag_net)
{
  int ret = OB_SUCCESS;
  ObRestoreDagNet *restore_dag_net = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("initial restore dag init twice", K(ret));
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet*>(dag_net))) {
  } else if (FALSE_IT(ha_dag_net_ctx_ = restore_dag_net->get_ctx())) {
  } else if (OB_ISNULL(ha_dag_net_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ha_dag_net_ctx_));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObInitialRestoreDag::create_first_task()
{
  int ret = OB_SUCCESS;
  ObInitialRestoreTask *task = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("initial restore dag do not init", K(ret));
  } else if (OB_FAIL(alloc_task(task))) {
    LOG_WARN("Fail to alloc task", K(ret));
  } else if (OB_FAIL(task->init())) {
    LOG_WARN("failed to init initial restore task", K(ret), KPC(ha_dag_net_ctx_));
  } else if (OB_FAIL(add_task(*task))) {
    LOG_WARN("Fail to add task", K(ret));
  } else {
    LOG_DEBUG("success to create first task", K(ret), KPC(this));
  }
  return ret;
}

/**********************ObInitialRestoreTask***************************/
ObInitialRestoreTask::ObInitialRestoreTask()
  : share::ObITask(share::ObITask::TASK_TYPE_MIGRATE_PREPARE),
    is_inited_(false),
    ctx_(nullptr),
    dag_net_(nullptr)
{
}

ObInitialRestoreTask::~ObInitialRestoreTask()
{
}

int ObInitialRestoreTask::init()
{
  int ret = OB_SUCCESS;
  ObIDagNet *dag_net = nullptr;
  ObRestoreDagNet *restore_dag_net = nullptr;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("initial restore task init twice", K(ret));
  } else if (FALSE_IT(dag_net = this->get_dag()->get_dag_net())) {
  } else if (OB_ISNULL(dag_net) || ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL or dag net type is unexpected", K(ret), KP(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet *>(dag_net))) {
  } else if (OB_ISNULL(ctx_ = restore_dag_net->get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ctx_));
  } else {
    ctx_->reuse();
    dag_net_ = dag_net;
    is_inited_ = true;
  }
  return ret;
}

int ObInitialRestoreTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("initial restore task do not init", K(ret));
  } else if (OB_FAIL(generate_restore_dags_())) {
    LOG_WARN("failed to generate restore dags", K(ret), KPC(ctx_));
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

int ObInitialRestoreTask::generate_restore_dags_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObStartRestoreDag *start_restore_dag = nullptr;
  ObRestoreFinishDag *finish_restore_dag = nullptr;
  ObTenantDagScheduler *scheduler = nullptr;
  ObInitialRestoreDag *initial_restore_dag = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("initial restore task do not init", K(ret));
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", K(ret));
  } else if (OB_ISNULL(initial_restore_dag = static_cast<ObInitialRestoreDag *>(this->get_dag()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("initial restore dag should not be NULL", K(ret), KP(initial_restore_dag));
  } else if (OB_FAIL(scheduler->alloc_dag(start_restore_dag))) {
    LOG_WARN("failed to alloc start restore dag", K(ret));
  } else if (OB_FAIL(scheduler->alloc_dag(finish_restore_dag))) {
    LOG_WARN("failed to alloc finish restore dag", K(ret));
  } else if (OB_FAIL(start_restore_dag->init(dag_net_))) {
    LOG_WARN("failed to init start restore dag", K(ret));
  } else if (OB_FAIL(finish_restore_dag->init(dag_net_))) {
    LOG_WARN("failed to init finish restore dag", K(ret));
  } else if (OB_FAIL(this->get_dag()->add_child(*start_restore_dag))) {
    LOG_WARN("failed to add start restore dag as child", K(ret), KPC(start_restore_dag));
  } else if (OB_FAIL(start_restore_dag->create_first_task())) {
    LOG_WARN("failed to create first task for start restore dag", K(ret));
  } else if (OB_FAIL(start_restore_dag->add_child(*finish_restore_dag))) {
    LOG_WARN("failed to add finish restore dag as child", K(ret));
  } else if (OB_FAIL(finish_restore_dag->create_first_task())) {
    LOG_WARN("failed to create first task for finish restore dag", K(ret));
  } else if (OB_FAIL(scheduler->add_dag(finish_restore_dag))) {
    LOG_WARN("failed to add restore finish dag", K(ret), K(*finish_restore_dag));
    if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_WARN("Fail to add task", K(ret));
        ret = OB_EAGAIN;
    }
  } else if (OB_FAIL(scheduler->add_dag(start_restore_dag))) {
    LOG_WARN("failed to add start restore dag", K(ret), K(*start_restore_dag));
    if (OB_TMP_FAIL(scheduler->cancel_dag(finish_restore_dag))) {
      LOG_WARN("failed to cancel ha dag", K(tmp_ret), KPC(finish_restore_dag));
    } else {
      finish_restore_dag = nullptr;
    }
    if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
      LOG_WARN("Fail to add task", K(ret));
      ret = OB_EAGAIN;
    }
  } else {
    LOG_INFO( "succeed to schedule restore start dag", K(*start_restore_dag));
  }

  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(scheduler) && OB_NOT_NULL(finish_restore_dag)) {
      scheduler->free_dag(*finish_restore_dag);
      finish_restore_dag = nullptr;
    }
    if (OB_NOT_NULL(scheduler) && OB_NOT_NULL(start_restore_dag)) {
      scheduler->free_dag(*start_restore_dag);
      start_restore_dag = nullptr;
    }
    const bool need_retry = true;
    if (OB_TMP_FAIL(ctx_->set_result(ret, need_retry, this->get_dag()->get_type()))) {
      LOG_WARN("failed to set restore ctx result", K(ret), K(tmp_ret), KPC(ctx_));
    }
  }
  return ret;
}

int ObInitialRestoreTask::record_server_event_()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("initial restore task do not init", K(ret));
  } else {
    SERVER_EVENT_ADD("storage_ha", "standby_restore_task",
        "task_id", ctx_->task_.task_id_,
        "task_type", ctx_->task_.type_,
        "src_addr", ctx_->task_.src_info_);
  }
  return ret;
}

/**************************ObStartRestoreDag***************************/
ObStartRestoreDag::ObStartRestoreDag()
  : ObRestoreDag(share::ObDagType::DAG_TYPE_START_LS_RESTORE),
    is_inited_(false)
{
}

ObStartRestoreDag::~ObStartRestoreDag()
{
}

bool ObStartRestoreDag::operator == (const share::ObIDag &other) const
{
  bool is_same = true;
  if (this == &other) {
    // same
  } else if (get_type() != other.get_type()) {
    is_same = false;
  } else {
    const ObStartRestoreDag &other_dag = static_cast<const ObStartRestoreDag&>(other);
    ObRestoreDagNetCtx *ctx = get_ctx();
    ObRestoreDagNetCtx *other_ctx = other_dag.get_ctx();
    if (OB_NOT_NULL(ctx) || OB_NOT_NULL(other_ctx)) {
      is_same = false;
    } else if (ctx->task_.task_id_ != other_ctx->task_.task_id_) {
      is_same = false;
    }
  }
  return is_same;
}

uint64_t ObStartRestoreDag::hash() const
{
  uint64_t hash_value = 0;
  ObRestoreDagNetCtx *ctx = get_ctx();
  if (OB_ISNULL(ctx)) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "restore dag ctx should not be NULL", KPC(ctx));
  } else {
    hash_value = ctx->task_.task_id_.hash();
    ObDagType::ObDagTypeEnum dag_type = get_type();
    hash_value = common::murmurhash(&dag_type, sizeof(dag_type), hash_value);
  }
  return hash_value;
}

int ObStartRestoreDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  ObRestoreDagNetCtx *ctx = get_ctx();
  ObCStringHelper helper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore dag net do not init", K(ret));
  } else if (OB_ISNULL(ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore dag ctx should not be null", K(ret), KPC(ctx));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(databuff_printf(buf, buf_len, "ObStartRestoreDag task_id=%s, task_type=%s",
                                        helper.convert(ctx->task_.task_id_),
                                        ObRestoreTaskType::get_str(ctx->task_.type_)))) {
    LOG_WARN("failed to fill dag key", K(ret), KPC(ctx));
  }
  return ret;
}

int ObStartRestoreDag::create_first_task()
{
  ObStartRestoreTask *task = nullptr;
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("start restore dag do not init", K(ret));
  } else if (OB_FAIL(alloc_task(task))) {
    LOG_WARN("failed to alloc start restore task", K(ret));
  } else if (OB_FAIL(task->init())) {
    LOG_WARN("failed to init start restore task", K(ret));
  } else if (OB_FAIL(add_task(*task))) {
    LOG_WARN("failed to add task", K(ret));
  } else {
    LOG_DEBUG("success to create first task", K(ret), KPC(this));
  }
  return ret;
}

int ObStartRestoreDag::init(share::ObIDagNet *dag_net)
{
  int ret = OB_SUCCESS;
  ObRestoreDagNet *restore_dag_net = nullptr;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("start restore dag init twice", K(ret));
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet *>(dag_net))) {
  } else if (FALSE_IT(ha_dag_net_ctx_ = restore_dag_net->get_ctx())) {
  } else if (OB_ISNULL(ha_dag_net_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ha_dag_net_ctx_));
  } else {
    is_inited_ = true;
  }
  return ret;
}

/**************************ObStartRestoreTask***************************/
ObStartRestoreTask::ObStartRestoreTask()
  : share::ObITask(share::ObITask::TASK_TYPE_MIGRATE_PREPARE),
    is_inited_(false),
    ctx_(nullptr),
    bandwidth_throttle_(nullptr),
    allocator_("StartRestore"),
    helper_(nullptr),
    dag_net_(nullptr)
{
}

ObStartRestoreTask::~ObStartRestoreTask()
{
  if (OB_NOT_NULL(helper_)) {
    helper_->destroy();
    helper_ = nullptr;
  }
}

int ObStartRestoreTask::init()
{
  int ret = OB_SUCCESS;
  ObIDagNet *dag_net = nullptr;
  ObRestoreDagNet *restore_dag_net = nullptr;
  ObIRestoreHelper *proto_helper = nullptr;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("start restore task init twice", K(ret));
  } else if (FALSE_IT(dag_net = this->get_dag()->get_dag_net())) {
  } else if (OB_ISNULL(dag_net) || ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL or dag net type is unexpected", K(ret), KP(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet *>(dag_net))) {
  } else if (OB_ISNULL(ctx_ = restore_dag_net->get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ctx_));
  } else if (OB_ISNULL(proto_helper = restore_dag_net->get_helper())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore helper should not be NULL", K(ret), KP(proto_helper));
  } else if (OB_ISNULL(bandwidth_throttle_ = restore_dag_net->get_bandwidth_throttle())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("bandwidth throttle should not be NULL", K(ret), KP(bandwidth_throttle_));
  } else if (OB_FAIL(proto_helper->copy_for_task(allocator_, helper_))) {
    LOG_WARN("failed to copy task helper", K(ret), KP(proto_helper));
  } else if (OB_ISNULL(helper_) || !helper_->is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task helper should not be null or invalid", K(ret), KP(helper_));
  } else {
    dag_net_ = dag_net;
    is_inited_ = true;
    LOG_INFO("succeed init start restore task",
        "dag_id", *ObCurTraceId::get_trace_id(), "dag_net_id", ctx_->task_.task_id_);
  }
  return ret;
}

int ObStartRestoreTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("start restore task do not init", K(ret));
  } else if (ctx_->is_failed()) {
    //do nothing
  } else if (OB_FAIL(check_restore_precondition_())) {
    LOG_WARN("failed to check disk space enough", K(ret));
  } else if (OB_FAIL(build_ls_())) {
    LOG_WARN("failed to build ls", K(ret), KPC(ctx_));
  } else if (OB_FAIL(generate_tablets_restore_dag_())) {
    LOG_WARN("failed to generate tablets restore dag", K(ret), KPC(ctx_));
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

int ObStartRestoreTask::check_restore_precondition_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(helper_) || !helper_->is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("helper should not be NULL or invalid", K(ret), KPC(helper_));
  } else if (OB_FAIL(helper_->check_restore_precondition())) {
    LOG_WARN("failed to check disk space enough", K(ret));
  }
  return ret;
}

int ObStartRestoreTask::update_ls_()
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  ObLSService *ls_service = nullptr;
  share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("start restore task do not init", K(ret));
  } else if (OB_ISNULL(ls_service = MTL(ObLSService*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObLSService from MTL", K(ret));
  } else if (OB_FAIL(ObStorageHADagUtils::get_ls(ls_id, ls_handle))) {
    LOG_WARN("failed to get ls", K(ret), K(ls_id));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KP(ls));
  } else {
    ObLSLockGuard lock_ls(ls);
    if (OB_FAIL(ls->update_ls_meta(false/*update_restore_status*/, ctx_->src_ls_meta_package_.ls_meta_))) {
      LOG_WARN("failed to update ls meta", K(ret), KPC(ctx_));
    } else if (OB_FAIL(ls->advance_base_info(ctx_->src_ls_meta_package_.palf_meta_, false/*is_rebuild*/))) {
      LOG_WARN("failed to advance base lsn for migration", K(ret), KPC(ctx_));
    } else {
      ctx_->local_clog_checkpoint_scn_ = ctx_->src_ls_meta_package_.ls_meta_.get_clog_checkpoint_scn();
      LOG_INFO("succeed to update ls", K(ctx_->src_ls_meta_package_.ls_meta_));
    }
  }
  return ret;
}

int ObStartRestoreTask::generate_tablets_restore_dag_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObSysTabletsRestoreDag *sys_tablets_dag = nullptr;
  ObDataTabletsRestoreDag *data_tablets_dag = nullptr;
  ObTenantDagScheduler *scheduler = nullptr;
  ObStartRestoreDag *start_restore_dag = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("start restore task do not init", K(ret));
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", K(ret));
  } else if (OB_ISNULL(start_restore_dag = static_cast<ObStartRestoreDag *>(this->get_dag()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("start restore dag should not be NULL", K(ret), KP(start_restore_dag));
  } else {
    if (OB_FAIL(scheduler->alloc_dag(sys_tablets_dag))) {
      LOG_WARN("failed to alloc sys tablets restore dag", K(ret));
    } else if (OB_FAIL(sys_tablets_dag->init(dag_net_))) {
      LOG_WARN("failed to init sys tablets restore dag", K(ret));
    } else if (OB_FAIL(this->get_dag()->add_child(*sys_tablets_dag))) {
      LOG_WARN("failed to add sys tablets dag as child", K(ret), KPC(sys_tablets_dag));
    } else if (OB_FAIL(sys_tablets_dag->create_first_task())) {
      LOG_WARN("failed to create first task for sys tablets dag", K(ret));
    } else if (OB_FAIL(scheduler->alloc_dag(data_tablets_dag))) {
      LOG_WARN("failed to alloc data tablets restore dag", K(ret));
    } else if (OB_FAIL(data_tablets_dag->init(dag_net_))) {
      LOG_WARN("failed to init data tablets restore dag", K(ret));
    } else if (OB_FAIL(sys_tablets_dag->add_child(*data_tablets_dag))) {
      LOG_WARN("failed to add data tablets dag as child", K(ret), KPC(data_tablets_dag));
    } else if (OB_FAIL(data_tablets_dag->create_first_task())) {
      LOG_WARN("failed to create first task for data tablets dag", K(ret));
    } else if (OB_FAIL(scheduler->add_dag(sys_tablets_dag))) {
      LOG_WARN("failed to add sys tablets dag", K(ret), KPC(sys_tablets_dag));
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_WARN("Fail to add task", K(ret));
        ret = OB_EAGAIN;
      }
    } else if (OB_FAIL(scheduler->add_dag(data_tablets_dag))) {
      LOG_WARN("failed to add data tablets dag", K(ret), KPC(data_tablets_dag));
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_WARN("Fail to add task", K(ret));
        ret = OB_EAGAIN;
      }

      if (OB_TMP_FAIL(scheduler->cancel_dag(sys_tablets_dag))) {
        LOG_WARN("failed to cancel ha dag", K(tmp_ret), KPC(sys_tablets_dag));
      } else {
        sys_tablets_dag = nullptr;
      }
    } else {
      LOG_INFO( "succeed to schedule sys and data tablets restore dag");
    }

    if (OB_FAIL(ret)) {
      if (OB_NOT_NULL(data_tablets_dag)) {
        scheduler->free_dag(*data_tablets_dag);
        data_tablets_dag = nullptr;
      }
      if (OB_NOT_NULL(sys_tablets_dag)) {
        scheduler->free_dag(*sys_tablets_dag);
        sys_tablets_dag = nullptr;
      }
    }
  }

  return ret;
}

int ObStartRestoreTask::build_ls_()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("start restore task do not init", K(ret));
  } else if (OB_FAIL(inner_build_ls_())) {
    LOG_WARN("failed to inner build ls", K(ret));
  } else if (OB_FAIL(create_all_tablets_())) {
    LOG_WARN("failed to create all tablets", K(ret));
  }
  return ret;
}

int ObStartRestoreTask::inner_build_ls_()
{
  int ret = OB_SUCCESS;
  ObLSMetaPackage ls_meta_package;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("start restore task do not init", K(ret));
  } else if (OB_ISNULL(helper_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("helper should not be NULL", K(ret));
  } else if (OB_FAIL(helper_->init_for_ls_view())) {
    LOG_WARN("failed to init ls view", K(ret), KPC(ctx_));
  } else if (OB_FAIL(helper_->fetch_ls_meta(ls_meta_package))) {
    LOG_WARN("failed to fetch ls meta", K(ret), KPC(ctx_));
  } else {
    ObRestoreStatus restore_status;
    if (OB_FAIL(ls_meta_package.ls_meta_.get_restore_status(restore_status))) {
      LOG_WARN("failed to get restore status", K(ret), K(ls_meta_package));
    } else if (ObRestoreStatus::Status::NONE != restore_status.get_status()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("restore status is unexpected", K(ret), K(restore_status));
    } else {
      ctx_->src_ls_meta_package_ = ls_meta_package;
    }
  }

  if (FAILEDx(update_ls_())) {
    LOG_WARN("failed to update ls", K(ret), KPC(ctx_));
  }
  return ret;
}

int ObStartRestoreTask::create_all_tablets_()
{
  int ret = OB_SUCCESS;

  int tmp_ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  ObArray<ObTabletID> tablet_id_array;
  ObIDagNet *dag_net = nullptr;
  obrpc::ObCopyTabletInfo tablet_info;
  ObCopyTabletSimpleInfo tablet_simple_info;
  const int overwrite = 1;
  ObLogicTabletID logic_tablet_id;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("start restore task do not init", K(ret));
  } else if (OB_ISNULL(this->get_dag()) || OB_ISNULL(helper_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag and helper should not be nullptr", K(ret), KP(this->get_dag()), KP(helper_));
  } else if (OB_ISNULL(dag_net = this->get_dag()->get_dag_net())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be nullptr", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (OB_FAIL(ObStorageHADagUtils::get_ls(sys_ls_id, ls_handle))) {
    LOG_WARN("failed to get ls", K(ret), KPC(ctx_));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KP(ls), KPC(ctx_));
  } else {
    ctx_->sys_tablet_id_array_.reset();
    ctx_->data_tablet_id_array_.reset();
    static const int64_t CREATE_TABLETS_WARN_THRESHOLD = 60 * 1000 * 1000; //60s
    common::ObTimeGuard timeguard("restore_create_all_tablets", CREATE_TABLETS_WARN_THRESHOLD);
    while (OB_SUCC(ret)) {
      tablet_info.reset();
      tablet_simple_info.reset();
      logic_tablet_id.reset();
      if (dag_net->is_cancel()) {
        ret = OB_CANCELED;
        LOG_WARN("restore dag is canceled", K(ret));
      } else if (OB_FAIL(fetch_next_tablet_info_(tablet_info))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to fetch tablet meta", K(ret), K(tablet_info));
        }
      } else if (OB_FAIL(create_tablet_(tablet_info, ls))) {
        LOG_WARN("failed to create tablet", K(ret), K(tablet_info));
      } else if (OB_FAIL(logic_tablet_id.init(tablet_info.tablet_id_, 0 /*transfer_seq*/))) {
        LOG_WARN("failed to init logic tablet id", K(ret), K(tablet_info));
      } else if (tablet_info.tablet_id_.is_ls_inner_tablet()) {
        if (OB_FAIL(ctx_->sys_tablet_id_array_.push_back(logic_tablet_id))) {
          LOG_WARN("failed to push back sys tablet id", K(ret), K(logic_tablet_id));
        }
      } else {
        if (OB_FAIL(ctx_->data_tablet_id_array_.push_back(logic_tablet_id))) {
          LOG_WARN("failed to push back data tablet id", K(ret), K(logic_tablet_id));
        }
      }
      if (OB_FAIL(ret)) {
      } else {
        tablet_simple_info.tablet_id_ = tablet_info.tablet_id_;
        tablet_simple_info.status_ = tablet_info.status_;
        tablet_simple_info.data_size_ = tablet_info.data_size_;
        if (OB_FAIL(ctx_->tablet_simple_info_map_.set_refactored(tablet_info.tablet_id_, tablet_simple_info, overwrite))) {
          LOG_WARN("failed to set tablet simple info", K(ret), K(tablet_info));
        }
      }
    }
  }
  return ret;
}

int ObStartRestoreTask::fetch_next_tablet_info_(obrpc::ObCopyTabletInfo &tablet_info)
{
  int ret = OB_SUCCESS;
  tablet_info.reset();
  if (OB_ISNULL(helper_) || !helper_->is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("helper should not be nullptr or invalid", K(ret), KP(helper_));
  } else if (OB_FAIL(helper_->fetch_next_tablet_info(tablet_info))) {
    LOG_WARN("failed to fetch tablet meta", K(ret), K(tablet_info));
  } else if (!tablet_info.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet info should be valid", K(ret), K(tablet_info));
  } else if (tablet_info.param_.is_empty_shell()) {
    // do nothing
  } else if (!tablet_info.param_.ha_status_.is_data_status_complete()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet info ha status is unexpected", K(ret), K(tablet_info));
  } else if (OB_FAIL(tablet_info.param_.ha_status_.set_data_status(ObTabletDataStatus::INCOMPLETE))) {
    LOG_WARN("failed to set data status", K(ret), K(tablet_info));
  }
  return ret;
}

int ObStartRestoreTask::create_tablet_(const obrpc::ObCopyTabletInfo &tablet_info, ObLS *ls)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(ls) || !tablet_info.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls and tablet info should not be nullptr", K(ret), KP(ls), K(tablet_info));
  } else if (ObCopyTabletStatus::TABLET_NOT_EXIST == tablet_info.status_ && tablet_info.tablet_id_.is_ls_inner_tablet()) {
    ret = OB_TABLET_NOT_EXIST;
    LOG_WARN("src ls inner tablet is not exist, src ls is maybe deleted", K(ret), K(tablet_info));
  } else if (OB_FAIL(ObTabletCreateMdsHelper::check_create_new_tablets(1LL, ObTabletCreateThrottlingLevel::SOFT))) {
    if (OB_TOO_MANY_PARTITIONS_ERROR == ret) {
      LOG_ERROR("too many partitions, failed to check create new tablet", K(ret), K(tablet_info));
    } else {
      LOG_WARN("failed to check create new tablet", K(ret), K(tablet_info));
    }
  } else {
    if (OB_FAIL(ObRestoreDagNetUtils::create_or_update_tablet(tablet_info, true/*need_check_tablet_limit*/, ls))) {
      LOG_WARN("failed to create tablet", K(ret), K(tablet_info));
    }
  }
  return ret;
}

int ObStartRestoreTask::record_server_event_()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("start restore task do not init", K(ret));
  } else {
    SERVER_EVENT_ADD("storage_ha", "standby_restore_start_task",
        "task_id", ctx_->task_.task_id_,
        "task_type", ctx_->task_.type_,
        "src_addr", ctx_->task_.src_info_,
        "local_checkpoint_scn", ctx_->local_clog_checkpoint_scn_);
  }
  return ret;
}

/**************************ObSysTabletsRestoreDag***************************/
ObSysTabletsRestoreDag::ObSysTabletsRestoreDag()
  : ObRestoreDag(share::ObDagType::DAG_TYPE_SYS_TABLETS_RESTORE),
    is_inited_(false)
{
}

ObSysTabletsRestoreDag::~ObSysTabletsRestoreDag()
{
}

bool ObSysTabletsRestoreDag::operator == (const share::ObIDag &other) const
{
  bool is_same = true;
  if (this == &other) {
    // same
  } else if (get_type() != other.get_type()) {
    is_same = false;
  } else {
    const ObSysTabletsRestoreDag &other_dag = static_cast<const ObSysTabletsRestoreDag&>(other);
    ObRestoreDagNetCtx *ctx = get_ctx();
    ObRestoreDagNetCtx *other_ctx = other_dag.get_ctx();
    if (OB_ISNULL(ctx) || OB_ISNULL(other_ctx)) {
      is_same = false;
      LOG_ERROR_RET(OB_INVALID_ARGUMENT, "restore ctx should not be NULL", KP(ctx), KP(other_ctx));
    } else if (ctx->task_.task_id_ != other_ctx->task_.task_id_) {
      is_same = false;
    }
  }
  return is_same;
}

uint64_t ObSysTabletsRestoreDag::hash() const
{
  uint64_t hash_value = 0;
  ObRestoreDagNetCtx *ctx = get_ctx();
  if (OB_ISNULL(ctx)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore dag ctx should not be NULL", KPC(ctx));
  } else {
    hash_value = common::murmurhash(&ctx->task_.task_id_, sizeof(ctx->task_.task_id_), hash_value);
    ObDagType::ObDagTypeEnum dag_type = get_type();
    hash_value = common::murmurhash(&dag_type, sizeof(dag_type), hash_value);
  }
  return hash_value;
}

int ObSysTabletsRestoreDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  ObRestoreDagNetCtx *ctx = get_ctx();
  ObCStringHelper helper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("sys tablets restore dag do not init", K(ret));
  } else if (OB_ISNULL(ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore dag ctx should not be null", K(ret), KPC(ctx));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(databuff_printf(buf, buf_len, "ObSysTabletsRestoreDag task_id=%s, task_type=%s",
                                        helper.convert(ctx->task_.task_id_),
                                        ObRestoreTaskType::get_str(ctx->task_.type_)))) {
    LOG_WARN("failed to fill dag key", K(ret), KPC(ctx));
  }
  return ret;
}

int ObSysTabletsRestoreDag::create_first_task()
{
  int ret = OB_SUCCESS;
  ObSysTabletsRestoreTask *task = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("sys tablets restore dag do not init", K(ret));
  } else if (OB_FAIL(alloc_task(task))) {
    LOG_WARN("Fail to alloc task", K(ret));
  } else if (OB_FAIL(task->init())) {
    LOG_WARN("failed to init sys tablets restore task", K(ret), KPC(ha_dag_net_ctx_));
  } else if (OB_FAIL(add_task(*task))) {
    LOG_WARN("Fail to add task", K(ret));
  } else {
    LOG_DEBUG("success to create first task", K(ret), KPC(this));
  }
  return ret;
}

int ObSysTabletsRestoreDag::init(share::ObIDagNet *dag_net)
{
  int ret = OB_SUCCESS;
  ObRestoreDagNet *restore_dag_net = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("sys tablets restore dag init twice", K(ret));
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet*>(dag_net))) {
  } else if (FALSE_IT(ha_dag_net_ctx_ = restore_dag_net->get_ctx())) {
  } else if (OB_ISNULL(ha_dag_net_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ha_dag_net_ctx_));
  } else {
    is_inited_ = true;
  }
  return ret;
}

/**************************ObSysTabletsRestoreTask***************************/
ObSysTabletsRestoreTask::ObSysTabletsRestoreTask()
  : share::ObITask(share::ObITask::TASK_TYPE_MIGRATE_PREPARE),
    is_inited_(false),
    ctx_(nullptr),
    ls_handle_(),
    bandwidth_throttle_(nullptr),
    allocator_("SysTabletsRes"),
    helper_(nullptr),
    dag_net_(nullptr)
{
}

ObSysTabletsRestoreTask::~ObSysTabletsRestoreTask()
{
  if (OB_NOT_NULL(helper_)) {
    helper_->destroy();
    helper_ = nullptr;
  }
}

int ObSysTabletsRestoreTask::init()
{
  int ret = OB_SUCCESS;
  ObIDagNet *dag_net = nullptr;
  ObRestoreDagNet *restore_dag_net = nullptr;
  ObIRestoreHelper *proto_helper = nullptr;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("sys tablets restore task init twice", K(ret));
  } else if (OB_ISNULL(dag_net = this->get_dag()->get_dag_net())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet *>(dag_net))) {
  } else if (OB_ISNULL(ctx_ = restore_dag_net->get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ctx_));
  } else if (OB_ISNULL(bandwidth_throttle_ = restore_dag_net->get_bandwidth_throttle())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("bandwidth throttle should not be NULL", K(ret), KP(bandwidth_throttle_));
  } else if (OB_ISNULL(proto_helper = restore_dag_net->get_helper())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("helper should not be NULL", K(ret), KP(proto_helper));
  } else if (OB_FAIL(proto_helper->copy_for_task(allocator_, helper_))) {
    LOG_WARN("failed to copy task helper", K(ret), KP(proto_helper));
  } else if (OB_ISNULL(helper_) || !helper_->is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task helper should not be null or invalid", K(ret), KP(helper_));
  } else if (OB_FAIL(ObStorageHADagUtils::get_ls(sys_ls_id, ls_handle_))) {
    LOG_WARN("failed to get ls", K(ret));
  } else {
    dag_net_ = dag_net;
    is_inited_ = true;
    LOG_INFO("succeed to init sys tablets restore task", K(ret), "dag_net_id", ctx_->task_.task_id_);
  }
  return ret;
}

int ObSysTabletsRestoreTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("sys tablets restore task do not init", K(ret));
  } else if (ctx_->is_failed()) {
    //do nothing
  } else if (OB_FAIL(build_tablets_sstable_info_())) {
    LOG_WARN("failed to build tablets sstable info", K(ret), KPC(ctx_));
  } else if (OB_FAIL(generate_sys_tablet_restore_dag_())) {
    LOG_WARN("failed to generate sys tablet restore dag", K(ret), KPC(ctx_));
  }

  if (OB_FAIL(ret)) {
    if (OB_TMP_FAIL(ObStorageHADagUtils::deal_with_fo(ret, this->get_dag()))) {
      LOG_WARN("failed to deal with fo", K(ret), K(tmp_ret), KPC(ctx_));
    }
  }
  return ret;
}

int ObSysTabletsRestoreTask::build_tablets_sstable_info_()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("sys tablets restore task do not init", K(ret));
  } else if (OB_ISNULL(ctx_) || OB_ISNULL(helper_) || OB_ISNULL(dag_net_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx or helper or dag net should not be NULL", K(ret), KP(ctx_), KP(helper_), KP(dag_net_));
  } else if (OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KP(ls), KPC(ctx_));
  } else {
    ObArray<ObTabletID> tablet_id_array;
    if (OB_FAIL(ObStorageHAUtils::append_tablet_list(ctx_->sys_tablet_id_array_, tablet_id_array))) {
      LOG_WARN("failed to append tablet list", K(ret), K(ctx_->sys_tablet_id_array_), KPC(ctx_));
    } else if (OB_FAIL(ObRestoreDagNetUtils::build_tablets_sstable_info_with_helper(tablet_id_array, helper_,
                                                                  &ctx_->ha_table_info_mgr_, dag_net_, ls))) {
      LOG_WARN("failed to build tablets sstable info with helper", K(ret), KPC(ctx_));
    }
  }
  return ret;
}

int ObSysTabletsRestoreTask::generate_sys_tablet_restore_dag_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObArray<share::ObIDag *> tablet_restore_dag_array;
  ObTenantDagScheduler *scheduler = nullptr;
  ObSysTabletsRestoreDag *sys_tablets_restore_dag = nullptr;
  share::ObIDag *parent = nullptr;
  ObLS *ls = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("sys tablets restore task do not init", K(ret));
  } else if (OB_ISNULL(sys_tablets_restore_dag = static_cast<ObSysTabletsRestoreDag *>(this->get_dag()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sys tablets restore dag should not be NULL", K(ret), KP(sys_tablets_restore_dag));
  } else if (OB_ISNULL(dag_net_) || OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL or dag net is NULL", K(ret), KP(dag_net_), KP(scheduler));
  } else if (OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KP(ls), KPC(ctx_));
  } else if (OB_FALSE_IT(parent = this->get_dag())) {
  } else if (OB_FAIL(tablet_restore_dag_array.push_back(parent))) {
    LOG_WARN("failed to push sys_tablets_restore_dag into array", K(ret), KPC(ctx_));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < ctx_->sys_tablet_id_array_.count(); ++i) {
      const ObTabletID &tablet_id = ctx_->sys_tablet_id_array_.at(i).tablet_id_;
      ObTabletRestoreDag *tablet_restore_dag = nullptr;
      ObTabletHandle tablet_handle;

      if (dag_net_->is_cancel()) {
        ret = OB_CANCELED;
        LOG_WARN("task is cancelled", K(ret), K(tablet_id));
      } else if (OB_FAIL(ls->ha_get_tablet(tablet_id, tablet_handle))) {
        LOG_WARN("failed to get tablet", K(ret), K(tablet_id), KPC(ctx_));
      } else if (OB_FAIL(scheduler->alloc_dag(tablet_restore_dag))) {
        LOG_WARN("failed to alloc tablet restore dag", K(ret));
      } else if (OB_FAIL(tablet_restore_dag_array.push_back(tablet_restore_dag))) {
        LOG_WARN("failed to push tablet restore dag into array", K(ret), KPC(ctx_));
      } else if (OB_FAIL(tablet_restore_dag->init(tablet_id, tablet_handle, dag_net_, nullptr /*tablet_group_ctx*/,
                                                      ObTabletRestoreDag::ObTabletType::SYS_TABLET_TYPE))) {
        LOG_WARN("failed to init tablet restore dag", K(ret), K(tablet_id), KPC(ctx_));
      } else if (OB_FAIL(parent->add_child(*tablet_restore_dag))) {
        LOG_WARN("failed to add child dag", K(ret), K(tablet_id), KPC(ctx_));
      } else if (OB_FAIL(tablet_restore_dag->create_first_task())) {
        LOG_WARN("failed to create first task", K(ret), K(tablet_id), KPC(ctx_));
      } else if (OB_FAIL(scheduler->add_dag(tablet_restore_dag))) {
        LOG_WARN("failed to add tablet restore dag", K(ret), K(*tablet_restore_dag));
        if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
          LOG_WARN("Fail to add task", K(ret));
          ret = OB_EAGAIN;
        }
      } else {
        LOG_INFO("succeed to schedule tablet restore dag", K(tablet_id), KPC(tablet_restore_dag));
        parent = tablet_restore_dag;
        tablet_restore_dag = nullptr;
      }

      if (OB_FAIL(ret) && OB_NOT_NULL(tablet_restore_dag)) {
        // tablet_restore_dag_array is not empty.
        share::ObIDag *last = tablet_restore_dag_array.at(tablet_restore_dag_array.count() - 1);
        if (last == tablet_restore_dag) {
          tablet_restore_dag_array.pop_back();
        }
        scheduler->free_dag(*tablet_restore_dag);
        tablet_restore_dag = nullptr;
      }
    }

    // Cancel all dags from back to front, except the first dag which is 'sys_tablets_restore_dag'.
    if (OB_FAIL(ret)) {
      for (int64_t child_idx = tablet_restore_dag_array.count() - 1; child_idx > 0; --child_idx) {
        if (OB_TMP_FAIL(scheduler->cancel_dag(tablet_restore_dag_array.at(child_idx)))) {
          LOG_WARN("failed to cancel inner tablet restore dag", K(tmp_ret), K(child_idx));
        }
      }
      tablet_restore_dag_array.reset();
    }
  }
  return ret;
}

int ObSysTabletsRestoreTask::record_server_event_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx should not be null", K(ret), KPC(ctx_));
  } else {
    SERVER_EVENT_ADD("storage_ha", "sys_tablets_restore_task",
        "task_id", ctx_->task_.task_id_,
        "task_type", ObRestoreTaskType::get_str(ctx_->task_.type_),
        "src_addr", ctx_->task_.src_info_);
  }
  return ret;
}

/**************************ObTabletRestoreDag***************************/
ObTabletRestoreDag::ObTabletRestoreDag()
  : ObRestoreDag(share::ObDagType::DAG_TYPE_TABLET_RESTORE),
    is_inited_(false),
    ls_handle_(),
    copy_tablet_ctx_(),
    tablet_group_ctx_(nullptr),
    tablet_type_(ObTabletRestoreDag::ObTabletType::MAX_TYPE)
{
}

ObTabletRestoreDag::~ObTabletRestoreDag()
{
  int ret = OB_SUCCESS;
  ObRestoreDagNetCtx *ctx = get_ctx();
  if (OB_NOT_NULL(ctx)) {
    if (OB_FAIL(ctx->ha_table_info_mgr_.remove_tablet_table_info(copy_tablet_ctx_.tablet_id_))) {
      LOG_WARN("failed to remove tablet table info", K(ret), KPC(ctx), K(copy_tablet_ctx_));
    }
  }
}

bool ObTabletRestoreDag::operator == (const share::ObIDag &other) const
{
  bool is_same = true;
  if (this == &other) {
    // same
  } else if (get_type() != other.get_type()) {
    is_same = false;
  } else {
    const ObTabletRestoreDag &other_dag = static_cast<const ObTabletRestoreDag&>(other);
    ObRestoreDagNetCtx *ctx = get_ctx();
    ObRestoreDagNetCtx *other_ctx = other_dag.get_ctx();
    if (OB_NOT_NULL(ctx) || OB_ISNULL(other_ctx)) {
      is_same = false;
    } else if (ctx->task_.task_id_ != other_ctx->task_.task_id_) {
      is_same = false;
    } else if (copy_tablet_ctx_.tablet_id_ != other_dag.copy_tablet_ctx_.tablet_id_) {
      is_same = false;
    }
  }
  return is_same;
}

uint64_t ObTabletRestoreDag::hash() const
{
  uint64_t hash_value = 0;
  ObRestoreDagNetCtx *ctx = get_ctx();
  if (OB_ISNULL(ctx)) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "restore dag ctx should not be NULL", KPC(ctx));
  } else {
    hash_value = ctx->task_.task_id_.hash();
    hash_value = common::murmurhash(&copy_tablet_ctx_.tablet_id_, sizeof(copy_tablet_ctx_.tablet_id_), hash_value);
    share::ObDagType::ObDagTypeEnum dag_type = get_type();
    hash_value = common::murmurhash(&dag_type, sizeof(dag_type), hash_value);
  }
  return hash_value;
}

int ObTabletRestoreDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  ObRestoreDagNetCtx *ctx = nullptr;
  ObCStringHelper helper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore dag do not init", K(ret));
  } else if (OB_ISNULL(ctx = get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore dag ctx should not be NULL", K(ret), KP(ctx));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(databuff_printf(buf, buf_len,
      "ObTabletRestoreDag: task_id=%s, tablet_id=%s, task_type=%s",
      helper.convert(ctx->task_.task_id_),
      helper.convert(copy_tablet_ctx_.tablet_id_),
      ObRestoreTaskType::get_str(ctx->task_.type_)))) {
    LOG_WARN("failed to fill dag key", K(ret), KPC(ctx), K(copy_tablet_ctx_));
  }
  return ret;
}

int ObTabletRestoreDag::init(
    const common::ObTabletID &tablet_id,
    ObTabletHandle &tablet_handle,
    share::ObIDagNet *dag_net,
    ObHATabletGroupCtx *tablet_group_ctx,
    ObTabletType tablet_type)
{
  int ret = OB_SUCCESS;
  ObRestoreDagNet *restore_dag_net = nullptr;
  ObLS *ls = nullptr;
  ObCopyTabletStatus::STATUS status = ObCopyTabletStatus::TABLET_EXIST;
  ObRestoreDagNetCtx *ctx = nullptr;
  bool is_exist = true;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet restore dag init twice", K(ret));
  } else if (!tablet_id.is_valid() || OB_ISNULL(dag_net)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet restore dag init get invalid argument", K(ret), K(tablet_id), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet*>(dag_net))) {
  } else if (OB_ISNULL(ctx = restore_dag_net->get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ctx));
  } else if (OB_FAIL(ObStorageHADagUtils::get_ls(sys_ls_id, ls_handle_))) {
    LOG_WARN("failed to get ls", K(ret), KPC(ctx));
  } else if (OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream should not be NULL", K(ret), K(tablet_id));
  } else if (FAILEDx(ctx->ha_table_info_mgr_.check_copy_tablet_exist(tablet_id, is_exist))) {
    LOG_WARN("failed to check copy tablet exist", K(ret), K(tablet_id));
  } else if (FALSE_IT(status = is_exist ? ObCopyTabletStatus::TABLET_EXIST : ObCopyTabletStatus::TABLET_NOT_EXIST)) {
  } else if (FALSE_IT(copy_tablet_ctx_.tablet_id_ = tablet_id)) {
  } else if (FALSE_IT(copy_tablet_ctx_.tablet_handle_ = tablet_handle)) {
  } else if (OB_FAIL(copy_tablet_ctx_.set_copy_tablet_status(status))) {
    LOG_WARN("failed to set copy tablet status", K(ret), K(status), K(tablet_id));
  } else if (FALSE_IT(ha_dag_net_ctx_ = ctx)) {
  } else {
    compat_mode_ = copy_tablet_ctx_.tablet_handle_.get_obj()->get_tablet_meta().compat_mode_;
    tablet_group_ctx_ = tablet_group_ctx;
    tablet_type_ = tablet_type;
    is_inited_ = true;
  }
  return ret;
}

int ObTabletRestoreDag::get_tablet_group_ctx(ObHATabletGroupCtx *&tablet_group_ctx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore dag do not init", K(ret));
  } else if (OB_ISNULL(tablet_group_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("only data tablet has tablet group ctx", K(ret), K_(tablet_type));
  } else {
    tablet_group_ctx = tablet_group_ctx_;
  }
  return ret;
}

int ObTabletRestoreDag::create_first_task()
{
  int ret = OB_SUCCESS;
  ObTabletRestoreTask *task = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore dag do not init", K(ret));
  } else if (OB_FAIL(alloc_task(task))) {
    LOG_WARN("failed to alloc tablet restore task", K(ret));
  } else if (OB_FAIL(task->init(copy_tablet_ctx_))) {
    LOG_WARN("failed to init tablet restore task", K(ret));
  } else if (OB_FAIL(add_task(*task))) {
    LOG_WARN("failed to add task", K(ret));
  }
  return ret;
}

int ObTabletRestoreDag::fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const
{
  int ret = OB_SUCCESS;
  ObRestoreDagNetCtx *ctx = nullptr;
  ObCStringHelper helper;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore dag do not init", K(ret));
  } else if (OB_ISNULL(ctx = get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore dag ctx should not be NULL", K(ret), KP(ctx));
  } else {
    if (OB_FAIL(ADD_DAG_WARN_INFO_PARAM(out_param, allocator, get_type(),
                                  static_cast<int64_t>(copy_tablet_ctx_.tablet_id_.id()),
                                  static_cast<int64_t>(ctx->task_.type_),
                                  "task_id", helper.convert(ctx->task_.task_id_),
                                  "src_addr", helper.convert(ctx->task_.src_info_)))) {
      LOG_WARN("failed to fill info param", K(ret));
    }
  }
  return ret;
}

int ObTabletRestoreDag::get_ls(ObLS *&ls)
{
  int ret = OB_SUCCESS;
  ls = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore dag is not init", K(ret));
  } else {
    ls = ls_handle_.get_ls();
  }
  return ret;
}

int ObTabletRestoreDag::generate_next_dag(share::ObIDag *&dag)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  dag = nullptr;
  ObTenantDagScheduler *scheduler = nullptr;
  ObIDagNet *dag_net = nullptr;
  ObTabletRestoreDag *tablet_restore_dag = nullptr;
  bool need_set_failed_result = true;
  ObLogicTabletID logic_tablet_id;
  ObDagId dag_id;
  const int64_t start_ts = ObTimeUtil::current_time();
  ObRestoreDagNetCtx *ctx = nullptr;
  ObLS *ls = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore dag do not init", K(ret));
  } else if (OB_ISNULL(ctx = get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ctx));
  } else if (OB_ISNULL(tablet_group_ctx_)) {
    ret = OB_ITER_END;
    need_set_failed_result = false;
    LOG_INFO("tablet restore dag do not has next dag", KPC(this));
  } else if (ctx->is_failed()) {
    if (OB_TMP_FAIL(ctx->get_result(ret))) {
      LOG_WARN("failed to get result", K(tmp_ret), KPC(ctx));
      ret = tmp_ret;
    }
  } else if (OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KPC(ctx));
  } else {
    while (OB_SUCC(ret)) {
      ObTabletHandle tablet_handle;
      if (OB_FAIL(tablet_group_ctx_->get_next_tablet_id(logic_tablet_id))) {
        if (OB_ITER_END == ret) {
          //do nothing
          need_set_failed_result = false;
        } else {
          LOG_WARN("failed to get next tablet id", K(ret), KPC(this));
        }
      } else if (OB_ISNULL(dag_net = this->get_dag_net())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("restore dag net should not be NULL", K(ret), KP(dag_net));
      } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get ObTenantDagScheduler from MTL", K(ret));
      } else if (OB_FAIL(ls->ha_get_tablet(logic_tablet_id.tablet_id_, tablet_handle))) {
        if (OB_TABLET_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to get tablet", K(ret), K(logic_tablet_id));
        }
      } else if (OB_FAIL(scheduler->alloc_dag(tablet_restore_dag))) {
        LOG_WARN("failed to alloc tablet restore dag", K(ret));
      } else {
        if (OB_FAIL(tablet_restore_dag->init(logic_tablet_id.tablet_id_, tablet_handle, dag_net, tablet_group_ctx_,
                                                ObTabletRestoreDag::ObTabletType::DATA_TABLET_TYPE))) {
          LOG_WARN("failed to init tablet restore dag", K(ret), K(logic_tablet_id));
        } else if (FALSE_IT(dag_id.init(MYADDR))) {
        } else if (OB_FAIL(tablet_restore_dag->set_dag_id(dag_id))) {
          LOG_WARN("failed to set dag id", K(ret), K(logic_tablet_id));
        } else {
          LOG_INFO("succeed generate next dag", K(logic_tablet_id));
          dag = tablet_restore_dag;
          tablet_restore_dag = nullptr;
          break;
        }
      }
    }
  }

  if (OB_NOT_NULL(tablet_restore_dag)) {
    if (OB_NOT_NULL(scheduler)) {
      scheduler->free_dag(*tablet_restore_dag);
    }
    tablet_restore_dag = nullptr;
  }

  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    const bool need_retry = false;
    if (need_set_failed_result && OB_TMP_FAIL(ha_dag_net_ctx_->set_result(ret, need_retry, get_type()))) {
      LOG_WARN("failed to set result", K(ret), K(tmp_ret), KPC(ha_dag_net_ctx_));
    }
  }
  LOG_INFO("generate_next_dag", K(logic_tablet_id), "cost", ObTimeUtil::current_time() - start_ts, "dag_id", dag_id);

  return ret;
}

int ObTabletRestoreDag::inner_reset_status_for_retry()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObRestoreDagNetCtx *ctx = nullptr;
  int32_t result = OB_SUCCESS;
  int32_t retry_count = 0;
  ObLS *ls = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore dag do not init", K(ret), KP(ha_dag_net_ctx_));
  } else if (OB_ISNULL(ctx = get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ctx));
  } else if (ctx->is_failed()) {
    if (OB_TMP_FAIL(ctx->get_result(ret))) {
      LOG_WARN("failed to get result", K(tmp_ret), KPC(ctx));
      ret = tmp_ret;
    } else {
      LOG_INFO("set inner reset status for retry failed", K(ret), KPC(ctx));
    }
  } else if (OB_FAIL(result_mgr_.get_result(result))) {
    LOG_WARN("failed to get result", K(ret), KP(ctx));
  } else if (OB_FAIL(result_mgr_.get_retry_count(retry_count))) {
    LOG_WARN("failed to get retry count", K(ret));
  } else {
    LOG_INFO("start retry", KPC(this));
    result_mgr_.reuse();
    SERVER_EVENT_ADD("storage_ha", "tablet_restore_retry",
        "task_id", ctx->task_.task_id_,
        "tablet_id", copy_tablet_ctx_.tablet_id_,
        "result", result,
        "retry_count", retry_count);
    if (OB_FAIL(ctx->ha_table_info_mgr_.remove_tablet_table_info(copy_tablet_ctx_.tablet_id_))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to remove tablet info", K(ret), KPC(ctx), K(copy_tablet_ctx_));
      }
    }

    if (OB_SUCC(ret)) {
      copy_tablet_ctx_.tablet_handle_.reset();
      copy_tablet_ctx_.macro_block_reuse_mgr_.reset();
      copy_tablet_ctx_.extra_info_.reset();
      share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);
      if (OB_ISNULL(ls = ls_handle_.get_ls())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls should not be NULL", K(ret), K(copy_tablet_ctx_));
      } else if (OB_FAIL(ls->ha_get_tablet(copy_tablet_ctx_.tablet_id_, copy_tablet_ctx_.tablet_handle_))) {
        if (OB_TABLET_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          const ObCopyTabletStatus::STATUS status = ObCopyTabletStatus::TABLET_NOT_EXIST;
          if (OB_FAIL(copy_tablet_ctx_.set_copy_tablet_status(status))) {
            LOG_WARN("failed to set copy tablet status", K(ret), K(status));
          } else {
            FLOG_INFO("tablet in dest is deleted, set copy status not exist", K(copy_tablet_ctx_));
          }
        } else {
          LOG_WARN("failed to get tablet", K(ret), K(copy_tablet_ctx_));
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(create_first_task())) {
      LOG_WARN("failed to create first task", K(ret), KPC(this));
    }
  }
  return ret;
}

/**************************ObTabletRestoreTask***************************/
ObTabletRestoreTask::ObTabletRestoreTask()
  : share::ObITask(share::ObITask::TASK_TYPE_MIGRATE_PREPARE),
    is_inited_(false),
    ctx_(nullptr),
    bandwidth_throttle_(nullptr),
    helper_(nullptr),
    copy_tablet_ctx_(nullptr),
    copy_table_key_array_(),
    copy_sstable_info_mgr_()
{
}

ObTabletRestoreTask::~ObTabletRestoreTask()
{
  if (OB_NOT_NULL(helper_)) {
    helper_->destroy();
    helper_ = nullptr;
  }
}

int ObTabletRestoreTask::init(ObCopyTabletCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObIDagNet *dag_net = nullptr;
  ObRestoreDagNet *restore_dag_net = nullptr;
  ObIRestoreHelper *proto_helper = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet restore task init twice", K(ret));
  } else if (!ctx.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet restore task init get invalid argument", K(ret), K(ctx));
  } else if (FALSE_IT(dag_net = this->get_dag()->get_dag_net())) {
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet*>(dag_net))) {
  } else {
    ctx_ = restore_dag_net->get_ctx();
    bandwidth_throttle_ = restore_dag_net->get_bandwidth_throttle();
    proto_helper = restore_dag_net->get_helper();
    copy_tablet_ctx_ = &ctx;
    if (OB_ISNULL(ctx_) || OB_ISNULL(proto_helper) || OB_ISNULL(bandwidth_throttle_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ctx or restore helper should not be NULL",
                  K(ret), KP(ctx_), KP(proto_helper), KP(bandwidth_throttle_));
    } else if (OB_FAIL(proto_helper->copy_for_task(allocator_, helper_))) {
      LOG_WARN("failed to copy task helper", K(ret), KP(proto_helper));
    } else if (OB_ISNULL(helper_) || !helper_->is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("task helper should not be null or invalid", K(ret), KP(helper_));
    } else {
      is_inited_ = true;
      LOG_INFO("succeed init tablet restore task", "tablet_id", ctx.tablet_id_,
          "dag_id", *ObCurTraceId::get_trace_id(), "dag_net_id", ctx_->task_.task_id_);
    }
  }
  return ret;
}

int ObTabletRestoreTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  LOG_INFO("start do tablet restore task", KPC(copy_tablet_ctx_));
  const int64_t start_ts = ObTimeUtility::current_time();
  ObCopyTabletStatus::STATUS status = ObCopyTabletStatus::MAX_STATUS;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);
  int32_t result = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret), KPC(copy_tablet_ctx_));
  } else if (OB_ISNULL(ctx_) || OB_ISNULL(copy_tablet_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx or copy tablet ctx should not be NULL", K(ret), KP(ctx_), KP(copy_tablet_ctx_));
  } else if (ctx_->is_failed()) {
    //do nothing
  } else if (!copy_tablet_ctx_->tablet_id_.is_ls_inner_tablet()
                && OB_FAIL(ObStorageHAUtils::check_log_status(MTL_ID(), sys_ls_id, result))) {
    LOG_WARN("failed to check if can replay log", K(ret), KPC(ctx_));
  } else if (OB_SUCCESS != result) {
    LOG_INFO("can not replay log, it will retry", K(result), KPC(ctx_));
    if (OB_FAIL(ctx_->set_result(result/*result*/, true/*need_retry*/, this->get_dag()->get_type()))) {
      LOG_WARN("failed to set result", K(ret), KPC(ctx_));
    } else {
      ret = result;
      LOG_WARN("log sync or replay error, need retry", K(ret), KPC(ctx_));
    }
  } else if (OB_FAIL(try_update_tablet_())) {
    LOG_WARN("failed to try update tablet", K(ret), KPC(copy_tablet_ctx_));
  } else if (OB_FAIL(copy_tablet_ctx_->get_copy_tablet_status(status))) {
    LOG_WARN("failed to get copy tablet status", K(ret), KPC(copy_tablet_ctx_));
  } else if (ObCopyTabletStatus::TABLET_NOT_EXIST == status) {
    FLOG_INFO("copy tablet is not exist, skip copy it", KPC(copy_tablet_ctx_));
    if (OB_FAIL(update_ha_expected_status_(status))) {
      LOG_WARN("failed to update ha expected status", K(ret), KPC(copy_tablet_ctx_));
    }
  } else if (OB_FAIL(build_copy_table_key_info_())) {
    LOG_WARN("failed to build copy table key info", K(ret), KPC(copy_tablet_ctx_));
  } else if (OB_FAIL(build_copy_sstable_info_mgr_())) {
    LOG_WARN("failed to build copy sstable info mgr", K(ret), KPC(copy_tablet_ctx_));
  } else if (OB_FAIL(ObStorageHAUtils::build_major_sstable_reuse_info(copy_tablet_ctx_->tablet_handle_,
                                                                        copy_tablet_ctx_->macro_block_reuse_mgr_,
                                                                        false/*is_restore*/))) {
    LOG_WARN("failed to update major sstable reuse info", K(ret), KPC(copy_tablet_ctx_));
  } else if (OB_FAIL(generate_restore_tasks_())) {
    LOG_WARN("failed to generate restore tasks", K(ret), K(*copy_tablet_ctx_));
  }

  const int64_t cost_us = ObTimeUtility::current_time() - start_ts;
  if (OB_TMP_FAIL(record_server_event_(cost_us, ret))) {
    LOG_WARN("failed to record server event", K(tmp_ret), K(cost_us), K(ret));
  }

  if (OB_FAIL(ret)) {
    if (OB_TMP_FAIL(ObStorageHADagUtils::deal_with_fo(ret, this->get_dag()))) {
      LOG_WARN("failed to deal with fo", K(ret), K(tmp_ret), KPC(ctx_));
    }
  }

  return ret;
}

// task running dag map
   // copy minor sstables task ->
   // copy major sstables task ->
   // copy ddl sstables task   ->
   // tablet copy finish task  ->

// task function introduce
   // copy minor sstables task is copy needed minor sstable from remote
   // copy major sstables task is copy needed major sstable from remote
   // copy ddl sstables task is copy needed ddl tables from remote
   // tablet copy finish task is using copyed sstables to create new table store
int ObTabletRestoreTask::generate_restore_tasks_()
{
  int ret = OB_SUCCESS;
  ObITask *parent_task = this;
  ObTabletCopyFinishTask *tablet_copy_finish_task = nullptr;
  ObTabletFinishRestoreTask *tablet_finish_restore_task = nullptr;
  ObIDag *dag = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("not inited", K(ret));
  } else if (OB_FAIL(generate_tablet_copy_finish_task_(tablet_copy_finish_task))) {
    LOG_WARN("failed to generate tablet copy finish task", K(ret), KPC(ctx_));
  } else if (OB_FAIL(generate_mds_copy_tasks_(tablet_copy_finish_task, parent_task))) {
    LOG_WARN("failed to generate restore mds tasks", K(ret), K(*ctx_));
  } else if (OB_FAIL(generate_minor_copy_tasks_(tablet_copy_finish_task, parent_task))) {
    LOG_WARN("failed to generate restore minor tasks", K(ret), K(*ctx_));
  } else if (OB_FAIL(generate_major_copy_tasks_(tablet_copy_finish_task, parent_task))) {
    LOG_WARN("failed to generate restore major tasks", K(ret), K(*ctx_));
  } else if (OB_FAIL(generate_ddl_copy_tasks_(tablet_copy_finish_task, parent_task))) {
    LOG_WARN("failed to generate ddl copy tasks", K(ret), K(*ctx_));
  } else if (OB_FAIL(generate_tablet_finish_restore_task_(tablet_finish_restore_task))) {
    LOG_WARN("failed to generate tablet finish restore task", K(ret), KPC(ctx_));
  } else if (OB_FAIL(tablet_copy_finish_task->add_child(*tablet_finish_restore_task))) {
    LOG_WARN("failed to add child finish task for parent", K(ret), KPC(tablet_finish_restore_task));
  } else if (OB_FAIL(parent_task->add_child(*tablet_copy_finish_task))) {
    LOG_WARN("failed to add tablet copy finish task as child", K(ret), KPC(ctx_));
  } else if (OB_ISNULL(dag = this->get_dag())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag should not be NULL", K(ret), KP(dag));
  } else if (OB_FAIL(dag->add_task(*tablet_copy_finish_task))) {
    LOG_WARN("failed to add tablet copy finish task", K(ret), KPC(ctx_));
  } else if (OB_FAIL(dag->add_task(*tablet_finish_restore_task))) {
    LOG_WARN("failed to add tablet finish restore task", K(ret));
  } else {
    LOG_INFO("generate sstable restore tasks", KPC(copy_tablet_ctx_), K(copy_table_key_array_));
  }
  return ret;
}

int ObTabletRestoreTask::generate_tablet_finish_restore_task_(
    ObTabletFinishRestoreTask *&tablet_finish_restore_task)
{
  int ret = OB_SUCCESS;
  ObTabletRestoreDag *tablet_restore_dag = nullptr;
  ObLS *ls = nullptr;
  ObIDag *dag = nullptr;
  const int64_t task_gen_time = ObTimeUtility::current_time();

  if (OB_NOT_NULL(tablet_finish_restore_task)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet finish restore task must not be null", K(ret), KPC(tablet_finish_restore_task));
  } else if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (OB_ISNULL(dag = this->get_dag()) || ObDagType::DAG_TYPE_TABLET_RESTORE != dag->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag should not be NULL or dag type is unexpected", K(ret), KP(dag));
  } else if (FALSE_IT(tablet_restore_dag = static_cast<ObTabletRestoreDag *>(this->get_dag()))) {
  } else if (OB_FAIL(tablet_restore_dag->alloc_task(tablet_finish_restore_task))) {
    LOG_WARN("failed to alloc tablet finish restore task", K(ret), KPC(ctx_));
  } else if (OB_FAIL(tablet_restore_dag->get_ls(ls))) {
    LOG_WARN("failed to get ls", K(ret), KPC(ctx_));
  } else if (OB_ISNULL(ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret));
  } else if (OB_FAIL(tablet_finish_restore_task->init(task_gen_time, copy_table_key_array_.count(), *copy_tablet_ctx_, *ls))) {
    LOG_WARN("failed to init tablet finish restore task", K(ret), KPC(ctx_), KPC(copy_tablet_ctx_));
  } else {
    LOG_INFO("generate tablet restore finish task", "ls_id", ls->get_ls_id().id(), "tablet_id", copy_tablet_ctx_->tablet_id_);
  }
  return ret;
}

int ObTabletRestoreTask::generate_minor_copy_tasks_(
    ObTabletCopyFinishTask *tablet_copy_finish_task,
    share::ObITask *&parent_task)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (OB_ISNULL(tablet_copy_finish_task) || OB_ISNULL(parent_task)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("generate minor task get invalid argument", K(ret), KP(tablet_copy_finish_task), KP(parent_task));
  } else if (OB_FAIL(generate_copy_tasks_(ObITable::is_minor_sstable, tablet_copy_finish_task, parent_task))) {
    LOG_WARN("failed to generate copy minor tasks", K(ret), KPC(copy_tablet_ctx_));
  }
  return ret;
}

int ObTabletRestoreTask::generate_major_copy_tasks_(
    ObTabletCopyFinishTask *tablet_copy_finish_task,
    share::ObITask *&parent_task)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (OB_ISNULL(tablet_copy_finish_task) || OB_ISNULL(parent_task)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("generate major task get invalid argument", K(ret), KP(tablet_copy_finish_task), KP(parent_task));
  } else if (OB_FAIL(generate_copy_tasks_(ObITable::is_major_sstable, tablet_copy_finish_task, parent_task))) {
    LOG_WARN("failed to generate copy major tasks", K(ret), KPC(copy_tablet_ctx_));
  }
  return ret;
}

int ObTabletRestoreTask::generate_ddl_copy_tasks_(
    ObTabletCopyFinishTask *tablet_copy_finish_task,
    share::ObITask *&parent_task)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (OB_ISNULL(tablet_copy_finish_task) || OB_ISNULL(parent_task)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("generate ddl task get invalid argument", K(ret), KP(tablet_copy_finish_task), KP(parent_task));
  } else if (OB_FAIL(generate_copy_tasks_(ObITable::is_ddl_sstable, tablet_copy_finish_task, parent_task))) {
    LOG_WARN("failed to generate copy ddl tasks", K(ret), KPC(copy_tablet_ctx_));
  }
  return ret;
}

int ObTabletRestoreTask::generate_physical_copy_task_(
    const ObITable::TableKey &copy_table_key,
    ObTabletCopyFinishTask *tablet_copy_finish_task,
    ObITask *parent_task,
    ObITask *child_task)
{
  int ret = OB_SUCCESS;
  ObPhysicalCopyTask *copy_task = nullptr;
  ObSSTableCopyFinishTask *finish_task = nullptr;
  ObLS *ls = nullptr;
  ObPhysicalCopyTaskInitParam init_param;
  ObTabletRestoreDag *tablet_restore_dag = nullptr;
  ObIDag *dag = nullptr;
  bool is_tablet_exist = true;
  bool need_copy = true;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (!copy_table_key.is_valid() || OB_ISNULL(tablet_copy_finish_task)
                || OB_ISNULL(parent_task) || OB_ISNULL(child_task)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(copy_table_key), KP(parent_task), KP(child_task));
  } else if (OB_ISNULL(dag = this->get_dag())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag should not be NULL", K(ret));
  } else if (FALSE_IT(tablet_restore_dag = static_cast<ObTabletRestoreDag *>(dag))) {
  } else if (OB_FAIL(tablet_restore_dag->get_ls(ls))) {
    LOG_WARN("failed to get ls", K(ret), KPC(ctx_));
  } else if (OB_FAIL(copy_sstable_info_mgr_.check_src_tablet_exist(is_tablet_exist))) {
    LOG_WARN("failed to check src tablet exist", K(ret), K(copy_table_key));
  } else if (!is_tablet_exist) {
    if (OB_FAIL(tablet_copy_finish_task->set_tablet_status(ObCopyTabletStatus::TABLET_NOT_EXIST))) {
      LOG_WARN("failed to set tablet status", K(ret), K(copy_table_key), KPC(copy_tablet_ctx_));
    } else if (OB_FAIL(parent_task->add_child(*child_task))) {
      LOG_WARN("failed to add child task", K(ret), KPC(copy_tablet_ctx_), K(copy_table_key));
    }
  } else {
    if (OB_FAIL(ctx_->ha_table_info_mgr_.get_table_info(copy_tablet_ctx_->tablet_id_,
                                                            copy_table_key, init_param.sstable_param_))) {
      LOG_WARN("failed to get table info", K(ret), KPC(copy_tablet_ctx_), K(copy_table_key));
    } else if (OB_FAIL(ObStorageHATaskUtils::check_need_copy_macro_blocks(*init_param.sstable_param_,
                                                                          helper_->is_leader_restore(),
                                                                          need_copy))) {
      LOG_WARN("failed to check need copy macro blocks", K(ret), K(init_param), K(copy_table_key));
    } else {
      init_param.tenant_id_ = MTL_ID();
      init_param.ls_id_ = sys_ls_id;
      init_param.tablet_id_ = copy_tablet_ctx_->tablet_id_;
      init_param.tablet_copy_finish_task_ = tablet_copy_finish_task;
      init_param.ls_ = ls;
      init_param.macro_block_reuse_mgr_ = ObITable::is_major_sstable(copy_table_key.table_type_) ? &copy_tablet_ctx_->macro_block_reuse_mgr_ : nullptr;
      init_param.extra_info_ = &copy_tablet_ctx_->extra_info_;
      init_param.helper_ = helper_;

      if (OB_FAIL(ctx_->ha_table_info_mgr_.get_table_info(copy_tablet_ctx_->tablet_id_,
                                                              copy_table_key, init_param.sstable_param_))) {
        LOG_WARN("failed to get table info", K(ret), KPC(copy_tablet_ctx_), K(copy_table_key));
      } else if (!need_copy && FALSE_IT(init_param.sstable_macro_range_info_.copy_table_key_ = copy_table_key)) {
      } else if (need_copy && OB_FAIL(copy_sstable_info_mgr_.get_copy_sstable_maro_range_info(copy_table_key,
                                                                          init_param.sstable_macro_range_info_))) {
        LOG_WARN("failed to get copy sstable macro range info", K(ret), K(copy_table_key));
      } else if (!init_param.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("physical copy task init param not valid", K(ret), K(init_param), KPC(ctx_));
      } else if (OB_FAIL(dag->alloc_task(finish_task))) {
        LOG_WARN("failed to alloc finish task", K(ret));
      } else if (OB_FAIL(finish_task->init(init_param))) {
        LOG_WARN("failed to init finish task", K(ret), K(copy_table_key), K(*ctx_));
      } else if (OB_FAIL(finish_task->add_child(*child_task))) {
        LOG_WARN("failed to add child", K(ret));
      } else if (need_copy) {
        // parent->copy->finish->child
        if (OB_FAIL(dag->alloc_task(copy_task))) {
          LOG_WARN("failed to alloc copy task", K(ret));
        } else if (OB_FAIL(copy_task->init(finish_task->get_copy_ctx(), finish_task))) {
          LOG_WARN("failed to init copy task", K(ret));
        } else if (OB_FAIL(parent_task->add_child(*copy_task))) {
          LOG_WARN("failed to add child copy task", K(ret));
        } else if (OB_FAIL(copy_task->add_child(*finish_task))) {
          LOG_WARN("failed to add child finish task", K(ret));
        } else if (OB_FAIL(dag->add_task(*copy_task))) {
          LOG_WARN("failed to add copy task to dag", K(ret));
        }
      } else {
        if (OB_FAIL(parent_task->add_child(*finish_task))) {
          LOG_WARN("failed to add child finish_task for parent", K(ret));
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(dag->add_task(*finish_task))) {
          LOG_WARN("failed to add finish task to dag", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObTabletRestoreTask::build_copy_table_key_info_()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (OB_FAIL(ctx_->ha_table_info_mgr_.get_table_keys(copy_tablet_ctx_->tablet_id_, copy_table_key_array_))) {
    LOG_WARN("failed to get copy table keys", K(ret), KPC(copy_tablet_ctx_));
  } else {
    // sort copy table key array by snapshot version, copy major sstable from low version to high version
    ObStorageHAUtils::sort_table_key_array_by_snapshot_version(copy_table_key_array_);
    LOG_INFO("succeed to build copy table key info", K(copy_table_key_array_));
  }
  return ret;
}

int ObTabletRestoreTask::build_copy_sstable_info_mgr_()
{
  int ret = OB_SUCCESS;
  ObStorageHACopySSTableParam param;
  ObArray<ObITable::TableKey> filter_table_key_array;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (OB_FAIL(get_need_copy_sstable_info_key_(copy_table_key_array_, filter_table_key_array))) {
    LOG_WARN("failed to get need copy sstable info key", K(ret), K(copy_table_key_array_));
  } else {
    // copy sstable macro range info is provided by helper in this branch
    if (OB_FAIL(param.copy_table_key_array_.assign(filter_table_key_array))) {
      LOG_WARN("failed to assign copy table key array", K(ret), K(filter_table_key_array));
    } else {
      param.helper_ = helper_;
      if (OB_FAIL(copy_sstable_info_mgr_.init(param))) {
        LOG_WARN("failed to init copy sstable info mgr", K(ret), K(param), KPC(ctx_), KPC(copy_tablet_ctx_));
      }
    }
  }
  return ret;
}

int ObTabletRestoreTask::generate_copy_tasks_(
    IsRightTypeSSTableFunc is_right_type_sstable,
    ObTabletCopyFinishTask *tablet_copy_finish_task,
    share::ObITask *&parent_task)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (OB_ISNULL(tablet_copy_finish_task) || OB_ISNULL(parent_task)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("generate copy task get invalid argument", K(ret), KP(parent_task));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < copy_table_key_array_.count(); ++i) {
    const ObITable::TableKey &copy_table_key = copy_table_key_array_.at(i);
    ObFakeTask *wait_finish_task = nullptr;
    bool need_copy = true;

    if (!copy_table_key.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("copy table key info is invalid", K(ret), K(copy_table_key));
    } else if (!is_right_type_sstable(copy_table_key.table_type_)) {
      //do nothing
    } else {
      if (OB_FAIL(check_need_copy_sstable_(copy_table_key, need_copy))) {
        LOG_WARN("failed to check need copy sstable", K(ret), K(copy_table_key));
      } else if (!need_copy) {
        LOG_INFO("local contains the sstable, no need copy", K(copy_table_key));
      } else if (OB_FAIL(dag_->alloc_task(wait_finish_task))) {
        LOG_WARN("failed to alloc wait finish task", K(ret));
      } else if (OB_FAIL(generate_physical_copy_task_(copy_table_key, tablet_copy_finish_task,
                                                          parent_task, wait_finish_task))) {
        LOG_WARN("failed to generate physical copy task", K(ret), KPC(ctx_), K(copy_table_key));
      } else if (OB_FAIL(dag_->add_task(*wait_finish_task))) {
        LOG_WARN("failed to add wait finish task", K(ret));
      } else {
        parent_task = wait_finish_task;
        LOG_INFO("succeed to generate sstable copy task", K(copy_table_key));
      }
    }
  }
  return ret;
}

int ObTabletRestoreTask::generate_tablet_copy_finish_task_(
    ObTabletCopyFinishTask *&tablet_copy_finish_task)
{
  int ret = OB_SUCCESS;
  tablet_copy_finish_task = nullptr;
  ObLS *ls = nullptr;
  const ObMigrationTabletParam *src_tablet_meta = nullptr;
  ObTabletRestoreDag *tablet_restore_dag = nullptr;
  const ObTabletRestoreAction::ACTION restore_action = ObTabletRestoreAction::RESTORE_NONE;
  const bool is_leader_restore = false;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (FALSE_IT(tablet_restore_dag = static_cast<ObTabletRestoreDag *>(dag_))) {
  } else if (OB_FAIL(dag_->alloc_task(tablet_copy_finish_task))) {
    LOG_WARN("failed to alloc tablet copy finish task", K(ret), KPC(ctx_));
  } else if (OB_FAIL(tablet_restore_dag->get_ls(ls))) {
    LOG_WARN("failed to get ls", K(ret), KPC(ctx_));
  } else if (OB_FAIL(ctx_->ha_table_info_mgr_.get_tablet_meta(copy_tablet_ctx_->tablet_id_, src_tablet_meta))) {
    LOG_WARN("failed to get src tablet meta", K(ret), KPC(copy_tablet_ctx_));
  } else {
    // TODO(xingzhi): refactor with helper
    ObTabletCopyFinishTaskParam param;
    param.ls_ = ls;
    param.tablet_id_ = copy_tablet_ctx_->tablet_id_;
    param.copy_tablet_ctx_ = copy_tablet_ctx_;
    param.restore_action_ = restore_action;
    param.src_tablet_meta_ = src_tablet_meta;
    param.is_leader_restore_ = is_leader_restore;
    param.is_only_replace_major_ = false;
    if (OB_FAIL(tablet_copy_finish_task->init(param))) {
      LOG_WARN("failed to init tablet copy finish task", K(ret), KPC(ctx_), KPC(copy_tablet_ctx_));
    } else {
      LOG_INFO("generate tablet copy finish task", "ls_id", ls->get_ls_id().id(), "tablet_id", copy_tablet_ctx_->tablet_id_);
    }
  }
  return ret;
}

int ObTabletRestoreTask::record_server_event_(const int64_t cost_us, const int64_t result)
{
  int ret = OB_SUCCESS;
  const ObMigrationTabletParam *src_tablet_meta = nullptr;
  ObTabletCreateDeleteMdsUserData user_data;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  ObTabletRestoreDag *tablet_restore_dag = nullptr;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);

  if (OB_ISNULL(ctx_) || OB_ISNULL(copy_tablet_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx should not be null", K(ret), KPC_(ctx), KPC_(copy_tablet_ctx));
  } else if (FALSE_IT(tablet_restore_dag = static_cast<ObTabletRestoreDag *>(dag_))) {
  } else if (OB_FAIL(tablet_restore_dag->get_ls(ls))) {
    LOG_WARN("failed to get ls", K(ret), KPC(ctx_));
  } else if (OB_FAIL(ls->ha_get_tablet(copy_tablet_ctx_->tablet_id_, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret));
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret));
  } else if (OB_FAIL(tablet->get_latest_committed_tablet_status(user_data))) {
    LOG_WARN("failed to get tx data", K(ret), KPC(tablet));
  } else {
    const char *tablet_status = ObTabletStatus::get_str(user_data.tablet_status_);
    SERVER_EVENT_ADD("storage_ha", "tablet_restore_task",
        "tenant_id", MTL_ID(),
        "src", ctx_->task_.src_info_,
        "tablet_id", copy_tablet_ctx_->tablet_id_.id(),
        "cost_us", cost_us,
        "result", result,
        "tablet_status", tablet_status);
  }
  return ret;
}

int ObTabletRestoreTask::try_update_tablet_()
{
  int ret = OB_SUCCESS;
  ObTabletRestoreDag *dag = nullptr;
  ObSEArray<ObTabletID, 1> tablet_id_array;
  ObLS *ls = nullptr;
  bool is_exist = false;
  ObCopyTabletStatus::STATUS status = ObCopyTabletStatus::MAX_STATUS;
  ObIDagNet *dag_net = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (OB_ISNULL(dag = static_cast<ObTabletRestoreDag *>(this->get_dag()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet restore dag should not be NULL", K(ret), KP(dag));
  } else if (OB_FAIL(copy_tablet_ctx_->get_copy_tablet_status(status))) {
    LOG_WARN("failed to get copy tablet status", K(ret), KPC(dag), KPC(copy_tablet_ctx_));
  } else if (ObCopyTabletStatus::TABLET_NOT_EXIST == status) {
    //do nothing
  } else if (OB_FAIL(ctx_->ha_table_info_mgr_.check_tablet_table_info_exist(copy_tablet_ctx_->tablet_id_, is_exist))) {
    LOG_WARN("failed to check tablet table info exist", K(ret), KPC(copy_tablet_ctx_));
  } else if (is_exist) {
    //do nothing
  } else if (OB_FAIL(tablet_id_array.push_back(copy_tablet_ctx_->tablet_id_))) {
    LOG_WARN("failed to push tablet id into array", K(ret), KPC(copy_tablet_ctx_));
  } else if (OB_FAIL(dag->get_ls(ls))) {
    LOG_WARN("failed to get ls", K(ret), KPC(copy_tablet_ctx_));
  } else {
    if (OB_FAIL(ctx_->ha_table_info_mgr_.remove_tablet_table_info(copy_tablet_ctx_->tablet_id_))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to remove tablet info", K(ret), KPC(copy_tablet_ctx_), KPC(ctx_));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(dag_net = dag->get_dag_net())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("dag net should not be nullptr", K(ret), KP(dag_net));
    } else if (OB_ISNULL(helper_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("helper should not be NULL", K(ret), KP(helper_));
    } else if (copy_tablet_ctx_->tablet_id_.is_ls_inner_tablet()
                && OB_FAIL(ObRestoreDagNetUtils::create_or_update_tablets_with_helper(tablet_id_array,
                                                                                          helper_, dag_net, ls))) {
      LOG_WARN("failed to create or update inner tablet with helper", K(ret), KPC(ctx_));
    } else if (OB_FAIL(ObRestoreDagNetUtils::build_tablets_sstable_info_with_helper(tablet_id_array, helper_,
                                                                            &ctx_->ha_table_info_mgr_, dag_net, ls))) {
      LOG_WARN("failed to build tablets sstable info with helper", K(ret), KPC(ctx_), KPC(copy_tablet_ctx_));
    } else if (OB_FAIL(ctx_->ha_table_info_mgr_.check_tablet_table_info_exist(copy_tablet_ctx_->tablet_id_, is_exist))) {
      LOG_WARN("failed to check tablet table info exist", K(ret), KPC(copy_tablet_ctx_));
    } else if (!is_exist) {
      status = ObCopyTabletStatus::TABLET_NOT_EXIST;
      if (OB_FAIL(copy_tablet_ctx_->set_copy_tablet_status(status))) {
        LOG_WARN("failed to set copy tablet status", K(ret), KPC(dag));
      }
    }
  }
  return ret;
}

int ObTabletRestoreTask::update_ha_expected_status_(const ObCopyTabletStatus::STATUS &status)
{
  int ret = OB_SUCCESS;
  ObTabletRestoreDag *dag = nullptr;
  ObLS *ls = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (ObCopyTabletStatus::TABLET_NOT_EXIST != status) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("update ha expected status get invalid argument", K(ret), K(status));
  } else if (OB_ISNULL(dag = static_cast<ObTabletRestoreDag *>(dag_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet restore dag should not be NULL", K(ret), KP(dag));
  } else if (OB_FAIL(dag->get_ls(ls))) {
    LOG_WARN("failed to get ls", K(ret), KPC(copy_tablet_ctx_));
  } else {
    const ObTabletExpectedStatus::STATUS expected_status = ObTabletExpectedStatus::DELETED;
    ObTablet *tablet = nullptr;
    if (OB_ISNULL(tablet = copy_tablet_ctx_->tablet_handle_.get_obj())) {
      LOG_INFO("tablet is already deleted", "tablet_id", copy_tablet_ctx_->tablet_id_);
    } else if (OB_FAIL(ls->get_tablet_svr()->update_tablet_ha_expected_status(
                                                                    copy_tablet_ctx_->tablet_id_, expected_status))) {
      if (OB_TABLET_NOT_EXIST == ret) {
        LOG_INFO("restore tablet maybe deleted, skip it", K(ret), KPC(copy_tablet_ctx_));
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to update tablet ha expected status", K(ret), K(expected_status), KPC(copy_tablet_ctx_));
      }
    }
  }
  return ret;
}

int ObTabletRestoreTask::check_need_copy_sstable_(
    const ObITable::TableKey &table_key,
    bool &need_copy)
{
  int ret = OB_SUCCESS;
  need_copy = true;
  const blocksstable::ObMigrationSSTableParam *copy_table_info = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (!table_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check need copy sstable get invalid argument", K(ret), K(table_key));
  } else if (OB_FAIL(ctx_->ha_table_info_mgr_.get_table_info(copy_tablet_ctx_->tablet_id_, table_key, copy_table_info))) {
    LOG_WARN("failed to get table info", K(ret), KPC(copy_tablet_ctx_), K(table_key));
  } else if (OB_FAIL(ObStorageHATaskUtils::check_need_copy_sstable(*copy_table_info, false /* is_restore */, copy_tablet_ctx_->tablet_handle_, need_copy))) {
    LOG_WARN("failed to check need copy sstable", K(ret), KPC(copy_tablet_ctx_), K(table_key));
  }
  return ret;
}

int ObTabletRestoreTask::generate_mds_copy_tasks_(
    ObTabletCopyFinishTask *tablet_copy_finish_task,
    share::ObITask *&parent_task)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else if (OB_ISNULL(tablet_copy_finish_task) || OB_ISNULL(parent_task)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("generate mds copy task get invalid argument", K(ret), KP(tablet_copy_finish_task), KP(parent_task));
  } else if (OB_FAIL(generate_copy_tasks_(ObITable::is_mds_sstable, tablet_copy_finish_task, parent_task))) {
    LOG_WARN("failed to generate mds copy tasks", K(ret), KPC(copy_tablet_ctx_));
  }
  return ret;
}

int ObTabletRestoreTask::get_need_copy_sstable_info_key_(
    const common::ObIArray<ObITable::TableKey> &copy_table_key_array,
    common::ObIArray<ObITable::TableKey> &filter_table_key_array)
{
  int ret = OB_SUCCESS;
  filter_table_key_array.reset();
  const blocksstable::ObMigrationSSTableParam *sstable_param = nullptr;
  bool need_copy = true;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet restore task do not init", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < copy_table_key_array.count(); ++i) {
      need_copy = true;
      const ObITable::TableKey &table_key = copy_table_key_array.at(i);
      if (OB_FAIL(ctx_->ha_table_info_mgr_.get_table_info(copy_tablet_ctx_->tablet_id_, table_key, sstable_param))) {
        LOG_WARN("failed to get table info", K(ret), KPC(copy_tablet_ctx_), K(table_key));
      } else if (OB_FAIL(ObStorageHATaskUtils::check_need_copy_macro_blocks(*sstable_param,
          false /* is_leader_restore */,
          need_copy))) {
        LOG_WARN("failed to check need copy macro blocks", K(ret), K(table_key), KPC(sstable_param));
      } else if (!need_copy) {
        //do nothing
      } else if (OB_FAIL(filter_table_key_array.push_back(table_key))) {
        LOG_WARN("failed to push table key into array", K(ret), K(table_key));
      }
    }
    if (OB_SUCC(ret)) {
      LOG_INFO("succeed get need copy sstable info key", K(copy_table_key_array), K(filter_table_key_array));
    }
  }
  return ret;
}

/**************************ObTabletFinishRestoreTask***************************/
ObTabletFinishRestoreTask::ObTabletFinishRestoreTask()
  : share::ObITask(share::ObITask::TASK_TYPE_MIGRATE_PREPARE),
    is_inited_(false),
    task_gen_time_(0),
    copy_table_count_(0),
    ha_dag_net_ctx_(nullptr),
    copy_tablet_ctx_(nullptr),
    ls_(nullptr)
{
}

ObTabletFinishRestoreTask::~ObTabletFinishRestoreTask()
{
}

int ObTabletFinishRestoreTask::init(
    const int64_t task_gen_time,
    const int64_t copy_table_count,
    ObCopyTabletCtx &ctx,
    ObLS &ls)
{
  int ret = OB_SUCCESS;
  ObTabletRestoreDag *tablet_restore_dag = nullptr;
  share::ObIDag *dag = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet finish restore task already inited", K(ret));
  } else if (OB_ISNULL(dag = this->get_dag())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag should not be NULL", K(ret));
  } else if (ObDagType::DAG_TYPE_TABLET_RESTORE != dag->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag type is unexpected", K(ret), KPC(dag));
  } else if (OB_ISNULL(tablet_restore_dag = static_cast<ObTabletRestoreDag *>(dag))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet restore dag should not be NULL", K(ret));
  } else {
    task_gen_time_ = task_gen_time;
    copy_table_count_ = copy_table_count;
    copy_tablet_ctx_ = &ctx;
    ls_ = &ls;
    ha_dag_net_ctx_ = tablet_restore_dag->get_ctx();
    is_inited_ = true;
  }
  return ret;
}

int ObTabletFinishRestoreTask::process()
{
  int ret = OB_SUCCESS;
  LOG_INFO("start do tablet finish restore task", KPC(copy_tablet_ctx_));

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet finish restore task do not init", K(ret), KPC(copy_tablet_ctx_));
  } else {
    bool is_failed = false;
    if (ha_dag_net_ctx_->is_failed()) {
      is_failed = true;
    } else if (OB_FAIL(update_data_and_expected_status_())) {
      LOG_WARN("failed to update data and expected status", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(ObStorageHADagUtils::deal_with_fo(ret, this->get_dag()))) {
      LOG_WARN("failed to deal with fo", K(ret), K(tmp_ret), KPC(copy_tablet_ctx_));
    }
  }
  return ret;
}

int ObTabletFinishRestoreTask::update_data_and_expected_status_()
{
  int ret = OB_SUCCESS;
  ObCopyTabletStatus::STATUS status;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet finish restore task do not init", K(ret));
  } else if (OB_FAIL(copy_tablet_ctx_->get_copy_tablet_status(status))) {
    LOG_WARN("failed to get copy tablet status", KPC(copy_tablet_ctx_));
  } else if (ObCopyTabletStatus::TABLET_NOT_EXIST == status) {
    const ObTabletExpectedStatus::STATUS expected_status = ObTabletExpectedStatus::DELETED;
    if (OB_FAIL(ls_->get_tablet_svr()->update_tablet_ha_expected_status(copy_tablet_ctx_->tablet_id_, expected_status))) {
      if (OB_TABLET_NOT_EXIST == ret) {
        LOG_INFO("restore tablet maybe deleted, skip it", K(ret), KPC(copy_tablet_ctx_));
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to update tablet ha expected status", K(ret), K(expected_status), KPC(copy_tablet_ctx_));
      }
    } else {
      LOG_INFO("update tablet expected status", KPC(copy_tablet_ctx_), K(expected_status));
      SERVER_EVENT_ADD("storage_ha", "tablet_finish_restore_task",
          "tablet_id", copy_tablet_ctx_->tablet_id_.id(),
          "sstable_count", copy_table_count_,
          "cost_time_us", ObTimeUtility::current_time() - task_gen_time_,
          "expected_status", expected_status);
    }
  } else {
    const ObTabletDataStatus::STATUS data_status = ObTabletDataStatus::COMPLETE;
    if (OB_FAIL(ls_->update_tablet_ha_data_status(copy_tablet_ctx_->tablet_id_, data_status))) {
      if (OB_TABLET_NOT_EXIST == ret) {
        LOG_INFO("restore tablet maybe deleted, skip it", K(ret), KPC(copy_tablet_ctx_));
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to update tablet ha data status", K(ret), KPC(copy_tablet_ctx_), K(data_status));
      }
    } else {
      LOG_INFO("update tablet ha data status", KPC(copy_tablet_ctx_), K(data_status));
      SERVER_EVENT_ADD("storage_ha", "tablet_finish_restore_task",
          "tenant_id", MTL_ID(),
          "ls_id", ls_->get_ls_id().id(),
          "tablet_id", copy_tablet_ctx_->tablet_id_.id(),
          "sstable_count", copy_table_count_,
          "cost_time_us", ObTimeUtility::current_time() - task_gen_time_,
          "data_status", data_status);
    }
  }
  return ret;
}

/**************************ObDataTabletsRestoreDag***************************/
ObDataTabletsRestoreDag::ObDataTabletsRestoreDag()
  : ObRestoreDag(share::ObDagType::DAG_TYPE_DATA_TABLETS_META_RESTORE),
    is_inited_(false)
{
}

ObDataTabletsRestoreDag::~ObDataTabletsRestoreDag()
{
}

bool ObDataTabletsRestoreDag::operator == (const share::ObIDag &other) const
{
  bool is_same = true;
  if (this == &other) {
    // same
  } else if (get_type() != other.get_type()) {
    is_same = false;
  } else {
    const ObDataTabletsRestoreDag &other_dag = static_cast<const ObDataTabletsRestoreDag&>(other);
    ObRestoreDagNetCtx *ctx = get_ctx();
    ObRestoreDagNetCtx *other_ctx = other_dag.get_ctx();
    if (OB_ISNULL(ctx) || OB_ISNULL(other_ctx)) {
      is_same = false;
      LOG_ERROR_RET(OB_INVALID_ARGUMENT, "restore ctx should not be NULL", KP(ctx), KP(other_ctx));
    } else if (ctx->task_.task_id_ != other_ctx->task_.task_id_) {
      is_same = false;
    }
  }
  return is_same;
}

uint64_t ObDataTabletsRestoreDag::hash() const
{
  uint64_t hash_value = 0;
  ObRestoreDagNetCtx *ctx = get_ctx();
  if (OB_ISNULL(ctx)) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "restore dag ctx should not be NULL", KPC(ctx));
  } else {
    hash_value = common::murmurhash(
        &ctx->task_.task_id_, sizeof(ctx->task_.task_id_), hash_value);
    ObDagType::ObDagTypeEnum dag_type = get_type();
    hash_value = common::murmurhash(
        &dag_type, sizeof(dag_type), hash_value);
  }
  return hash_value;
}

int ObDataTabletsRestoreDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  ObRestoreDagNetCtx *ctx = nullptr;
  ObCStringHelper helper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("data tablets restore dag do not init", K(ret));
  } else if (OB_ISNULL(ctx = get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore dag ctx should not be null", K(ret), KPC(ctx));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(databuff_printf(buf, buf_len, "ObDataTabletsRestoreDag task_id=%s, task_type=%s",
                                        helper.convert(ctx->task_.task_id_),
                                        ObRestoreTaskType::get_str(ctx->task_.type_)))) {
    LOG_WARN("failed to fill dag key", K(ret), KPC(ctx));
  }
  return ret;
}

int ObDataTabletsRestoreDag::create_first_task()
{
  int ret = OB_SUCCESS;
  ObDataTabletsRestoreTask *task = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("data tablets restore dag do not init", K(ret));
  } else if (OB_FAIL(alloc_task(task))) {
    LOG_WARN("Fail to alloc task", K(ret));
  } else if (OB_FAIL(task->init())) {
    LOG_WARN("failed to init data tablets restore task", K(ret), KPC(ha_dag_net_ctx_));
  } else if (OB_FAIL(add_task(*task))) {
    LOG_WARN("Fail to add task", K(ret));
  } else {
    LOG_DEBUG("success to create first task", K(ret), KPC(this));
  }
  return ret;
}

int ObDataTabletsRestoreDag::init(share::ObIDagNet *dag_net)
{
  int ret = OB_SUCCESS;
  ObRestoreDagNet *restore_dag_net = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("data tablets restore dag init twice", K(ret));
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet*>(dag_net))) {
  } else if (FALSE_IT(ha_dag_net_ctx_ = restore_dag_net->get_ctx())) {
  } else if (OB_ISNULL(ha_dag_net_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ha_dag_net_ctx_));
  } else {
    is_inited_ = true;
  }
  return ret;
}

/**************************ObDataTabletsRestoreTask***************************/
ObDataTabletsRestoreTask::ObDataTabletsRestoreTask()
  : share::ObITask(share::ObITask::TASK_TYPE_MIGRATE_PREPARE),
    is_inited_(false),
    ctx_(nullptr),
    bandwidth_throttle_(nullptr),
    allocator_("DataTabletsRes"),
    helper_(nullptr),
    finish_dag_(nullptr)
{
}

ObDataTabletsRestoreTask::~ObDataTabletsRestoreTask()
{
  if (OB_NOT_NULL(helper_)) {
    helper_->destroy();
    helper_ = nullptr;
  }
}

int ObDataTabletsRestoreTask::init()
{
  int ret = OB_SUCCESS;
  ObIDagNet *dag_net = nullptr;
  ObRestoreDagNet *restore_dag_net = nullptr;
  ObSEArray<ObINodeWithChild*, 1> child_node_array;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("data tablets restore task init twice", K(ret));
  } else if (FALSE_IT(dag_net = this->get_dag()->get_dag_net())) {
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet *>(dag_net))) {
  } else if (OB_ISNULL(this->get_dag())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag should not be NULL", K(ret));
  } else {
    const common::ObIArray<ObINodeWithChild*> &child_node_array = this->get_dag()->get_child_nodes();
    if (child_node_array.count() != 1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("data tablets restore dag get unexpected child node", K(ret), K(child_node_array));
    } else {
      ObRestoreDag *child_dag = static_cast<ObRestoreDag*>(child_node_array.at(0));
      if (ObDagType::DAG_TYPE_FINISH_LS_RESTORE != child_dag->get_type()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("restore dag type is unexpected", K(ret), K(*child_dag));
      } else {
        ctx_ = restore_dag_net->get_ctx();
        bandwidth_throttle_ = restore_dag_net->get_bandwidth_throttle();
        finish_dag_ = static_cast<ObIDag*>(child_dag);
        ObIRestoreHelper *proto_helper = restore_dag_net->get_helper();
        if (OB_ISNULL(proto_helper)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("prototype helper should not be NULL", K(ret), KP(proto_helper));
        } else if (OB_FAIL(proto_helper->copy_for_task(allocator_, helper_))) {
          LOG_WARN("failed to copy task helper", K(ret), KP(proto_helper));
        } else if (OB_ISNULL(helper_) || !helper_->is_valid()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("task helper should not be null or invalid", K(ret), KP(helper_));
        } else {
          is_inited_ = true;
          LOG_INFO("succeed init data tablets restore task",
              "dag_id", *ObCurTraceId::get_trace_id(), "dag_net_id", ctx_->task_.task_id_);
        }
      }
    }
  }
  return ret;
}

int ObDataTabletsRestoreTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  int32_t result = OB_SUCCESS;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);
  LOG_INFO("start do data tablets restore task", K(ret), KPC(ctx_));

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("data tablets restore task do not init", K(ret));
  } else if (ctx_->is_failed()) {
    //do nothing
  } else if (OB_FAIL(set_restore_status_())) {
    LOG_WARN("failed to set restore status", KR(ret), KPC(ctx_));
  } else if (OB_FAIL(check_tx_data_continue_())) {
    LOG_WARN("failed to check tx data continue", K(ret), KPC(ctx_));
  } else if (OB_FAIL(ls_online_())) {
    LOG_WARN("failed to set ls online", K(ret), KPC(ctx_));
  } else if (OB_FAIL(trigger_log_sync_())) {
    LOG_WARN("failed to trigger log sync", KR(ret), KPC(ctx_));
  } else if (OB_FAIL(ObStorageHAUtils::check_log_status(MTL_ID(), sys_ls_id, result))) {
    LOG_WARN("failed to check log status", K(ret), KPC(ctx_));
  } else if (OB_SUCCESS != result) {
    LOG_INFO("can not replay log, it will retry", K(result), KPC(ctx_));
    if (OB_FAIL(ctx_->set_result(result/*result*/, true/*need_retry*/, this->get_dag()->get_type()))) {
      LOG_WARN("failed to set result", K(ret), KPC(ctx_));
    } else {
      ret = result;
      LOG_WARN("log sync or replay error, need retry", K(ret), KPC(ctx_));
    }
  } else if (OB_FAIL(build_tablet_group_info_())) {
    LOG_WARN("failed to build tablet group info", K(ret), KPC(ctx_));
  } else if (OB_FAIL(generate_tablet_group_dag_())) {
      LOG_WARN("failed to generate tablet group dag", K(ret), KPC(ctx_));
  }

  if (OB_TMP_FAIL(record_server_event_())) {
    LOG_WARN("failed to record server event", K(tmp_ret), K(ret));
  }

  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    bool allow_retry = true;
    if (OB_TMP_FAIL(try_offline_ls_())) {
      LOG_WARN("failed to try offline ls", K(tmp_ret));
    } else if (FALSE_IT(allow_retry = OB_SUCCESS == tmp_ret)) {
    }

    if (OB_TMP_FAIL(ObStorageHADagUtils::deal_with_fo(ret, this->get_dag(), allow_retry))) {
      LOG_WARN("failed to deal with fo", K(ret), K(tmp_ret), KPC(ctx_));
    }
  }

  return ret;
}

int ObDataTabletsRestoreTask::ls_online_()
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("data tablets restore task do not init", K(ret));
  } else if (OB_FAIL(ObStorageHADagUtils::get_ls(sys_ls_id, ls_handle))) {
    LOG_WARN("failed to get ls", K(ret), KPC(ctx_));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KPC(ctx_));
  } else if (OB_FAIL(ls->online())) {
    LOG_WARN("failed to online ls", K(ret), KPC(ctx_));
  } else {
    FLOG_INFO("succeed online ls for restore", K(ret), KPC(ctx_));
  }
  return ret;
}

int ObDataTabletsRestoreTask::trigger_log_sync_()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("data tablets restore task do not init", K(ret));
  } else {
    logservice::ObLogService *log_service = MTL(logservice::ObLogService*);
    if (OB_ISNULL(log_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log_service is null", KR(ret));
    } else {
      logservice::ObLogRestoreService *restore_service = log_service->get_log_restore_service();
      if (OB_ISNULL(restore_service)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("restore_service is null", KR(ret));
      } else {
        restore_service->signal();
        LOG_INFO("log sync triggered");
      }
    }
  }
  return ret;
}

int ObDataTabletsRestoreTask::set_restore_status_()
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("data tablets restore task do not init", K(ret));
  } else if (OB_FAIL(ObStorageHADagUtils::get_ls(sys_ls_id, ls_handle))) {
    LOG_WARN("failed to get ls", K(ret), KPC(ctx_));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KPC(ctx_));
  } else if (OB_FAIL(ls->set_restore_status(ObRestoreStatus(ObRestoreStatus::Status::RESTORE_DATA_TABLETS)))) {
    LOG_WARN("failed to set restore status", KR(ret), KPC(ctx_));
  }
  return ret;
}

int ObDataTabletsRestoreTask::build_tablet_group_info_()
{
  int ret = OB_SUCCESS;
  ObCopyTabletSimpleInfo tablet_simple_info;
  ObArray<ObLogicTabletID> tablet_group_id_array;
  ObArray<ObLogicTabletID> tablet_id_array;
  hash::ObHashSet<ObTabletID> remove_tablet_set;
  int64_t total_size = 0;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("data tablets restore task do not init", K(ret));
  } else {
    ctx_->tablet_group_mgr_.reuse();
    const hash::ObHashMap<common::ObTabletID, ObCopyTabletSimpleInfo> &tablet_simple_info_map =
        ctx_->tablet_simple_info_map_;
    const ObHATabletGroupCtx::TabletGroupCtxType type = ObHATabletGroupCtx::TabletGroupCtxType::NORMAL_TYPE;

    for (int64_t i = 0; OB_SUCC(ret) && i < ctx_->data_tablet_id_array_.count(); ++i) {
      tablet_simple_info.reset();
      const ObLogicTabletID &logic_tablet_id = ctx_->data_tablet_id_array_.at(i);
      tablet_group_id_array.reset();

      if (OB_FAIL(tablet_simple_info_map.get_refactored(logic_tablet_id.tablet_id_, tablet_simple_info))) {
        if (OB_HASH_NOT_EXIST == ret) {
          FLOG_INFO("tablet do not exist in src ls, skip it", K(logic_tablet_id));
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to get tablet simple info", K(ret), K(logic_tablet_id));
        }
      } else if (!tablet_simple_info.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet simple info is not valid", K(ret), K(tablet_simple_info));
      } else if (tablet_simple_info.data_size_ >= MAX_TABLET_GROUP_SIZE
          && ObCopyTabletStatus::TABLET_EXIST == tablet_simple_info.status_) {
        if (OB_FAIL(tablet_group_id_array.push_back(logic_tablet_id))) {
          LOG_WARN("failed to push tablet id into array", K(ret), K(logic_tablet_id));
        } else if (OB_FAIL(ctx_->tablet_group_mgr_.build_tablet_group_ctx(tablet_group_id_array, type))) {
          LOG_WARN("failed to build tablet group ctx", K(ret), KPC(ctx_));
        } else {
          LOG_INFO("succeed build tablet group ctx", K(tablet_group_id_array));
        }
      } else if (OB_FAIL(tablet_id_array.push_back(logic_tablet_id))) {
        LOG_WARN("failed to push tablet id into array", K(ret), K(logic_tablet_id));
      }
    }

    if (OB_SUCC(ret)) {
      if (tablet_id_array.empty()) {
        //do nothing
      } else if (OB_FAIL(remove_tablet_set.create(tablet_id_array.count()))) {
        LOG_WARN("failed to create remove tablet set", K(ret), K(tablet_id_array));
      } else {
        LOG_INFO("need tablet group list", K(tablet_id_array));
        for (int64_t i = 0; OB_SUCC(ret) && i < tablet_id_array.count(); ++i) {
          tablet_simple_info.reset();
          const ObLogicTabletID &logic_tablet_id = tablet_id_array.at(i);
          tablet_group_id_array.reset();
          total_size = 0;
          int hash_ret = OB_SUCCESS;

          if (OB_FAIL(tablet_simple_info_map.get_refactored(logic_tablet_id.tablet_id_, tablet_simple_info))) {
            LOG_WARN("failed to get tablet simple info", K(ret), K(logic_tablet_id));
          } else if (FALSE_IT(hash_ret = remove_tablet_set.exist_refactored(logic_tablet_id.tablet_id_))) {
          } else if (OB_HASH_EXIST == hash_ret) {
            //do nothing
          } else if (hash_ret != OB_HASH_NOT_EXIST) {
            ret = OB_SUCCESS == hash_ret ? OB_ERR_UNEXPECTED : hash_ret;
            LOG_WARN("failed to check remove tablet exist", K(ret), K(logic_tablet_id));
          } else if (OB_FAIL(tablet_group_id_array.push_back(logic_tablet_id))) {
            LOG_WARN("failed to push tablet id into array", K(ret), K(logic_tablet_id));
          } else {
            total_size = tablet_simple_info.data_size_;
            int64_t max_tablet_count = 0;
            for (int64_t j = i + 1; OB_SUCC(ret) && j < tablet_id_array.count() && max_tablet_count < MAX_TABLET_COUNT; ++j) {
              const ObLogicTabletID &tmp_tablet_id = tablet_id_array.at(j);
              ObCopyTabletSimpleInfo tmp_tablet_simple_info;

              if (FALSE_IT(hash_ret = remove_tablet_set.exist_refactored(tmp_tablet_id.tablet_id_))) {
              } else if (OB_HASH_EXIST == hash_ret) {
                //do nothing
              } else if (hash_ret != OB_HASH_NOT_EXIST) {
                ret = OB_SUCCESS == hash_ret ? OB_ERR_UNEXPECTED : hash_ret;
                LOG_WARN("failed to check remove tablet exist", K(ret), K(logic_tablet_id));
              } else if (OB_FAIL(tablet_simple_info_map.get_refactored(tmp_tablet_id.tablet_id_, tmp_tablet_simple_info))) {
                LOG_WARN("failed to get tablet simple info", K(ret), K(tmp_tablet_id));
              } else if (total_size + tmp_tablet_simple_info.data_size_ <= MAX_TABLET_GROUP_SIZE) {
                if (OB_FAIL(tablet_group_id_array.push_back(tmp_tablet_id))) {
                  LOG_WARN("failed to set tablet id into array", K(ret), K(tmp_tablet_id));
                } else if (OB_FAIL(remove_tablet_set.set_refactored(tmp_tablet_id.tablet_id_))) {
                  LOG_WARN("failed to set tablet into set", K(ret), K(tmp_tablet_id));
                } else {
                  total_size += tmp_tablet_simple_info.data_size_;
                  max_tablet_count++;
                }
              }
            }

            if (OB_SUCC(ret)) {
              if (OB_FAIL(ctx_->tablet_group_mgr_.build_tablet_group_ctx(tablet_group_id_array, type))) {
                LOG_WARN("failed to build tablet group ctx", K(ret), K(tablet_group_id_array));
              } else {
                LOG_INFO("succeed build tablet group ctx", K(tablet_group_id_array), K(total_size));
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDataTabletsRestoreTask::generate_tablet_group_dag_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTabletGroupRestoreDag *tablet_group_dag = nullptr;
  ObTenantDagScheduler *scheduler = nullptr;
  ObIDagNet *dag_net = nullptr;
  ObDataTabletsRestoreDag *data_tablets_restore_dag = nullptr;
  ObHATabletGroupCtx *tablet_group_ctx = nullptr;
  ObArray<ObLogicTabletID> tablet_id_array;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("data tablets restore task do not init", K(ret));
  } else if (OB_FAIL(ctx_->tablet_group_mgr_.get_next_tablet_group_ctx(tablet_group_ctx))) {
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get next tablet group ctx", K(ret), KPC(ctx_));
    }
  } else if (OB_FAIL(tablet_group_ctx->get_all_tablet_ids(tablet_id_array))) {
    LOG_WARN("failed to get all tablet ids", K(ret), KPC(ctx_));
  } else if (OB_ISNULL(data_tablets_restore_dag = static_cast<ObDataTabletsRestoreDag *>(this->get_dag()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("data tablets restore dag should not be NULL", K(ret), KP(data_tablets_restore_dag));
  } else if (OB_ISNULL(dag_net = data_tablets_restore_dag->get_dag_net())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), K(*this));
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", K(ret));
  } else {
    if (OB_FAIL(scheduler->alloc_dag(tablet_group_dag))) {
      LOG_WARN("failed to alloc tablet group restore dag ", K(ret));
    } else if (OB_FAIL(tablet_group_dag->init(tablet_id_array, dag_net, finish_dag_, tablet_group_ctx))) {
      LOG_WARN("failed to init tablet group dag", K(ret), K(tablet_id_array));
    } else if (OB_FAIL(dag_net->add_dag_into_dag_net(*tablet_group_dag))) {
      LOG_WARN("failed to add dag into dag net", K(ret), KPC(tablet_group_dag));
    } else if (OB_FAIL(this->get_dag()->add_child_without_inheritance(*tablet_group_dag))) {
      LOG_WARN("failed to add tablet group dag as child", K(ret), K(*tablet_group_dag));
    } else if (OB_FAIL(tablet_group_dag->create_first_task())) {
      LOG_WARN("failed to create first task", K(ret), K(*ctx_));
    } else if (OB_FAIL(tablet_group_dag->add_child_without_inheritance(*finish_dag_))) {
      LOG_WARN("failed to add finish dag as child", K(ret), K(*tablet_group_dag), K(*finish_dag_));
    } else if (OB_FAIL(scheduler->add_dag(tablet_group_dag))) {
      LOG_WARN("failed to add tablet group restore dag", K(ret), K(*tablet_group_dag));
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_WARN("Fail to add task", K(ret));
        ret = OB_EAGAIN;
      }
    }

    if (OB_FAIL(ret)) {
      if (OB_NOT_NULL(tablet_group_dag)) {
        scheduler->free_dag(*tablet_group_dag);
        tablet_group_dag = nullptr;
      }
    }
  }
  return ret;
}

int ObDataTabletsRestoreTask::try_offline_ls_()
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("data tablets restore task do not init", K(ret));
  } else if (OB_FAIL(ObStorageHADagUtils::get_ls(sys_ls_id, ls_handle))) {
    LOG_WARN("failed to get ls", K(ret), KPC(ctx_));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KPC(ctx_));
  } else if (OB_FAIL(ls->offline())) {
    LOG_WARN("failed to offline ls", K(ret), KPC(ctx_));
  }
  return ret;
}

int ObDataTabletsRestoreTask::check_tx_data_continue_()
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  share::SCN tx_data_recycle_scn;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("data tablets restore task do not init", K(ret));
  } else if (OB_FAIL(ObStorageHADagUtils::get_ls(sys_ls_id, ls_handle))) {
    LOG_WARN("failed to get ls", K(ret), KPC(ctx_));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KPC(ctx_));
  } else if (OB_FAIL(ls->get_tx_data_sstable_recycle_scn(tx_data_recycle_scn))) {
    LOG_WARN("failed to get tx data recycle scn", K(ret), KPC(ctx_));
  } else if (!ctx_->src_ls_meta_package_.tx_data_recycle_scn_.is_valid()) {
    //do nothing
  } else if (ctx_->src_ls_meta_package_.tx_data_recycle_scn_ != tx_data_recycle_scn) {
    ret = OB_MIGRATE_TX_DATA_NOT_CONTINUES;
    LOG_WARN("src tx data is already recycle, need retry", K(ret), KPC(ctx_), K(tx_data_recycle_scn));
  }
  return ret;
}

int ObDataTabletsRestoreTask::record_server_event_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx should not be null", K(ret), KPC_(ctx));
  } else {
    share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);
    SERVER_EVENT_ADD("storage_ha", "data_tablets_restore_task",
        "src", to_cstring(ctx_->task_.src_info_),
        "task_id", to_cstring(ctx_->task_.task_id_),
        "is_failed", ctx_->is_failed(),
        "task_type", ObRestoreTaskType::get_str(ctx_->task_.type_));
  }
  return ret;
}

/**************************ObTabletGroupRestoreDag***************************/
ObTabletGroupRestoreDag::ObTabletGroupRestoreDag()
  : ObRestoreDag(share::ObDagType::DAG_TYPE_TABLET_GROUP_META_RESTORE),
    is_inited_(false),
    tablet_id_array_(),
    finish_dag_(nullptr),
    tablet_group_ctx_(nullptr)
{
}

ObTabletGroupRestoreDag::~ObTabletGroupRestoreDag()
{
}

bool ObTabletGroupRestoreDag::operator == (const share::ObIDag &other) const
{
  bool is_same = true;
  if (this == &other) {
    // same
  } else if (get_type() != other.get_type()) {
    is_same = false;
  } else {
    const ObRestoreDag &other_dag = static_cast<const ObRestoreDag&>(other);
    ObRestoreDagNetCtx *ctx = get_ctx();
    if (NULL != ctx && NULL != other_dag.get_ctx()) {
      if (ctx->task_.task_id_ != other_dag.get_ctx()->task_.task_id_) {
        is_same = false;
      } else {
        const ObTabletGroupRestoreDag &tablet_group_dag = static_cast<const ObTabletGroupRestoreDag&>(other);
        if (tablet_id_array_.count() != tablet_group_dag.tablet_id_array_.count()) {
          is_same = false;
        } else {
          for (int64_t i = 0; is_same && i < tablet_id_array_.count(); ++i) {
            if (tablet_id_array_.at(i) != tablet_group_dag.tablet_id_array_.at(i)) {
              is_same = false;
            }
          }
        }
      }
    }
  }
  return is_same;
}

uint64_t ObTabletGroupRestoreDag::hash() const
{
  uint64_t hash_value = 0;
  ObRestoreDagNetCtx *ctx = get_ctx();
  if (NULL != ctx) {
    ObDagType::ObDagTypeEnum dag_type = get_type();
    hash_value = common::murmurhash(
        &dag_type, sizeof(dag_type), hash_value);
    for (int64_t i = 0; i < tablet_id_array_.count(); ++i) {
      hash_value = common::murmurhash(
          &tablet_id_array_.at(i).tablet_id_, sizeof(tablet_id_array_.at(i).tablet_id_), hash_value);
    }
  }
  return hash_value;
}

int ObTabletGroupRestoreDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  ObRestoreDagNetCtx *ctx = nullptr;
  ObCStringHelper helper;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet group restore dag do not init", K(ret));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_ISNULL(ctx = get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ctx));
  } else if (OB_FAIL(databuff_printf(buf, buf_len,
                          "ObTabletGroupRestoreDag: restore_type = %s, first_tablet_id = %s",
                          ObRestoreTaskType::get_str(ctx->task_.type_),
                          helper.convert(tablet_id_array_.at(0).tablet_id_)))) {
    LOG_WARN("failed to fill dag key", K(ret), KPC(ctx));
  }
  return ret;
}

int ObTabletGroupRestoreDag::init(
    const common::ObIArray<ObLogicTabletID> &tablet_id_array,
    share::ObIDagNet *dag_net,
    share::ObIDag *finish_dag,
    ObHATabletGroupCtx *tablet_group_ctx)
{
  int ret = OB_SUCCESS;
  ObRestoreDagNet *restore_dag_net = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet group restore dag init twice", K(ret));
  } else if (tablet_id_array.empty() || OB_ISNULL(dag_net) || OB_ISNULL(finish_dag) || OB_ISNULL(tablet_group_ctx)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet group restore init get invalid argument", K(ret), KP(dag_net), KP(finish_dag), KP(tablet_group_ctx));
  } else if (OB_FAIL(tablet_id_array_.assign(tablet_id_array))) {
    LOG_WARN("failed to assign tablet id array", K(ret), K(tablet_id_array));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet*>(dag_net))) {
  } else if (FALSE_IT(ha_dag_net_ctx_ = restore_dag_net->get_ctx())) {
  } else if (OB_ISNULL(ha_dag_net_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ha_dag_net_ctx_));
  } else {
    finish_dag_ = finish_dag;
    tablet_group_ctx_ = tablet_group_ctx;
    is_inited_ = true;
  }
  return ret;
}

int ObTabletGroupRestoreDag::create_first_task()
{
  int ret = OB_SUCCESS;
  ObTabletGroupRestoreTask *task = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet group restore dag do not init", K(ret));
  } else if (OB_FAIL(alloc_task(task))) {
    LOG_WARN("failed to alloc tablet group restore task", K(ret));
  } else if (OB_FAIL(task->init(tablet_id_array_, finish_dag_, tablet_group_ctx_))) {
    LOG_WARN("failed to init tablet group restore task", K(ret));
  } else if (OB_FAIL(add_task(*task))) {
    LOG_WARN("failed to add task", K(ret));
  }
  return ret;
}

int ObTabletGroupRestoreDag::generate_next_dag(share::ObIDag *&dag)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  dag = nullptr;
  ObTenantDagScheduler *scheduler = nullptr;
  ObIDagNet *dag_net = nullptr;
  ObTabletGroupRestoreDag *tablet_group_restore_dag = nullptr;
  bool need_set_failed_result = true;
  ObRestoreDagNetCtx *ctx = nullptr;
  ObHATabletGroupCtx *tablet_group_ctx = nullptr;
  ObArray<ObLogicTabletID> tablet_id_array;
  ObDagId dag_id;
  const int64_t start_ts = ObTimeUtil::current_time();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet group restore dag do not init", K(ret));
  } else if (OB_ISNULL(ctx = get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ctx));
  } else if (ctx->is_failed()) {
    if (OB_TMP_FAIL(ctx->get_result(ret))) {
      LOG_WARN("failed to get result", K(tmp_ret), KPC(ctx));
      ret = tmp_ret;
    }
  } else if (OB_FAIL(ctx->tablet_group_mgr_.get_next_tablet_group_ctx(tablet_group_ctx))) {
    if (OB_ITER_END == ret) {
      //do nothing
      need_set_failed_result = false;
    } else {
      LOG_WARN("failed to get group ctx", K(ret), KPC(ctx));
    }
  } else if (FALSE_IT(dag_id.init(MYADDR))) {
  } else if (OB_FAIL(tablet_group_ctx->get_all_tablet_ids(tablet_id_array))) {
    LOG_WARN("failed to get all tablet ids", K(ret), KPC(ctx));
  } else if (OB_ISNULL(dag_net = this->get_dag_net())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore dag net should not be NULL", K(ret), KP(dag_net));
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", K(ret));
  } else if (OB_FAIL(scheduler->alloc_dag(tablet_group_restore_dag))) {
    LOG_WARN("failed to alloc tablet group restore dag", K(ret));
  } else if (OB_FAIL(tablet_group_restore_dag->init(tablet_id_array, dag_net, finish_dag_, tablet_group_ctx))) {
    LOG_WARN("failed to init tablet group restore dag", K(ret), KPC(ctx));
  } else if (OB_FAIL(tablet_group_restore_dag->set_dag_id(dag_id))) {
    LOG_WARN("failed to set dag id", K(ret), KPC(ctx));
  } else {
    LOG_INFO("succeed generate next dag", KPC(tablet_group_restore_dag));
    dag = tablet_group_restore_dag;
    tablet_group_restore_dag = nullptr;
  }

  if (OB_NOT_NULL(tablet_group_restore_dag)) {
    scheduler->free_dag(*tablet_group_restore_dag);
    tablet_group_restore_dag = nullptr;
  }

  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    const bool need_retry = false;
    if (need_set_failed_result && OB_TMP_FAIL(ha_dag_net_ctx_->set_result(ret, need_retry, get_type()))) {
      LOG_WARN("failed to set result", K(ret), KPC(ha_dag_net_ctx_));
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("generate_next_tablet_group dag", "cost", ObTimeUtil::current_time() - start_ts,
        "dag_id", dag_id, "dag_net_id", ctx->task_.task_id_);
  }

  return ret;
}

int ObTabletGroupRestoreDag::fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const
{
  int ret = OB_SUCCESS;
  ObRestoreDagNetCtx *ctx = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet group restore dag do not init", K(ret));
  } else if (OB_ISNULL(ctx = get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore dag ctx should not be NULL", K(ret), KP(ctx));
  } else {
    ObCStringHelper helper;
    if (OB_FAIL(ADD_DAG_WARN_INFO_PARAM(out_param, allocator, get_type(),
                                          static_cast<int64_t>(tablet_id_array_.at(0).tablet_id_.id()),
                                          "dag_net_task_id", helper.convert(ctx->task_.task_id_)))) {
      LOG_WARN("failed to fill info param", K(ret));
    }
  }
  return ret;
}

int ObTabletGroupRestoreDag::check_is_in_retry(bool &is_in_retry)
{
  int ret = OB_SUCCESS;
  is_in_retry = false;
  int32_t retry_count = 0;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage ha dag do not init", K(ret), KP(ha_dag_net_ctx_));
  } else if (OB_FAIL(result_mgr_.get_retry_count(retry_count))) {
    LOG_WARN("failed to check need retry", K(ret), K(*this));
  } else {
    is_in_retry = retry_count > 0;
  }
  return ret;
}

/**************************ObTabletGroupRestoreTask***************************/
ObTabletGroupRestoreTask::ObTabletGroupRestoreTask()
  : share::ObITask(share::ObITask::TASK_TYPE_MIGRATE_PREPARE),
    is_inited_(false),
    ls_handle_(),
    ctx_(nullptr),
    bandwidth_throttle_(nullptr),
    helper_(nullptr),
    tablet_id_array_(),
    finish_dag_(nullptr),
    ha_tablets_builder_(),
    tablet_group_ctx_(nullptr)
{
}

ObTabletGroupRestoreTask::~ObTabletGroupRestoreTask()
{
}

int ObTabletGroupRestoreTask::init(
    const common::ObIArray<ObLogicTabletID> &tablet_id_array,
    share::ObIDag *finish_dag,
    ObHATabletGroupCtx *tablet_group_ctx)
{
  int ret = OB_SUCCESS;
  ObIDagNet *dag_net = nullptr;
  ObRestoreDagNet *restore_dag_net = nullptr;
  share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet group restore task init twice", K(ret));
  } else if (tablet_id_array.empty() || OB_ISNULL(finish_dag)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet group restore task init get invalid argument", K(ret), K(tablet_id_array), KP(finish_dag));
  } else if (OB_FAIL(tablet_id_array_.assign(tablet_id_array))) {
    LOG_WARN("failed to assign tablet id array", K(ret));
  } else if (FALSE_IT(dag_net = this->get_dag()->get_dag_net())) {
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet*>(dag_net))) {
  } else {
    ctx_ = restore_dag_net->get_ctx();
    ObIRestoreHelper *proto_helper = nullptr;
    proto_helper = restore_dag_net->get_helper();
    finish_dag_ = finish_dag;
    tablet_group_ctx_ = tablet_group_ctx;
    if (OB_ISNULL(ctx_) || OB_ISNULL(proto_helper)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ctx or helper should not be NULL", K(ret), KP(ctx_), KP(proto_helper));
    } else if (OB_FAIL(proto_helper->copy_for_task(allocator_, helper_))) {
      LOG_WARN("failed to copy task helper", K(ret), KP(proto_helper));
    } else if (OB_ISNULL(helper_) || !helper_->is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("task helper should not be null or invalid", K(ret), KP(helper_));
    } else if (OB_FAIL(ObStorageHADagUtils::get_ls(sys_ls_id, ls_handle_))) {
      LOG_WARN("failed to get ls", K(ret), KPC(ctx_));
    } else {
      is_inited_ = true;
      LOG_INFO("succeed init tablet group restore task", "ls id", sys_ls_id,
                  "dag_id", *ObCurTraceId::get_trace_id(), "dag_net_id", ctx_->task_.task_id_, K(tablet_id_array));
    }
  }
  return ret;
}

int ObTabletGroupRestoreTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  LOG_INFO("start do tablet group restore task", K(ret), K(tablet_id_array_));

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet group restore task do not init", K(ret));
  } else if (ctx_->is_failed()) {
    //do nothing
  } else if (OB_FAIL(try_remove_tablets_info_())) {
    LOG_WARN("failed to try remove tablets info", K(ret), KPC(ctx_));
  } else if (OB_FAIL(build_tablets_sstable_info_())) {
    LOG_WARN("failed to build tablets sstable info", K(ret));
  } else if (OB_FAIL(generate_tablet_restore_dag_())) {
    LOG_WARN("failed to generate tablet restore dag", K(ret));
  }

  if (OB_TMP_FAIL(record_server_event_())) {
    LOG_WARN("failed to record server event", K(tmp_ret), K(ret));
  }

  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(ObStorageHADagUtils::deal_with_fo(ret, this->get_dag()))) {
      LOG_WARN("failed to deal with fo", K(ret), K(tmp_ret), KPC(ctx_));
    }
  }

  return ret;
}

int ObTabletGroupRestoreTask::build_tablets_sstable_info_()
{
  int ret = OB_SUCCESS;
  ObIDagNet *dag_net = nullptr;
  ObLS *ls = nullptr;
  ObArray<ObTabletID> tablet_id_array;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet group restore task do not init", K(ret), KPC(ctx_));
  } else if (OB_ISNULL(this->get_dag())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag should not be nullptr", K(ret), KP(this->get_dag()));
  } else if (OB_ISNULL(dag_net = this->get_dag()->get_dag_net())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be nullptr", K(ret), KP(dag_net));
  } else if (OB_ISNULL(ctx_) || OB_ISNULL(helper_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx or helper should not be NULL", K(ret), KP(ctx_), KP(helper_));
  } else if (OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KP(ls), KPC(ctx_));
  } else if (OB_FAIL(ObStorageHAUtils::append_tablet_list(tablet_id_array_, tablet_id_array))) {
    LOG_WARN("failed to append tablet list", K(ret), K(tablet_id_array_), KPC(ctx_));
  } else if (OB_FAIL(ObRestoreDagNetUtils::build_tablets_sstable_info_with_helper(tablet_id_array, helper_,
                                                                          &ctx_->ha_table_info_mgr_, dag_net, ls))) {
    LOG_WARN("failed to build tablets sstable info with helper", K(ret), KPC(ctx_));
  }
  return ret;
}


int ObTabletGroupRestoreTask::generate_tablet_restore_dag_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTenantDagScheduler *scheduler = nullptr;
  ObIDagNet *dag_net = nullptr;
  ObTabletGroupRestoreDag *tablet_group_restore_dag = nullptr;
  ObTabletRestoreDag *tablet_restore_dag = nullptr;
  ObLS *ls = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet group restore task do not init", K(ret));
  } else if (OB_ISNULL(tablet_group_restore_dag = static_cast<ObTabletGroupRestoreDag *>(this->get_dag()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet group restore dag should not be NULL", K(ret), KP(tablet_group_restore_dag));
  } else if (OB_ISNULL(dag_net = tablet_group_restore_dag->get_dag_net())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore dag net should not be NULL", K(ret), KP(dag_net));
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", K(ret));
  } else if (OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KP(ls));
  } else {
    ObIDag *parent = this->get_dag();
    ObLogicTabletID logic_tablet_id;
    //generate next_dag can execute successful generation only if the first dag is successfully generated
    while (OB_SUCC(ret)) {
      ObTabletHandle tablet_handle;
      ObTablet *tablet = nullptr;
      logic_tablet_id.reset();
      if (OB_FAIL(tablet_group_ctx_->get_next_tablet_id(logic_tablet_id))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to get next tablet id", K(ret), KPC(ctx_));
        }
      } else if (OB_FAIL(ls->ha_get_tablet(logic_tablet_id.tablet_id_, tablet_handle))) {
        if (OB_TABLET_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          LOG_INFO("tablet not exist, skip it", K(logic_tablet_id));
        } else {
          LOG_WARN("failed to get tablet", K(ret), K(logic_tablet_id));
        }
      } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet should not be NULL", K(ret), K(tablet_handle), K(logic_tablet_id));
      } else if (OB_FAIL(scheduler->alloc_dag(tablet_restore_dag))) {
        LOG_WARN("failed to alloc tablet restore dag", K(ret));
      } else if (OB_FAIL(tablet_restore_dag->init(logic_tablet_id.tablet_id_, tablet_handle, dag_net, tablet_group_ctx_, ObTabletRestoreDag::ObTabletType::DATA_TABLET_TYPE))) {
        LOG_WARN("failed to init tablet restore dag", K(ret), K(*ctx_));
      } else if (OB_FAIL(dag_net->add_dag_into_dag_net(*tablet_restore_dag))) {
        LOG_WARN("failed to add dag into dag net", K(ret), K(*ctx_));
      } else if (OB_FAIL(parent->add_child_without_inheritance(*tablet_restore_dag))) {
        LOG_WARN("failed to add child dag", K(ret), K(*ctx_));
      } else if (OB_FAIL(tablet_restore_dag->create_first_task())) {
        LOG_WARN("failed to create first task", K(ret), K(*ctx_));
      } else if (OB_FAIL(tablet_restore_dag->add_child_without_inheritance(*finish_dag_))) {
        LOG_WARN("failed to add finish dag as child", K(ret), K(*ctx_));
      } else if (OB_FAIL(scheduler->add_dag(tablet_restore_dag))) {
        LOG_WARN("failed to add tablet restore dag", K(ret), K(*tablet_restore_dag));
        if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
          LOG_WARN("Fail to add task", K(ret));
          ret = OB_EAGAIN;
        }
      } else {
        LOG_INFO("succeed to schedule tablet restore dag", K(*tablet_restore_dag), K(logic_tablet_id));
        break;
      }
    }

    if (OB_FAIL(ret)) {
      if (OB_NOT_NULL(tablet_restore_dag)) {
        scheduler->free_dag(*tablet_restore_dag);
        tablet_restore_dag = nullptr;
      }
    }
  }
  return ret;
}

int ObTabletGroupRestoreTask::try_remove_tablets_info_()
{
  int ret = OB_SUCCESS;
  bool is_in_retry = false;
  ObTabletGroupRestoreDag *dag = nullptr;
  ObIHADagNetCtx *ha_dag_net_ctx = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet group restore task do not init", K(ret), KPC(ctx_));
  } else if (OB_ISNULL(dag = static_cast<ObTabletGroupRestoreDag *>(this->get_dag()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet group restore dag should not be NULL", K(ret), KPC(ctx_), KP(dag));
  } else if (OB_ISNULL(ha_dag_net_ctx = dag->get_ha_dag_net_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ha dag net ctx should not be NULL", K(ret), KPC(ctx_), KP(dag));
  } else if (OB_FAIL(dag->check_is_in_retry(is_in_retry))) {
    LOG_WARN("failed to check is in retry", K(ret), KPC(ctx_), KP(dag));
  } else if (is_in_retry) {
    if (OB_FAIL(remove_tablets_info_())) {
      LOG_WARN("failed to try remove tablets info", K(ret), KPC(ctx_));
    }
  }
  return ret;
}

int ObTabletGroupRestoreTask::remove_tablets_info_()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet group restore task do not init", K(ret), KPC(ctx_));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_id_array_.count(); ++i) {
      const ObTabletID &tablet_id = tablet_id_array_.at(i).tablet_id_;
      if (OB_FAIL(ctx_->ha_table_info_mgr_.remove_tablet_table_info(tablet_id))) {
        if (OB_HASH_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to remove tablet info", K(ret), K(tablet_id), KPC(ctx_));
        }
      }
    }
  }
  return ret;
}

int ObTabletGroupRestoreTask::record_server_event_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx should not be null", K(ret), KPC_(ctx));
  } else {
    share::ObLSID sys_ls_id(share::ObLSID::SYS_LS_ID);
    SERVER_EVENT_ADD("storage_ha", "tablet_group_restore_task",
        "src", to_cstring(ctx_->task_.src_info_),
        "task_id", to_cstring(ctx_->task_.task_id_),
        "tablet_count", tablet_id_array_.count(),
        "task_type", ObRestoreTaskType::get_str(ctx_->task_.type_));
  }
  return ret;
}

/**************************ObRestoreFinishDag***************************/
ObRestoreFinishDag::ObRestoreFinishDag()
  : ObRestoreDag(share::ObDagType::DAG_TYPE_FINISH_LS_RESTORE),
    is_inited_(false)
{
}

ObRestoreFinishDag::~ObRestoreFinishDag()
{
}

bool ObRestoreFinishDag::operator == (const share::ObIDag &other) const
{
  bool is_same = true;
  if (this == &other) {
    // same
  } else if (get_type() != other.get_type()) {
    is_same = false;
  } else {
    const ObRestoreFinishDag &other_dag = static_cast<const ObRestoreFinishDag&>(other);
    ObRestoreDagNetCtx *ctx = get_ctx();
    ObRestoreDagNetCtx *other_ctx = other_dag.get_ctx();
    if (OB_ISNULL(ctx) || OB_ISNULL(other_ctx)) {
      is_same = false;
      LOG_ERROR_RET(OB_INVALID_ARGUMENT, "restore ctx should not be NULL", KP(ctx), KP(other_ctx));
    } else if (ctx->task_.task_id_ != other_ctx->task_.task_id_) {
      is_same = false;
    }
  }
  return is_same;
}

uint64_t ObRestoreFinishDag::hash() const
{
  uint64_t hash_value = 0;
  ObRestoreDagNetCtx *ctx = get_ctx();
  if (OB_ISNULL(ctx)) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "restore dag ctx should not be NULL", KPC(ctx));
  } else {
    hash_value = common::murmurhash(&ctx->task_.task_id_, sizeof(ctx->task_.task_id_), hash_value);
    ObDagType::ObDagTypeEnum dag_type = get_type();
    hash_value = common::murmurhash(&dag_type, sizeof(dag_type), hash_value);
  }
  return hash_value;
}

int ObRestoreFinishDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  ObRestoreDagNetCtx *ctx = nullptr;
  ObCStringHelper helper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore finish dag do not init", K(ret));
  } else if (OB_ISNULL(ctx = get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore dag ctx should not be null", K(ret), KPC(ctx));
  } else if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(databuff_printf(buf, buf_len, "ObRestoreFinishDag task_id=%s, task_type=%s",
                                        helper.convert(ctx->task_.task_id_),
                                        ObRestoreTaskType::get_str(ctx->task_.type_)))) {
    LOG_WARN("failed to fill dag key", K(ret), KPC(ctx));
  }
  return ret;
}

int ObRestoreFinishDag::create_first_task()
{
  int ret = OB_SUCCESS;
  ObRestoreFinishTask *task = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore finish dag do not init", K(ret));
  } else if (OB_FAIL(alloc_task(task))) {
    LOG_WARN("Fail to alloc task", K(ret));
  } else if (OB_FAIL(task->init())) {
    LOG_WARN("failed to init restore finish task", K(ret), KPC(ha_dag_net_ctx_));
  } else if (OB_FAIL(add_task(*task))) {
    LOG_WARN("Fail to add task", K(ret));
  } else {
    LOG_DEBUG("success to create first task", K(ret), KPC(this));
  }
  return ret;
}

int ObRestoreFinishDag::init(share::ObIDagNet *dag_net)
{
  int ret = OB_SUCCESS;
  ObRestoreDagNet *restore_dag_net = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("restore finish dag init twice", K(ret));
  } else if (OB_ISNULL(dag_net)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL", K(ret), KP(dag_net));
  } else if (ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net type is unexpected", K(ret), KPC(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet*>(dag_net))) {
  } else if (FALSE_IT(ha_dag_net_ctx_ = restore_dag_net->get_ctx())) {
  } else if (OB_ISNULL(ha_dag_net_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ha_dag_net_ctx_));
  } else {
    is_inited_ = true;
  }
  return ret;
}

/**************************ObRestoreFinishTask***************************/
ObRestoreFinishTask::ObRestoreFinishTask()
  : share::ObITask(share::ObITask::TASK_TYPE_MIGRATE_PREPARE),
    is_inited_(false),
    ctx_(nullptr),
    dag_net_(nullptr)
{
}

ObRestoreFinishTask::~ObRestoreFinishTask()
{
}

int ObRestoreFinishTask::init()
{
  int ret = OB_SUCCESS;
  ObIDagNet *dag_net = nullptr;
  ObRestoreDagNet *restore_dag_net = nullptr;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("restore finish task init twice", K(ret));
  } else if (FALSE_IT(dag_net = this->get_dag()->get_dag_net())) {
  } else if (OB_ISNULL(dag_net) || ObDagNetType::DAG_NET_TYPE_RESTORE != dag_net->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag net should not be NULL or dag net type is unexpected", K(ret), KP(dag_net));
  } else if (FALSE_IT(restore_dag_net = static_cast<ObRestoreDagNet *>(dag_net))) {
  } else if (OB_ISNULL(ctx_ = restore_dag_net->get_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore ctx should not be NULL", K(ret), KP(ctx_));
  } else {
    dag_net_ = dag_net;
    is_inited_ = true;
    LOG_INFO("succeed init restore finish task",
        "dag_id", *ObCurTraceId::get_trace_id(), "dag_net_id", ctx_->task_.task_id_);
  }
  return ret;
}

int ObRestoreFinishTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  LOG_INFO("start do restore finish task", KPC(ctx_));

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore finish task do not init", K(ret));
  } else if (ctx_->is_failed()) {
    bool allow_retry = false;
    if (OB_FAIL(ctx_->check_allow_retry_with_stop(allow_retry))) {
      LOG_ERROR("failed to check allow retry", K(ret), KPC(ctx_));
    } else if (allow_retry) {
      if (OB_FAIL(generate_initial_restore_dag_())) {
        LOG_WARN("failed to generate initial restore dag", K(ret), KPC(ctx_));
      }
    }
  }

  if (OB_TMP_FAIL(record_server_event_())) {
    LOG_WARN("failed to record server event", K(tmp_ret), K(ret));
  }

  return ret;
}

int ObRestoreFinishTask::generate_initial_restore_dag_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObInitialRestoreDag *initial_restore_dag = nullptr;
  ObTenantDagScheduler *scheduler = nullptr;
  ObRestoreFinishDag *restore_finish_dag = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore finish task do not init", K(ret));
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", K(ret));
  } else if (OB_ISNULL(restore_finish_dag = static_cast<ObRestoreFinishDag *>(this->get_dag()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore finish dag should not be NULL", K(ret), KP(restore_finish_dag));
  } else {
    if (OB_FAIL(scheduler->alloc_dag(initial_restore_dag))) {
      LOG_WARN("failed to alloc initial restore dag", K(ret));
    } else if (OB_FAIL(initial_restore_dag->init(dag_net_))) {
      LOG_WARN("failed to init initial restore dag", K(ret));
    } else if (OB_FAIL(this->get_dag()->add_child(*initial_restore_dag))) {
      LOG_WARN("failed to add initial restore dag as child", K(ret), KPC(initial_restore_dag));
    } else if (OB_FAIL(initial_restore_dag->create_first_task())) {
      LOG_WARN("failed to create first task", K(ret));
    } else if (OB_FAIL(scheduler->add_dag(initial_restore_dag))) {
      LOG_WARN("failed to add initial restore dag", K(ret), K(*initial_restore_dag));
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_WARN("Fail to add task", K(ret));
        ret = OB_EAGAIN;
      }
    } else {
      LOG_INFO("start create initial restore dag", K(ret), KPC(ctx_));
      initial_restore_dag = nullptr;
    }

    if (OB_NOT_NULL(initial_restore_dag) && OB_NOT_NULL(scheduler)) {
      scheduler->free_dag(*initial_restore_dag);
      initial_restore_dag = nullptr;
    }
  }

  return ret;
}

int ObRestoreFinishTask::record_server_event_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx should not be null", K(ret));
  } else {
    SERVER_EVENT_ADD("storage_ha", "restore_finish_task",
      "src", to_cstring(ctx_->task_.src_info_),
      "task_id", to_cstring(ctx_->task_.task_id_),
      "restore_type", ObRestoreTaskType::get_str(ctx_->task_.type_));
  }
  return ret;
}

/*********************ObRestoreDagNetUtils*********************/
int ObRestoreDagNetUtils::build_tablets_sstable_info_with_helper(
  const common::ObIArray<ObTabletID> &tablet_id_array,
  ObIRestoreHelper *helper,
  ObStorageHATableInfoMgr *ha_table_info_mgr,
  share::ObIDagNet *dag_net,
  ObLS *ls)
{
  int ret = OB_SUCCESS;
  obrpc::ObCopyTabletSSTableInfo sstable_info;
  obrpc::ObCopyTabletSSTableHeader copy_header;
  common::ObArray<ObTabletHandle> tablet_handle_array;

  if (OB_ISNULL(helper) || OB_ISNULL(ha_table_info_mgr) || OB_ISNULL(dag_net) || OB_ISNULL(ls) || tablet_id_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(helper), KP(ha_table_info_mgr), KP(dag_net), KP(ls), K(tablet_id_array));
  } else if (OB_FAIL(hold_local_tablet_(tablet_id_array, ls, tablet_handle_array))) {
    LOG_WARN("failed to hold local tablet", K(ret));
  } else if (tablet_handle_array.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("all tablets has been gc", K(ret));
  } else if (OB_FAIL(helper->init_for_build_tablets_sstable_info(tablet_handle_array))) {
    LOG_WARN("failed to init for build tablets sstable info", K(ret));
  } else {
    while (OB_SUCC(ret)) {
      sstable_info.reset();
      copy_header.reset();
      if (dag_net->is_cancel()) {
        ret = OB_CANCELED;
        LOG_WARN("task is cancelled", K(ret));
      } else if (OB_FAIL(helper->fetch_next_tablet_sstable_header(copy_header))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to fetch next tablet sstable header", K(ret));
        }
      } else if (ObCopyTabletStatus::TABLET_NOT_EXIST == copy_header.status_
                    && copy_header.tablet_id_.is_ls_inner_tablet()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls inner tablet should be exist", K(ret), K(copy_header));
      } else if (OB_FAIL(ha_table_info_mgr->init_tablet_info(copy_header))) {
        LOG_WARN("failed to init tablet info", K(ret), K(copy_header));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < copy_header.sstable_count_; ++i) {
          if (OB_FAIL(helper->fetch_next_sstable_meta(sstable_info))) {
            LOG_WARN("failed to fetch next sstable meta", K(ret), K(copy_header));
          } else if (!sstable_info.is_valid()) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("build tablets sstable info get invalid argument", K(ret), K(sstable_info));
          } else if (sstable_info.table_key_.is_memtable()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("table should not be MEMTABLE", K(ret), K(sstable_info));
          } else if (OB_FAIL(ha_table_info_mgr->add_table_info(sstable_info.tablet_id_, sstable_info))) {
            LOG_WARN("failed to add table info", K(ret), K(sstable_info));
          } else {
            LOG_DEBUG("add table info", K(sstable_info.tablet_id_), K(sstable_info));
          }
        }
      }
    }
  }
  return ret;
}

int ObRestoreDagNetUtils::create_or_update_tablets_with_helper(
    const common::ObIArray<ObTabletID> &tablet_id_array,
    ObIRestoreHelper *helper,
    share::ObIDagNet *dag_net,
    ObLS *ls)
{
  int ret = OB_SUCCESS;
  obrpc::ObCopyTabletInfo tablet_info;
  ObIRestoreHelper *new_helper = nullptr;
  common::ObArenaAllocator allocator("RestoreTablets");

  if (OB_ISNULL(helper) || OB_ISNULL(dag_net) || OB_ISNULL(ls) || tablet_id_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(helper), KP(dag_net), KP(ls), K(tablet_id_array));
  } else if (OB_FAIL(helper->copy_for_task(allocator, new_helper))) {
    LOG_WARN("failed to copy restore helper", K(ret));
  } else if (OB_FAIL(new_helper->init_for_fetch_tablet_meta(tablet_id_array))) {
    LOG_WARN("failed to init for fetch tablet meta", K(ret), K(tablet_id_array));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_id_array.count(); ++i) {
      const ObTabletID &tablet_id = tablet_id_array.at(i);
      tablet_info.reset();

      if (dag_net->is_cancel()) {
        ret = OB_CANCELED;
        LOG_WARN("task is cancelled", K(ret));
      } else if (OB_FAIL(new_helper->fetch_tablet_meta(tablet_info))) {
        LOG_WARN("failed to fetch tablet meta", K(ret), K(tablet_id));
      } else if (OB_FAIL(modified_tablet_info_(tablet_info))) {
        LOG_WARN("failed to modified tablet info", K(ret), K(tablet_info));
      } else if (OB_FAIL(create_or_update_tablet(tablet_info, false/*need_check_tablet_limit*/, ls))) {
        LOG_WARN("failed to create or update tablet", K(ret), K(tablet_info));
      }
    }
  }
  return ret;
}

int ObRestoreDagNetUtils::create_or_update_tablet(
    const obrpc::ObCopyTabletInfo &tablet_info,
    const bool need_check_tablet_limit,
    ObLS *ls)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator("RestoreTablet");
  ObTabletHandle local_tablet_hdl;
  ObTablesHandleArray major_tables;
  ObStorageSchema storage_schema;
  ObBuildMajorSSTablesParam major_sstables_param(storage_schema, tablet_info.param_.has_truncate_info_);

  if (!tablet_info.is_valid() || OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("create or update tablet get invalid argument", K(ret), K(tablet_info), KP(ls));
  } else if (ObCopyTabletStatus::TABLET_NOT_EXIST == tablet_info.status_ && tablet_info.tablet_id_.is_ls_inner_tablet()) {
    ret = OB_TABLET_NOT_EXIST;
    LOG_WARN("src ls inner tablet is not exist, src ls is maybe deleted", K(ret), K(tablet_info));
  } else if (need_check_tablet_limit
                && OB_FAIL(ObTabletCreateMdsHelper::check_create_new_tablets(1LL, ObTabletCreateThrottlingLevel::SOFT))) {
    if (OB_TOO_MANY_PARTITIONS_ERROR == ret) {
      LOG_ERROR("too many partitions, failed to check create new tablet", K(ret), K(tablet_info));
    } else {
      LOG_WARN("failed to check create new tablet", K(ret), K(tablet_info));
    }
  } else if (OB_FAIL(hold_local_reuse_major_sstables_(tablet_info.tablet_id_, ls, local_tablet_hdl, major_tables,
                                                          storage_schema, allocator))) {
    LOG_WARN("failed to hold local reuse major sstable", K(ret), K(tablet_info));
  } else if (OB_FAIL(ls->rebuild_create_tablet(tablet_info.param_, false/*keep old*/))) {
    LOG_WARN("failed to create or update tablet", K(ret), K(tablet_info));
  } else if (tablet_info.param_.is_empty_shell() || tablet_info.param_.ha_status_.is_restore_status_undefined()) {
    // empty shell or UNDEFINED tablet does not need to reuse any sstable.
  } else {
    if (major_tables.empty()) {
      // do nothing
    } else if (OB_FAIL(ObStorageHATabletBuilderUtil::build_tablet_with_major_tables(ls, tablet_info.tablet_id_,
                                                                                    major_tables, major_sstables_param,
                                                                                    false/*is_only_replace_major*/))) {
      LOG_WARN("failed to build tablet with major tables", K(ret), K(tablet_info), K(major_tables));
    } else {
      LOG_INFO("succeed build ha table new table store", K(tablet_info), K(major_tables));
    }
  }
  return ret;
}

int ObRestoreDagNetUtils::hold_local_tablet_(
  const common::ObIArray<ObTabletID> &tablet_id_array,
  ObLS *ls,
  common::ObIArray<ObTabletHandle> &tablet_handle_array)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ls should not be NULL", K(ret), KP(ls));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_id_array.count(); ++i) {
      const ObTabletID &tablet_id = tablet_id_array.at(i);
      ObTabletHandle tablet_handle;
      if (OB_FAIL(ls->ha_get_tablet(tablet_id, tablet_handle))) {
        if (OB_TABLET_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          LOG_INFO("tablet not exist, skip it", K(tablet_id));
        } else {
          LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
        }
      } else if (OB_FAIL(tablet_handle_array.push_back(tablet_handle))) {
        LOG_WARN("failed to push tablet handle into array", K(ret), K(tablet_handle));
      }
    }
  }
  return ret;
}

int ObRestoreDagNetUtils::modified_tablet_info_(obrpc::ObCopyTabletInfo &tablet_info)
{
  int ret = OB_SUCCESS;
  if (!tablet_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("modified tablet info get invalid argument", K(ret), K(tablet_info));
  } else if (tablet_info.param_.is_empty_shell()) {
    // do nothing
  } else if (tablet_info.param_.ha_status_.is_restore_status_full()
      && !tablet_info.param_.ha_status_.is_data_status_complete()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet info ha status is unexpected", K(ret), K(tablet_info));
  } else if (OB_FAIL(tablet_info.param_.ha_status_.set_data_status(ObTabletDataStatus::INCOMPLETE))) {
    LOG_WARN("failed to set data status", K(ret), K(tablet_info));
  }
  return ret;
}

int ObRestoreDagNetUtils::hold_local_complete_tablet_major_sstables_(ObTablet *tablet, ObTablesHandleArray &tables_handle)
{
  int ret = OB_SUCCESS;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;

  if (OB_ISNULL(tablet)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet should not be NULL", K(ret));
  } else if (tablet->get_tablet_meta().tablet_id_.is_ls_inner_tablet()) {
    LOG_INFO("ls inner tablet do not reuse any sstable", KPC(tablet));
  } else if (!tablet->get_tablet_meta().ha_status_.is_restore_status_full()) {
    LOG_INFO("tablet is in restore, do not reuse any sstable", KPC(tablet));
  } else if (OB_FAIL(tablet->fetch_table_store(table_store_wrapper))) {
    LOG_WARN("fail to fetch table store", K(ret));
  } else {
    const ObSSTableArray &major_sstable = table_store_wrapper.get_member()->get_major_sstables();
    for (int64_t i = 0; OB_SUCC(ret) && i < major_sstable.count(); ++i) {
      ObITable *table = major_sstable.at(i);
      bool is_exist = false;

      if (OB_ISNULL(table) || !table->is_major_sstable()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table should not be NULL or table type is unexpected", K(ret), KP(table), KPC(tablet));
      } else {
        for (int64_t j = 0; OB_SUCC(ret) && j < tables_handle.get_count(); ++j) {
          ObITable *tmp_table = tables_handle.get_table(j);
          if (OB_ISNULL(tmp_table)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("table should not be NULL", K(ret), KP(tmp_table), K(j), KPC(tablet));
          } else if (tmp_table->get_key() == table->get_key()) {
            is_exist = true;
            break;
          }
        }

        if (OB_SUCC(ret)) {
          if (!is_exist && OB_FAIL(tables_handle.add_sstable(table, table_store_wrapper.get_meta_handle()))) {
            LOG_WARN("failed to add table into tables handle", K(ret), KPC(tablet));
          }
        }
      }
    }
    LOG_INFO("succeed to get reuse major sstable handle", K(ret), K(tables_handle), KPC(tablet));
  }
  return ret;
}

int ObRestoreDagNetUtils::remove_uncomplete_tablet_(const common::ObTabletID &tablet_id, ObLS *ls)
{
  int ret = OB_SUCCESS;
  const bool is_rollback = true;
  if (OB_ISNULL(ls) || !tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("remove uncomplete tablet get invalid argument", K(ret), KP(ls), K(tablet_id));
  } else if (OB_FAIL(ls->trim_rebuild_tablet(tablet_id, is_rollback))) {
    LOG_WARN("failed to trim tablet tablet with rollback", K(ret), K(tablet_id));
  } else {
    LOG_INFO("succeed to remove uncomplete tablet", K(ret), K(tablet_id));
  }
  return ret;
}

int ObRestoreDagNetUtils::hold_local_reuse_major_sstables_(
    const common::ObTabletID &tablet_id,
    ObLS *ls,
    ObTabletHandle &local_tablet_hdl,
    ObTablesHandleArray &tables_handle,
    ObStorageSchema &storage_schema,
    common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  tables_handle.reset();
  ObTablet *tablet = nullptr;
  ObArenaAllocator arena_allocator;
  ObStorageSchema *tablet_storage_schema = nullptr;

  if (OB_ISNULL(ls) || !tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("hold local reuse sstable get invalid argument", K(ret), KP(ls), K(tablet_id));
  } else if (OB_FAIL(ls->ha_get_tablet(tablet_id, local_tablet_hdl))) {
    if (OB_TABLET_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
    }
  } else if (OB_ISNULL(tablet = local_tablet_hdl.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), K(tablet_id), KP(tablet));
  } else if (OB_FAIL(tablet->load_storage_schema(arena_allocator, tablet_storage_schema))) {
    LOG_WARN("fail to load storage schema", K(ret));
  } else {
    while (OB_SUCC(ret)) {
      if (tablet->get_tablet_meta().has_next_tablet_) {
        if (OB_FAIL(remove_uncomplete_tablet_(tablet_id, ls))) {
          LOG_WARN("failed to remove uncomplete tablet", K(ret), K(tablet_id));
        }
      } else if (OB_FAIL(hold_local_complete_tablet_major_sstables_(tablet, tables_handle))) {
        LOG_WARN("failed to hold local complete tablet sstable", K(ret), KP(tablet));
      } else {
        if (!storage_schema.is_inited()) {
          if (OB_FAIL(storage_schema.init(allocator, *tablet_storage_schema))) {
            LOG_WARN("failed to init storage schema", K(ret), KPC(tablet));
          }
        } else if (storage_schema.compare_schema_newer(*tablet_storage_schema)) {
          if (OB_FAIL(ObStorageSchemaUtil::update_storage_schema(allocator, *tablet_storage_schema, storage_schema/*dst*/))) {
            LOG_WARN("failed to update storage schema", K(ret), KPC(tablet));
          }
        }
      }

      if (OB_FAIL(ret)) {
      } else if (!tablet->get_tablet_meta().has_next_tablet_) {
        break;
      } else {
        tablet = tablet->get_next_tablet_guard().get_obj();
        if (OB_ISNULL(tablet)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("tablet should not be NULL", K(ret), KP(tablet), K(tablet_id));
        }
      }
    }
  }

  ObTabletObjLoadHelper::free(arena_allocator, tablet_storage_schema);
  return ret;
}

} // namespace restore
} // namespace oceanbase
