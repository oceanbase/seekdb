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
#include "ob_standby_restore_dag.h"
#include "standby/ob_standby_observer_adapter.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
using namespace share;
namespace storage
{

namespace
{
bool is_retryable_restore_error(const int err)
{
  switch (err) {
    case OB_NOT_INIT:
    case OB_INVALID_ARGUMENT:
    case OB_ERR_UNEXPECTED:
    case OB_ERR_SYS:
    case OB_INIT_TWICE:
    case OB_CANCELED:
    case OB_NOT_SUPPORTED:
    case OB_SERVER_OUTOF_DISK_SPACE:
    case OB_INVALID_DATA:
    case OB_CHECKSUM_ERROR:
    case OB_INVALID_TABLE_STORE:
      return false;
    default:
      return true;
  }
}
} // namespace

/******************ObStandbyRestoreResultMgr*********************/
ObStandbyRestoreResultMgr::ObStandbyRestoreResultMgr()
  : lock_(),
    result_(OB_SUCCESS),
    retry_count_(0),
    allow_retry_(true),
    failed_task_id_list_()
{
}

ObStandbyRestoreResultMgr::~ObStandbyRestoreResultMgr()
{
}

int ObStandbyRestoreResultMgr::set_result(
    const int32_t result,
    const bool allow_retry,
    const enum ObDagType::ObDagTypeEnum type)
{
  int ret = OB_SUCCESS;
  common::SpinWLockGuard guard(lock_);
  const uint64_t tenant_id = OB_SERVER_RUNTIME_ID;
  if (OB_SUCCESS == result_ && OB_SUCCESS != result) {
    result_ = result;
    allow_retry_ = allow_retry;
    if (NULL != ObCurTraceId::get_trace_id() && OB_FAIL(failed_task_id_list_.push_back(*ObCurTraceId::get_trace_id()))) {
      LOG_WARN("failed to push trace id into array", K(ret));
    } else {
      ObCStringHelper helper;
      standby::ObStandbyObserverAdapter::report_set_first_result(
          tenant_id,
          result,
          allow_retry,
          retry_count_,
          helper.convert(failed_task_id_list_),
          OB_DAG_TYPES[type].dag_type_str_);
      FLOG_INFO("set first result", K(result), K(allow_retry), K(retry_count_), K(failed_task_id_list_));
    }
  }
  return ret;
}

int ObStandbyRestoreResultMgr::get_result(int32_t &result)
{
  int ret = OB_SUCCESS;
  common::SpinRLockGuard guard(lock_);
  result = result_;
  return ret;
}

bool ObStandbyRestoreResultMgr::is_failed() const
{
  common::SpinRLockGuard guard(lock_);
  return OB_SUCCESS != result_;
}

int ObStandbyRestoreResultMgr::check_allow_retry(bool &allow_retry)
{
  int ret = OB_SUCCESS;
  allow_retry = false;
  common::SpinRLockGuard guard(lock_);
  if (!allow_retry_) {
    allow_retry = false;
  } else {
    allow_retry = is_retryable_restore_error(result_);
    if (allow_retry && retry_count_ < MAX_RETRY_CNT) {
      //do nohitng
    } else {
      allow_retry = false;
    }
  }
  return ret;
}

int ObStandbyRestoreResultMgr::get_first_failed_task_id(share::ObTaskId &task_id)
{
  int ret = OB_SUCCESS;
  common::SpinRLockGuard guard(lock_);
  if (OB_SUCCESS != result_) {
    ARRAY_FOREACH(failed_task_id_list_, i) {
      task_id.set(failed_task_id_list_.at(i));
      break;
    }

    if (task_id.is_invalid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get first failed task id", K(ret));
    }
  }
  return ret;
}

void ObStandbyRestoreResultMgr::reuse()
{
  common::SpinWLockGuard guard(lock_);
  retry_count_++;
  result_ = OB_SUCCESS;
  allow_retry_ = true;
}

void ObStandbyRestoreResultMgr::reset()
{
  common::SpinWLockGuard guard(lock_);
  result_ = OB_SUCCESS;
  retry_count_ = 0;
  allow_retry_ = true;
}

int ObStandbyRestoreResultMgr::get_retry_count(int32_t &retry_count)
{
  int ret = OB_SUCCESS;
  common::SpinRLockGuard guard(lock_);
  retry_count = retry_count_;
  return ret;
}

/******************ObIStandbyRestoreDagNetCtx*********************/
ObIStandbyRestoreDagNetCtx::ObIStandbyRestoreDagNetCtx()
  : result_mgr_()
{
}

ObIStandbyRestoreDagNetCtx::~ObIStandbyRestoreDagNetCtx()
{
}

int ObIStandbyRestoreDagNetCtx::set_result(
    const int32_t result,
    const bool need_retry,
    const enum share::ObDagType::ObDagTypeEnum type)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("standby restore dag net ctx is not init", K(ret), K(*this));
  } else if (OB_FAIL(result_mgr_.set_result(result, need_retry, type))) {
    LOG_WARN("failed to set result", K(ret), K(result), K(*this));
  }
  return ret;
}

bool ObIStandbyRestoreDagNetCtx::is_failed() const
{
  return result_mgr_.is_failed();
}

int ObIStandbyRestoreDagNetCtx::check_allow_retry_with_stop(bool &allow_retry)
{
  int ret = OB_SUCCESS;
  allow_retry = false;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("standby restore dag net ctx is invalid", K(ret), K(*this));
  } else if (OB_FAIL(check_allow_retry(allow_retry))) {
    LOG_WARN("failed to check need retry", K(ret), K(*this));
  } else if (!allow_retry) {
    int result = OB_SUCCESS;
    if (OB_FAIL(result_mgr_.get_result(result))) {
      LOG_WARN("failed to get result", K(ret), K(*this));
    } else {
      LOG_ERROR("failed to do retry, abort", K(result));
    }
    standby::ObStandbyObserverAdapter::stop_server();
  }
  return ret;
}

int ObIStandbyRestoreDagNetCtx::check_allow_retry(bool &allow_retry)
{
  int ret = OB_SUCCESS;
  allow_retry = false;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("standby restore dag net ctx is invalid", K(ret), K(*this));
  } else if (OB_FAIL(result_mgr_.check_allow_retry(allow_retry))) {
    LOG_WARN("failed to check need retry", K(ret), K(*this));
  }
  return ret;
}

int ObIStandbyRestoreDagNetCtx::get_result(int32_t &result)
{
  int ret = OB_SUCCESS;
  result = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("standby restore dag net ctx is invalid", K(ret), K(*this));
  } else if (OB_FAIL(result_mgr_.get_result(result))) {
    LOG_WARN("failed to get result", K(ret), K(*this));
  }
  return ret;
}

int ObIStandbyRestoreDagNetCtx::get_first_failed_task_id(share::ObTaskId &task_id)
{
  int ret = OB_SUCCESS;
  task_id.reset();
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("standby restore dag net ctx is invalid", K(ret), K(*this));
  } else if (OB_FAIL(result_mgr_.get_first_failed_task_id(task_id))) {
    LOG_WARN("failed to get result", K(ret), K(*this));
  }
  return ret;
}

void ObIStandbyRestoreDagNetCtx::reuse()
{
  result_mgr_.reuse();
}

void ObIStandbyRestoreDagNetCtx::reset()
{
  result_mgr_.reset();
}



/******************ObStandbyRestoreDag*********************/
ObStandbyRestoreDag::ObStandbyRestoreDag(
    const share::ObDagType::ObDagTypeEnum &dag_type)
  : ObIDag(dag_type),
    standby_restore_dag_net_ctx_(nullptr),
    result_mgr_()
{
}

ObStandbyRestoreDag::~ObStandbyRestoreDag()
{
}

int ObStandbyRestoreDag::inner_reset_status_for_retry()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (OB_ISNULL(standby_restore_dag_net_ctx_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore dag is not initialized", K(ret), KP(standby_restore_dag_net_ctx_));
  } else if (standby_restore_dag_net_ctx_->is_failed()) {
    if (OB_SUCCESS != (tmp_ret = standby_restore_dag_net_ctx_->get_result(ret))) {
      LOG_WARN("failed to get standby restore dag net ctx result", K(tmp_ret), KPC(standby_restore_dag_net_ctx_));
      ret = tmp_ret;
    } else {
      LOG_INFO("set inner set status for retry failed", K(ret), KPC(standby_restore_dag_net_ctx_));
    }
  } else {
    LOG_INFO("start retry", KPC(this));
    result_mgr_.reuse();
    if (OB_FAIL(create_first_task())) {
      LOG_WARN("failed to create first task", K(ret), KPC(this));
    }
  }
  return ret;
}

bool ObStandbyRestoreDag::inner_check_can_retry()
{
  bool bool_ret = false;
  int ret = OB_SUCCESS;
  if (OB_SUCCESS != (ret = result_mgr_.check_allow_retry(bool_ret))) {
    bool_ret = false;
    LOG_ERROR("failed to check need retry", K(ret), K(*this));
  }
  return bool_ret;
}

int ObStandbyRestoreDag::set_result(
    const int32_t result,
    const bool allow_retry,
    const enum share::ObDagType::ObDagTypeEnum type)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(standby_restore_dag_net_ctx_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore dag is not initialized", K(ret), KP(standby_restore_dag_net_ctx_));
  } else if (OB_SUCCESS == result) {
    //do nothing
  } else if (OB_FAIL(result_mgr_.set_result(result, allow_retry, type))) {
    LOG_WARN("failed to set result", K(ret), K(result), KPC(standby_restore_dag_net_ctx_));
  }
  return ret;
}

int ObStandbyRestoreDag::report_result()
{
  int ret = OB_SUCCESS;
  int32_t dag_ret = OB_SUCCESS;
  int32_t tmp_result = OB_SUCCESS;
  int32_t result = OB_SUCCESS;
  if (OB_ISNULL(standby_restore_dag_net_ctx_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore dag is not initialized", K(ret), KP(standby_restore_dag_net_ctx_));
  } else if (OB_FAIL(result_mgr_.get_result(tmp_result))) {
    LOG_WARN("failed to get result", K(ret), KPC(standby_restore_dag_net_ctx_));
  } else if (OB_SUCCESS != tmp_result) {
    result = tmp_result;
  } else if (FALSE_IT(dag_ret = this->get_dag_ret())) {
  } else if (OB_SUCCESS != dag_ret) {
    result = dag_ret;
  }

  if (OB_FAIL(ret)) {
  } else if (OB_SUCCESS == result) {
    //do nothing
  } else if (OB_FAIL(standby_restore_dag_net_ctx_->set_result(result, true /*allow_retry*/, get_type()))) {
    LOG_WARN("failed to set standby restore dag net ctx result", K(ret), KPC(standby_restore_dag_net_ctx_));
  }
  return ret;
}


/******************ObStandbyRestoreDagUtils*********************/
int ObStandbyRestoreDagUtils::deal_with_fo(
    const int err,
    share::ObIDag *dag,
    const bool allow_retry)
{
  int ret = OB_SUCCESS;
  ObStandbyRestoreDag *standby_restore_dag = nullptr;

  if (OB_SUCCESS == err || OB_ISNULL(dag)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("deal with fo get invalid argument", K(ret), K(err), KP(dag));
  } else if (0 != STRCMP(OB_DAG_TYPES[dag->get_type()].dag_module_str_, "MIGRATE")
      && 0 != STRCMP(OB_DAG_TYPES[dag->get_type()].dag_module_str_, "RESTORE")
      && 0 != STRCMP(OB_DAG_TYPES[dag->get_type()].dag_module_str_, "BACKFILL_TX")
      && 0 != STRCMP(OB_DAG_TYPES[dag->get_type()].dag_module_str_, "TRANSFER")
      && 0 != STRCMP(OB_DAG_TYPES[dag->get_type()].dag_module_str_, "REBUILD_TABLET")) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag type is unexpected", K(ret), KPC(dag));
  } else if (OB_ISNULL(standby_restore_dag = static_cast<ObStandbyRestoreDag *>(dag))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("standby restore dag should not be NULL", K(ret), KPC(standby_restore_dag));
  } else if (OB_FAIL(standby_restore_dag->set_result(err, allow_retry, dag->get_type()))) {
    LOG_WARN("failed to set result", K(ret), K(err));
  }
  return ret;
}

//TODO(xingzhi): remove input parameter ls_id
int ObStandbyRestoreDagUtils::get_ls(const share::ObLSID &ls_id, ObLS *&ls)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = nullptr;
  ls = nullptr;

  if (!ls_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get ls get invalid argument", K(ret), K(ls_id));
  } else if (OB_ISNULL(ls_service = share::server_service<ObLSService>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObLSService from MTL", K(ret), KP(ls_service));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("fail to get log stream", KR(ret), K(ls_id));
  } else if (OB_UNLIKELY(nullptr == ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream should not be NULL", KR(ret), K(ls_id), KP(ls));
  }
  return ret;
}


/******************ObStandbyRestoreTabletGroupCtx*********************/
ObStandbyRestoreTabletGroupCtx::ObStandbyRestoreTabletGroupCtx(const TabletGroupCtxType type)
  : is_inited_(false),
    lock_(),
    tablet_id_array_(),
    index_(0),
    type_(type)
{
}

ObStandbyRestoreTabletGroupCtx::~ObStandbyRestoreTabletGroupCtx()
{
}

int ObStandbyRestoreTabletGroupCtx::init(const common::ObIArray<common::ObTabletID> &tablet_id_array)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ha tablet group ctx already init", K(ret));
  } else if (tablet_id_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init ha tablet group ctx get invalid argument", K(ret), K(tablet_id_array));
  } else if (OB_FAIL(tablet_id_array_.assign(tablet_id_array))) {
    LOG_WARN("failed to assign tablet id array", K(ret), K(tablet_id_array));
  } else if (OB_FAIL(inner_init())) {
    LOG_WARN("failed to inner init", K(ret));
  } else {
    index_ = 0;
    is_inited_ = true;
  }
  return ret;
}

int ObStandbyRestoreTabletGroupCtx::get_next_tablet_id(common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  tablet_id.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ha tablet group ctx do not init", K(ret));
  } else {
    common::SpinWLockGuard guard(lock_);
    if (index_ > tablet_id_array_.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet group index is bigger than tablet id array count", K(ret), K(index_), K(tablet_id_array_));
    } else if (index_ == tablet_id_array_.count()) {
      ret = OB_ITER_END;
    } else {
      tablet_id = tablet_id_array_.at(index_);
      index_++;
    }
  }
  return ret;
}

int ObStandbyRestoreTabletGroupCtx::get_all_tablet_ids(ObIArray<common::ObTabletID> &tablet_id_array)
{
  int ret = OB_SUCCESS;
  tablet_id_array.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ha tablet group ctx do not init", K(ret));
  } else {
    common::SpinRLockGuard guard(lock_);
    if (OB_FAIL(tablet_id_array.assign(tablet_id_array_))) {
      LOG_WARN("failed to get tablet id array", K(ret), K(tablet_id_array_));
    }
  }
  return ret;
}


void ObStandbyRestoreTabletGroupCtx::reuse()
{
  common::SpinWLockGuard guard(lock_);
  inner_reuse();
}

void ObStandbyRestoreTabletGroupCtx::inner_reuse()
{
  tablet_id_array_.reuse();
  index_ = 0;
  is_inited_ = false;
}

/******************ObStandbyRestoreTabletGroupCtx*********************/
ObStandbyRestoreTabletGroupMgr::ObStandbyRestoreTabletGroupMgr()
  : is_inited_(false),
    lock_(),
    allocator_("StandbyTGMgr", OB_MALLOC_NORMAL_BLOCK_SIZE),
    tablet_group_ctx_array_(),
    index_(0)
{
}

ObStandbyRestoreTabletGroupMgr::~ObStandbyRestoreTabletGroupMgr()
{
  if (!is_inited_) {
  } else {
    reuse();
    is_inited_ = false;
  }
}

int ObStandbyRestoreTabletGroupMgr::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ha tablet group mgr init twice", K(ret));
  } else {
    index_ = 0;
    is_inited_ = true;
  }
  return ret;
}

int ObStandbyRestoreTabletGroupMgr::get_next_tablet_group_ctx(
    ObStandbyRestoreTabletGroupCtx *&tablet_group_ctx)
{
  int ret = OB_SUCCESS;
  tablet_group_ctx = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ha tablet group mgr do not init", K(ret));
  } else {
    common::SpinWLockGuard guard(lock_);
    if (index_ == tablet_group_ctx_array_.count()) {
      ret = OB_ITER_END;
    } else {
      tablet_group_ctx = tablet_group_ctx_array_.at(index_);
      index_++;
    }
  }
  return ret;
}

int ObStandbyRestoreTabletGroupMgr::build_tablet_group_ctx(
    const ObIArray<common::ObTabletID> &tablet_id_array,
    const ObStandbyRestoreTabletGroupCtx::TabletGroupCtxType type /*=NORMAL_TYPE*/)
{
  int ret = OB_SUCCESS;
  ObStandbyRestoreTabletGroupCtx *tablet_group_ctx = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ha tablet group mgr do not init", K(ret));
  } else if (tablet_id_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build tablet group ctx get invalid argument", K(ret), K(tablet_id_array));
  } else {
    common::SpinWLockGuard guard(lock_);
    if (OB_FAIL(alloc_and_new_tablet_group_ctx(type, tablet_group_ctx))) {
      LOG_WARN("failed to alloc and new tablet group ctx", K(ret));
    } else if (OB_ISNULL(tablet_group_ctx)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet group ctx should not be NULL", K(ret), KP(tablet_group_ctx));
    } else if (OB_FAIL(tablet_group_ctx->init(tablet_id_array))) {
      LOG_WARN("failed to init tablet group ctx", K(ret), K(tablet_id_array));
    } else if (OB_FAIL(tablet_group_ctx_array_.push_back(tablet_group_ctx))) {
      LOG_WARN("failed to push tablet group ctx into array", K(ret));
    } else {
      tablet_group_ctx = nullptr;
    }

    if (OB_NOT_NULL(tablet_group_ctx)) {
      tablet_group_ctx->~ObStandbyRestoreTabletGroupCtx();
    }
  }
  return ret;
}

int ObStandbyRestoreTabletGroupMgr::alloc_and_new_tablet_group_ctx(
    const ObStandbyRestoreTabletGroupCtx::TabletGroupCtxType type,
    ObStandbyRestoreTabletGroupCtx *&tablet_group_ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  if (ObStandbyRestoreTabletGroupCtx::TabletGroupCtxType::NORMAL_TYPE == type) {
    if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObStandbyRestoreTabletGroupCtx)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), KP(buf));
    } else {
      tablet_group_ctx = new (buf) ObStandbyRestoreTabletGroupCtx();
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ctx type", K(ret), K(type));
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(tablet_group_ctx)) {
    tablet_group_ctx->~ObStandbyRestoreTabletGroupCtx();
    tablet_group_ctx = nullptr;
  }
  return ret;
}

void ObStandbyRestoreTabletGroupMgr::reuse()
{
  common::SpinWLockGuard guard(lock_);
  for (int64_t i = 0; i < tablet_group_ctx_array_.count(); ++i) {
    ObStandbyRestoreTabletGroupCtx *tablet_group_ctx = tablet_group_ctx_array_.at(i);
    if (OB_NOT_NULL(tablet_group_ctx)) {
      tablet_group_ctx->~ObStandbyRestoreTabletGroupCtx();
    }
  }
  tablet_group_ctx_array_.reset();
  allocator_.reset();
  index_ = 0;
}



/******************ObStandbyRestoreTaskUtils*********************/
int ObStandbyRestoreTaskUtils::check_need_copy_sstable(
    const blocksstable::ObMigrationSSTableParam &param,
    const bool &is_restore,
    ObTabletHandle &tablet_handle,
    bool &need_copy)
{
  int ret = OB_SUCCESS;
  need_copy = true;
  if (!param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check need copy sstable get invalid argument", K(ret), K(param));
  } else if (param.table_key_.is_major_sstable()) {
    if (OB_FAIL(check_major_sstable_need_copy_(param, is_restore, tablet_handle, need_copy))) {
      LOG_WARN("failed to check major sstable need copy", K(ret), K(param), K(tablet_handle));
    }
  } else if (param.table_key_.is_minor_sstable()) {
    if (OB_FAIL(check_minor_sstable_need_copy_(param, tablet_handle, need_copy))) {
      LOG_WARN("failed to check minor sstable need copy", K(ret), K(param), K(tablet_handle));
    }
  } else if (param.table_key_.is_ddl_dump_sstable()) {
    if (OB_FAIL(check_ddl_sstable_need_copy_(param, tablet_handle, need_copy))) {
      LOG_WARN("failed to check ddl sstable need copy", K(ret), K(param), K(tablet_handle));
    }
  } else if (param.table_key_.is_mds_sstable()) {
    need_copy = true;
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("copy sstable table type is unexpected", K(ret), K(param));
  }
  return ret;
}

int ObStandbyRestoreTaskUtils::check_major_sstable_need_copy_(
    const blocksstable::ObMigrationSSTableParam &param,
    const bool &is_restore,
    ObTabletHandle &tablet_handle,
    bool &need_copy)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  ObSSTableWrapper sstable_wrapper;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  ObSSTableMetaHandle sst_meta_hdl;

  if (!param.table_key_.is_major_sstable()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check major sstable need copy get invalid argument", K(ret), K(param));
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), K(param), K(tablet_handle));
  } else if (OB_FAIL(tablet->fetch_table_store(table_store_wrapper))) {
    LOG_WARN("fail to fetch table store", K(ret));
  } else {
    const ObSSTableArray &major_sstable_array = table_store_wrapper.get_member()->get_major_sstables();
    if (major_sstable_array.empty()) {
      need_copy = true;
    } else if (OB_FAIL(major_sstable_array.get_table(param.table_key_, sstable_wrapper))) {
      LOG_WARN("failed to get table", K(ret), K(param), K(major_sstable_array));
    } else if (nullptr == sstable_wrapper.get_sstable()) {
      need_copy = true;
    } else if (OB_FAIL(sstable_wrapper.get_sstable()->get_meta(sst_meta_hdl))) {
      LOG_WARN("failed to get sstable meta handle", K(ret));
    } else if (OB_FAIL(param.check_sstable_meta(sst_meta_hdl.get_sstable_meta()))) {
      LOG_WARN("failed to check sstable meta", K(ret), K(param), K(sstable_wrapper));
    } else {
      need_copy = false;
    }
  }
  return ret;
}

int ObStandbyRestoreTaskUtils::check_minor_sstable_need_copy_(
    const blocksstable::ObMigrationSSTableParam &param,
    ObTabletHandle &tablet_handle,
    bool &need_copy)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  const ObSSTable *sstable = nullptr;
  ObTableStoreIterator minor_table_iter;

  if (!param.table_key_.is_minor_sstable()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check minor sstable need copy get invalid argument", K(ret), K(param));
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), K(param), K(tablet_handle));
  } else if (OB_FAIL(tablet->get_all_minor_sstables(minor_table_iter))) {
    LOG_WARN("failed to get tables handle array", K(ret), K(param));
  } else if (0 == minor_table_iter.count()) {
    need_copy = true;
  } else {
    bool found = false;

    while (OB_SUCC(ret)) {
      ObITable *table = nullptr;
      if (OB_FAIL(minor_table_iter.get_next(table))) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("fail to iterate minor tables", K(ret));
        } else {
          ret = OB_SUCCESS;
          break;
        }
      } else if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("minor sstable should not be NULL", K(ret), KP(table), K(minor_table_iter));
      } else if (table->get_key() == param.table_key_) {
        found = true;
        need_copy = true;
        //TODO(muwei.ym) Fix it in 4.3
        //Need copy should be false and reuse local minor sstable.
      }
    }
    if (OB_SUCC(ret) && !found) {
      need_copy = true;
    }
  }
  return ret;
}

int ObStandbyRestoreTaskUtils::check_ddl_sstable_need_copy_(
    const blocksstable::ObMigrationSSTableParam &param,
    ObTabletHandle &tablet_handle,
    bool &need_copy)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  ObSSTableWrapper sstable_wrapper;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  ObSSTableMetaHandle sst_meta_hdl;

  if (!param.table_key_.is_ddl_dump_sstable()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check ddl sstable need copy get invalid argument", K(ret), K(param));
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), K(param), K(tablet_handle));
  } else if (OB_FAIL(tablet->fetch_table_store(table_store_wrapper))) {
    LOG_WARN("fail to fetch table store", K(ret));
  } else {
    const ObSSTableArray &ddl_sstable_array = table_store_wrapper.get_member()->get_ddl_sstables();
    const ObSSTableArray &major_sstable_array = table_store_wrapper.get_member()->get_major_sstables();

    if (!major_sstable_array.empty()) {
      need_copy = false;
    } else if (ddl_sstable_array.empty()) {
      need_copy = true;
    } else if (OB_FAIL(ddl_sstable_array.get_table(param.table_key_, sstable_wrapper))) {
      LOG_WARN("failed to get table", K(ret), K(param), K(ddl_sstable_array));
    } else if (nullptr == sstable_wrapper.get_sstable()) {
      const SCN start_scn = ddl_sstable_array.get_boundary_table(false)->get_start_scn();
      const SCN end_scn = ddl_sstable_array.get_boundary_table(true)->get_end_scn();
      if (param.table_key_.scn_range_.start_scn_ >= start_scn
          && param.table_key_.scn_range_.end_scn_ <= end_scn) {
        need_copy = false;
      } else {
        need_copy = true;
      }
    } else if (OB_FAIL(sstable_wrapper.get_sstable()->get_meta(sst_meta_hdl))) {
      LOG_WARN("failed to get sstable meta handle", K(ret));
    } else if (OB_FAIL(param.check_sstable_meta(sst_meta_hdl.get_sstable_meta()))) {
      LOG_WARN("failed to check sstable meta", K(ret), K(param), K(sstable_wrapper));
    } else {
      need_copy = false;
    }
  }
  return ret;
}

int ObStandbyRestoreTaskUtils::check_need_copy_macro_blocks(
    const blocksstable::ObMigrationSSTableParam &param,
    const bool is_leader_restore,
    bool &need_copy)
{
  int ret = OB_SUCCESS;
  need_copy = true;
  if (param.is_empty_sstable()) {
    need_copy = false;
  } else {
    UNUSED(is_leader_restore);
    need_copy = true;
  }
  return ret;
}

}
}
