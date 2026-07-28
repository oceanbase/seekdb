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

#include "ob_ls_service.h"
#include "share/rc/ob_module_provider.h"
#include "storage/ls/ob_ls.h"
#include "share/ls/ob_restore_status.h"
#include "logservice/ob_log_service.h"
#include "observer/ob_srv_network_frame.h"
#include "storage/tx/ob_trans_service.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"
#include "storage/tx/ob_trans_service.h"
#include "share/ob_share_util.h"  // relocated-definition owner
#include "storage/allocator/ob_mds_allocator.h"  // relocated-definition owner
#include "storage/allocator/ob_tx_data_allocator.h"  // relocated-definition owner
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"  // needed by relocated destructor logic in the throttle helper
#include "share/resource_limit_calculator/ob_resource_limit_calculator.h"  // relocated-definition owner

namespace oceanbase
{
using namespace share;
using namespace palf;
using namespace lib;
using namespace logservice;
namespace storage
{
#define OB_BREAK_FAIL(statement) (OB_UNLIKELY(((++process_point) && break_point == process_point && OB_FAIL(OB_BREAK_BY_TEST)) || OB_FAIL(statement)))

ObLSService::ObLSService()
  : is_inited_(false),
    is_running_(false),
    is_stopped_(false),
    ls_(nullptr),
    ls_allocator_(),
    change_lock_(common::ObLatchIds::LS_CHANGE_LOCK)
{}

ObLSService::~ObLSService()
{
  destroy();
}

void ObLSService::destroy()
{
  int ret = OB_SUCCESS;
  LOG_INFO("destroy ls service", KP(this));
  if (!is_inited_) {
    return;
  } else if (is_running_ || !is_stopped_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("should has been stopped before destroy", K(ret), K_(is_running), K_(is_stopped), KP(this));
  }
  if (OB_NOT_NULL(ls_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls service still owns log stream while destroying", K(ret), KP_(ls));
  }
  OB_ASSERT(OB_SUCCESS == ret);
  if (OB_SUCCESS != ret) {
    return;
  }
  ls_allocator_.destroy();
  is_inited_ = false;
}

int ObLSService::get_resource_constraint_value(ObResoureConstraintValue &constraint_value)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls service not inited, the resource info may not right.", K(ret));
  } else if (!is_running_ || is_stopped_) {
    ret = OB_NOT_RUNNING;
    LOG_WARN("the ls service is not running ,the resource info may not right.", K(ret));
  } else {
    ret = get_resource_constraint_value_(constraint_value);
  }

  return ret;
}

int ObLSService::get_resource_constraint_value_(ObResoureConstraintValue &constraint_value)
{
  int ret = OB_SUCCESS;
  const int64_t config_value = 1;
  const int64_t memory_value = 1;
  const int64_t clog_disk_value = 1;
  if (OB_FAIL(constraint_value.set_type_value(CONFIGURATION_CONSTRAINT, config_value))) {
    LOG_WARN("set_type_value failed", K(ret), K(CONFIGURATION_CONSTRAINT), K(config_value));
  } else if (OB_FAIL(constraint_value.set_type_value(MEMORY_CONSTRAINT, memory_value))) {
    LOG_WARN("set_type_value failed", K(ret), K(MEMORY_CONSTRAINT), K(memory_value));
  } else if (OB_FAIL(constraint_value.set_type_value(CLOG_DISK_CONSTRAINT, clog_disk_value))) {
    LOG_WARN("set_type_value failed", K(ret), K(CLOG_DISK_CONSTRAINT), K(clog_disk_value));
  }

  return ret;
}

int ObLSService::get_current_info(share::ObResourceInfo &info)
{
  int ret = OB_SUCCESS;
  ObResoureConstraintValue constraint_value;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls service not inited, the resource info may not right.", K(ret));
  } else if (!is_running_ || is_stopped_) {
    ret = OB_NOT_RUNNING;
    LOG_WARN("the ls service is not running ,the resource info may not right.", K(ret));
  } else if (OB_FAIL(get_resource_constraint_value_(constraint_value))) {
    LOG_WARN("get resource constraint value failed", K(ret));
  } else {
    info.curr_utilization_ = 1;
    info.max_utilization_ = info.curr_utilization_;
    info.reserved_value_ = 0; // reserve value will be used later
    constraint_value.get_min_constraint(info.min_constraint_type_, info.min_constraint_value_);
  }
  return ret;
}

int ObLSService::cal_min_phy_resource_needed(share::ObMinPhyResourceResult &min_phy_res)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls service not inited, the resource info may not right.", K(ret));
  } else if (!is_running_ || is_stopped_) {
    ret = OB_NOT_RUNNING;
    LOG_WARN("the ls service is not running ,the resource info may not right.", K(ret));
  } else {
    ret = cal_min_phy_resource_needed_(min_phy_res);
  }
  return ret;
}

int ObLSService::cal_min_phy_resource_needed(const int64_t num,
                                             ObMinPhyResourceResult &min_phy_res)
{
  UNUSED(num);
  return cal_min_phy_resource_needed_(min_phy_res);
}

int ObLSService::cal_min_phy_resource_needed_(ObMinPhyResourceResult &min_phy_res)
{
  int ret = OB_SUCCESS;
  const int64_t memory_bytes = BASE_RUNTIME_MEMORY_LIMIT;
  const int64_t clog_disk_bytes = MIN_DISK_SIZE_PER_PALF_INSTANCE;

  if (OB_FAIL(min_phy_res.set_type_value(PHY_RESOURCE_MEMORY, memory_bytes))) {
    LOG_WARN("set type value failed", K(PHY_RESOURCE_MEMORY), K(memory_bytes));
  } else if (OB_FAIL(min_phy_res.set_type_value(PHY_RESOURCE_CLOG_DISK, clog_disk_bytes))) {
    LOG_WARN("set type value failed", K(PHY_RESOURCE_CLOG_DISK), K(clog_disk_bytes));
  }
  return ret;
}

int ObLSService::stop()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ls service not inited, cannot stop.", K(ret));
  } else if (!is_running_ || is_stopped_) {
    // do nothing
  } else {
    ObLS *ls = nullptr;
    const bool remove_from_disk = false;

    lib::ObMutexGuard change_guard(change_lock_);
    if (OB_FAIL(get_ls(ls))) {
      if (OB_LS_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to get ls", K(ret));
      }
    } else if (OB_FAIL(stop_and_remove_ls_(ls, remove_from_disk))) {
      LOG_WARN("safe remove ls failed", K(ret), KPC(ls));
    }
    is_running_ = false;
    is_stopped_ = true;
  }
  LOG_INFO("stop ls service");
  return ret;
}

int ObLSService::wait()
{
  free_ls_(ls_);
  return OB_SUCCESS;
}

int ObLSService::server_module_init(ObLSService* &ls_service)
{
  return ls_service->init();
}

int ObLSService::init()
{
  int ret = OB_SUCCESS;
  const char *OB_LS_SERVICE = "LSSvr";
  const int64_t LS_ALLOC_TOTAL_LIMIT = 1024 * 1024 * 1024;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ls service is inited.", K_(is_inited), K(ret));
  } else if (OB_FAIL(ls_allocator_.init(common::OB_MALLOC_NORMAL_BLOCK_SIZE,
                                        OB_LS_SERVICE,
                                        LS_ALLOC_TOTAL_LIMIT))) {
    LOG_WARN("fail to init ls allocator, ", K(ret));
  } else {
    is_inited_ = true;
  }
  if (OB_FAIL(ret)) {
    ls_allocator_.destroy();
  }
  return ret;
}

int ObLSService::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ls service is not inited, cannot start.", K(ret));
  } else if (OB_UNLIKELY(is_running_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service is already running", K(ret));
  } else {
    LOG_INFO("ls service start successfully");
    is_running_ = true;
  }
  return ret;
}

int ObLSService::inner_create_ls_(const ObRestoreStatus &restore_status,
                                  const SCN &create_scn,
                                  ObLS *&ls)
{
  int ret = OB_SUCCESS;
  const char *OB_LS_MODE = "ObLS";
  ObMemAttr memattr(OB_LS_MODE);
  void *buf = NULL;
  if (OB_ISNULL(buf = ls_allocator_.alloc(sizeof(ObLS), memattr))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc ls", K(ret));
  } else if (FALSE_IT(ls = new (buf) ObLS())) {

  } else if (OB_FAIL(ls->init(restore_status, create_scn))) {
    LOG_WARN("fail to init ls", K(ret));
  }
  if (OB_FAIL(ret) && NULL != ls) {
    ls->~ObLS();
    ls_allocator_.free(ls);
    ls = NULL;
  }
  return ret;
}

void ObLSService::free_ls_(ObLS *ls)
{
  if (OB_NOT_NULL(ls)) {
    if (ls_ == ls) {
      ls_ = nullptr;
    }
    ls->~ObLS();
    ls_allocator_.free(ls);
  }
}

int ObLSService::publish_ls_(ObLS *ls)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_NOT_NULL(ls_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("the local log stream already exists", K(ret), KP(ls), KP_(ls));
  } else {
    ls_ = ls;
    LOG_INFO("publish local log stream", KP(ls));
  }
  return ret;
}

int ObLSService::create_ls(const share::ObServerRole &server_role)
{
  int ret = OB_SUCCESS;
  LOG_INFO("create_ls begin");
  DEBUG_SYNC(BEFORE_CREATE_USER_LS);

  ObCreateLSCommonArg arg;
  arg.server_role_ = server_role;
  arg.restore_status_ = ObRestoreStatus(ObRestoreStatus::Status::NONE);
  arg.create_scn_ = SCN::base_scn();
  arg.need_create_inner_tablet_ = true;
  if (OB_FAIL(create_ls_(arg))) {
    LOG_WARN("create ls failed", K(ret));
  }
  FLOG_INFO("create_ls finish", K(ret));
  return ret;
}

int ObLSService::create_ls_for_restore()
{
  int ret = OB_SUCCESS;
  ObCreateLSCommonArg arg;
  arg.server_role_ = share::RESTORE_SERVER_ROLE;
  arg.restore_status_ = ObRestoreStatus(ObRestoreStatus::Status::RESTORE_DOING);
  arg.create_scn_ = SCN::min_scn();
  arg.need_create_inner_tablet_ = false;
  if (OB_FAIL(create_ls_(arg))) {
    LOG_WARN("create system ls for restore failed", K(ret));
  }
  return ret;
}

int ObLSService::post_create_ls_(const bool is_restore, ObLS *ls)
{
  int ret = OB_SUCCESS;
  bool need_online = false;
  if (OB_FAIL(ls->check_ls_need_online(need_online))) {
    LOG_WARN("check ls need online failed", K(ret));
  } else if (need_online &&
             OB_FAIL(ls->online_without_lock())) {
    LOG_ERROR("ls start failed", K(ret));
  } else if (is_restore) {
    if (OB_FAIL(ls->set_start_restore_state())) {
      LOG_ERROR("ls set start restore state failed", KR(ret), KPC(ls));
    }
  } else if (OB_FAIL(ls->set_start_work_state())) {
    LOG_ERROR("ls set start work state failed", KR(ret), KPC(ls));
  }

  return ret;
}

int ObLSService::replay_create_ls(const int64_t ls_epoch, const ObLSMeta &ls_meta)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard change_guard(change_lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ls service has not been inited", K(ret));
  } else if (OB_UNLIKELY(!ls_meta.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ls_meta));
  } else if (OB_FAIL(replay_create_ls_(ls_epoch, ls_meta))) {
    LOG_WARN("fail to create ls for replay", K(ret), K(ls_meta));
  }

  return ret;
}

int ObLSService::replay_update_ls(const ObLSMeta &ls_meta)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ls service has not been inited", K(ret));
  } else if (OB_UNLIKELY(!ls_meta.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ls_meta));
  } else if (OB_FAIL(replay_update_ls_(ls_meta))) {
    LOG_WARN("fail to update ls for replay", K(ret), K(ls_meta));
  }

  return ret;
}

int ObLSService::replay_remove_ls()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ls service has not been inited", K(ret));
  } else if (OB_FAIL(replay_remove_ls_())) {
    LOG_WARN("fail to remove ls for replay", K(ret));
  }

  return ret;
}

int ObLSService::replay_create_ls_commit()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ls service has not been inited", K(ret));
  } else if (OB_FAIL(get_ls(ls))) {
    LOG_WARN("fail to get ls", K(ret));
  } else {
    ObLSLockGuard lock_ls(ls);
    if (OB_FAIL(ls->set_start_work_state())) {
      LOG_ERROR("ls set start work state failed", KR(ret), KPC(ls));
    }
    FLOG_INFO("replay create ls", KR(ret), KPC(ls));
  }
  return ret;
}

int ObLSService::gc_ls_after_replay_slog()
{
  // NOTE: we only gc the ls that not create finished or removed.
  // the migrate failed ls will be gc at ObGarbageCollector.
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  static const int64_t SLEEP_TS = 100_ms;
  ObLSPersistentState ls_status;
  ObLS *ls = nullptr;
  bool need_free = false;
  lib::ObMutexGuard change_guard(change_lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ls service has not been inited", K(ret));
  } else if (OB_FAIL(get_ls(ls))) {
    if (OB_LS_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get ls", K(ret));
    }
  // this must be succeed
  } else {
    ls_status = ls->get_persistent_state();
    if (ls_status.is_need_gc()) {
      do {
        if (OB_TMP_FAIL(ls->stop())) {
          LOG_WARN("ls stop failed", K(tmp_ret), KP(ls));
        } else {
          ls->wait();
        }
        if (OB_SUCCESS != tmp_ret) {
          ob_usleep(SLEEP_TS);
        }
      } while(tmp_ret != OB_SUCCESS);
    }
    {
      ObLSLockGuard lock_ls(ls);
      if (ls_status.is_init_state()) {
        do {
          if (OB_TMP_FAIL(LOCAL_STORAGE_META_PERSISTER.abort_create_ls())) {
            LOG_ERROR("fail to write create ls abort slog", K(tmp_ret), KPC(ls));
          }
          if (OB_TMP_FAIL(tmp_ret)) {
            ob_usleep(SLEEP_TS);
          }
        } while (tmp_ret != OB_SUCCESS);
        remove_ls_(ls, true/*remove_from_disk*/, false/*write_slog*/);
        need_free = true;
      } else if (ls_status.is_zombie_state()) {
        remove_ls_(ls, true/*remove_from_disk*/, false/*write_slog*/);
        need_free = true;
      }
    }
    if (need_free) {
      free_ls_(ls);
    }
  }

  return ret;
}

int ObLSService::online_ls()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  if (OB_FAIL(get_ls(ls))) {
    if (OB_LS_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get ls", K(ret));
    }
  } else {
    ObLSLockGuard lock_ls(ls);
    if (OB_FAIL(post_create_ls_(false, ls))) {
      LOG_WARN("post create ls failed", K(ret));
    }
  }

  return ret;
}

int ObLSService::replay_update_ls_(const ObLSMeta &ls_meta)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  if (OB_FAIL(get_ls(ls))) {
    LOG_WARN("fail to get ls", K(ls_meta));
  } else if (OB_FAIL(ls->set_ls_meta(ls_meta))) {
    LOG_WARN("fail to set ls's meta for replay", K(ls_meta));
  }
  return ret;
}

int ObLSService::replay_remove_ls_()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  if (OB_FAIL(get_ls(ls))) {
    LOG_WARN("fail to get ls", K(ret));
  } else if (OB_FAIL(ls->set_remove_state())) {
    LOG_ERROR("ls set remove state failed", KR(ret), KPC(ls));
  } else {
  }
  return ret;
}

int ObLSService::replay_create_ls_(const int64_t ls_epoch, const ObLSMeta &ls_meta)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObLSCreateState state = ObLSCreateState::CREATE_STATE_INIT;

  if (OB_SUCCESS == (ret = get_ls(ls))) {
    ObLSLockGuard lock_ls(ls);
    if (OB_FAIL(ls->set_ls_meta(ls_meta))) {
      LOG_WARN("fail to update ls meta for replay", K(ret), K(ls_meta));
    } else if (OB_FAIL(ls->set_ls_epoch(ls_epoch))) {
      LOG_WARN("fail to update ls epoch for replay", K(ret), K(ls_epoch));
    } else {
      LOG_INFO("updated existing ls for replay", K(ls_epoch), K(ls_meta));
    }
  } else if (OB_LS_NOT_EXIST != ret) {
    LOG_WARN("fail to get ls before replay create", K(ret), K(ls_meta));
  } else if (FALSE_IT(ret = OB_SUCCESS)) {
  } else if (OB_FAIL(inner_create_ls_(ObRestoreStatus(ObRestoreStatus::Status::NONE),
                                      ls_meta.get_clog_checkpoint_scn(),
                                      ls))) {
    LOG_WARN("fail to inner create ls", K(ret));
  } else {
    state = ObLSCreateState::CREATE_STATE_LS_ALLOCATED;
    ObLSLockGuard lock_ls(ls);
    if (OB_FAIL(ls->set_ls_meta(ls_meta))) {
      LOG_WARN("set ls meta failed", K(ret), K(ls_meta));
    } else if (OB_FAIL(ls->set_ls_epoch(ls_epoch))) {
      LOG_WARN("fail to set ls epoch", K(ret), K(ls_epoch));
    } else if (OB_FAIL(publish_ls_(ls))) {
      LOG_WARN("fail to publish replayed ls", K(ret), K(ls_meta));
    } else if (FALSE_IT(state = ObLSCreateState::CREATE_STATE_PUBLISHED)) {
    } else if (OB_FAIL(ls->load_ls())) {
      LOG_WARN("enable ls palf failed", K(ret), K(ls_meta));
    } else {
      LOG_INFO("success replay create ls", K(ret), K(ls_meta));
    }
  }
  if (OB_FAIL(ret)) {
    del_ls_after_create_ls_failed_(state, ls);
  }
  return ret;
}

int ObLSService::get_ls(ObLS *&ls)
{
  int ret = OB_SUCCESS;
  ls = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(ls_)) {
    ret = OB_LS_NOT_EXIST;
  } else {
    ls = ls_;
  }

  return ret;
}

int ObLSService::stop_and_remove_ls_(ObLS *ls, const bool remove_from_disk)
{
  int ret = OB_SUCCESS;
  int64_t process_point = 0; // for test
  if (OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ls", K(ret));
  } else if (OB_BREAK_FAIL(ls->offline())) {
    LOG_WARN("ls offline failed", K(ret), KP(ls));
  } else if (OB_BREAK_FAIL(ls->stop())) {
    LOG_WARN("stop ls failed", K(ret), KP(ls));
  } else if (FALSE_IT(ls->wait())) {
  } else {
    {
      ObLSLockGuard lock_ls(ls);
      const bool write_slog = remove_from_disk;
      if (remove_from_disk && OB_BREAK_FAIL(ls->set_remove_state())) {
        LOG_WARN("ls set remove state failed", KR(ret));
      } else {
        remove_ls_(ls, remove_from_disk, write_slog);
      }
    }
  }
  return ret;
}

void ObLSService::remove_ls_(ObLS *ls, const bool remove_from_disk, const bool write_slog)
{
  int ret = OB_SUCCESS;
  static const int64_t SLEEP_TS = 100_ms;
  int64_t retry_cnt = 0;
  int64_t success_step = 0;

  do {
    // We must do prepare_for_safe_destroy to remove tablets from ObLSTabletService before writing the remove_ls_slog,
    // After removing tablets, no update_tablet_slog will be written. Otherwise, writing the update_tablet_slog will be
    // concurrent with remove_ls_slog, causing the update_tablet_slog to fall behind remove_ls_slog, and causing replay
    // creating an invalid tablet during restart.
    ret = OB_SUCCESS;
    if (success_step < 1) {
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ls->prepare_for_safe_destroy())) {
        LOG_WARN("prepare safe destroy failed", K(ret), KPC(ls));
      } else {
        success_step = 1;
      }
    }
    if (success_step < 2 && OB_SUCC(ret)) {
      // todo zk250686_ copy tablet_id_set to tablet_free_pending_array
      if(write_slog && OB_FAIL(LOCAL_STORAGE_META_PERSISTER.delete_ls())) {
        LOG_ERROR("fail to write remove ls slog", K(ret));
      } else {
        success_step = 2;
      }
    }
    if (success_step < 3 && OB_SUCC(ret)) {
      if (remove_from_disk && OB_FAIL(ls->remove_ls())) {
        LOG_WARN("remove ls from disk failed", K(ret), K(remove_from_disk));
      } else {
        success_step = 3;
      }
    }
    if (OB_FAIL(ret)) {
      retry_cnt++;
      ob_usleep(SLEEP_TS);
      if (retry_cnt % 100 == 0) {
        LOG_ERROR("remove_ls_ cost too much time", K(ret), KP(ls), K(success_step));
      }
    }
  } while (OB_FAIL(ret));
}

int ObLSService::create_ls_(const ObCreateLSCommonArg &arg)
{
  int ret = OB_SUCCESS;
  int64_t abs_timeout_ts = INT64_MAX;
  ObLSCreateState state = ObLSCreateState::CREATE_STATE_INIT;
  ObLS *ls = NULL;
  int64_t process_point = 0;
  palf::PalfBaseInfo palf_base_info;
  palf_base_info.generate_by_default();
  palf_base_info.prev_log_info_.scn_ = arg.create_scn_;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ls service has not been inited", K(ret));
  } else if (OB_UNLIKELY(!SERVER_STORAGE_META_SERVICE.is_started())) {
    ret = OB_NOT_RUNNING;
    LOG_WARN("ls service does not service before slog replay finished", K(ret));
  } else if (OB_BREAK_FAIL(ObShareUtil::get_abs_timeout(DEFAULT_LOCK_TIMEOUT /* default timeout */,
                                                  abs_timeout_ts))) {
    LOG_WARN("get timeout ts failed", KR(ret));
  } else {
    ObMutexGuardWithTimeout change_guard(change_lock_, abs_timeout_ts);
    if (OB_UNLIKELY(!is_running_)) {
      ret = OB_NOT_RUNNING;
      LOG_WARN("ls service is not running.", K(ret));
    } else if (OB_BREAK_FAIL(change_guard.get_ret())) {
      LOG_WARN("lock failed, try again later", K(ret));
      ret = OB_EAGAIN;
    } else if (OB_NOT_NULL(ls_)) {
      ret = OB_INIT_TWICE;
      LOG_WARN("the local log stream already exists", K(ret));
    } else if (OB_BREAK_FAIL(inner_create_ls_(arg.restore_status_,
                                              arg.create_scn_,
                                              ls))) {
      LOG_WARN("create ls failed", K(ret));
    } else {
      state = ObLSCreateState::CREATE_STATE_LS_ALLOCATED;
      int64_t ls_epoch = 0;
      ObLSLockGuard lock_ls(ls);
      const ObLSMeta &ls_meta = ls->get_ls_meta();
      if (OB_BREAK_FAIL(publish_ls_(ls))) {
        LOG_WARN("publish log stream failed", K(ret), K(ls_meta));
      } else if (FALSE_IT(state = ObLSCreateState::CREATE_STATE_PUBLISHED)) {
      } else if (OB_BREAK_FAIL(LOCAL_STORAGE_META_PERSISTER.prepare_create_ls(ls_meta, ls_epoch))) {
        LOG_ERROR("fail to write create log stream slog", K(ls_meta));
      } else if (OB_FAIL(ls->set_ls_epoch(ls_epoch))) {
        LOG_WARN("fail to set ls epoch", K(ret));
      } else if (FALSE_IT(state = ObLSCreateState::CREATE_STATE_WRITE_PREPARE_SLOG)) {
      } else if (OB_BREAK_FAIL(ls->create_ls(palf_base_info))) {
        LOG_WARN("enable ls palf failed", K(ret), K(ls_meta));
      } else if (FALSE_IT(state = ObLSCreateState::CREATE_STATE_PALF_ENABLED)) {
      } else if (arg.need_create_inner_tablet_ && OB_FAIL(ls->create_ls_inner_tablet(arg.create_scn_))) {
        LOG_WARN("create ls inner tablet failed", K(ret), K(ls_meta));
      } else if (FALSE_IT(state = ObLSCreateState::CREATE_STATE_INNER_TABLET_CREATED)) {
      } else if (OB_BREAK_FAIL(LOCAL_STORAGE_META_PERSISTER.commit_create_ls())) {
        LOG_ERROR("fail to write create log stream commit slog", K(ret), K(ls_meta));
      } else if (OB_BREAK_FAIL(ls->finish_create_ls())) {
        LOG_WARN("finish create ls failed", KR(ret));
      } else if (FALSE_IT(state = ObLSCreateState::CREATE_STATE_FINISH)) {
      } else if (OB_BREAK_FAIL(post_create_ls_(arg.server_role_.is_restore(), ls))) {
        LOG_WARN("post create ls failed", K(ret), K(ls_meta));
      }
    }
    if (OB_BREAK_FAIL(ret)) {
      del_ls_after_create_ls_failed_(state, ls);
    }
  }
  return ret;
}

void ObLSService::del_ls_after_create_ls_failed_(ObLSCreateState& in_ls_create_state, ObLS *ls)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  bool need_retry = false;
  const bool remove_from_disk = true;
  ObLSCreateState ls_create_state = in_ls_create_state;
  if (OB_NOT_NULL(ls)) {
    do {
      need_retry = false;
      tmp_ret = OB_SUCCESS;
      if (ls_create_state >= ObLSCreateState::CREATE_STATE_FINISH) {
        if (OB_TMP_FAIL(stop_and_remove_ls_(ls, remove_from_disk))) {
          need_retry = true;
          LOG_WARN("safe remove ls failed", K(tmp_ret));
        } else {
          free_ls_(ls);
          ls_create_state = ObLSCreateState::CREATE_STATE_INIT;
        }
      } else {
        if (ls_create_state >= ObLSCreateState::CREATE_STATE_INNER_TABLET_CREATED) {
          if (OB_TMP_FAIL(ls->remove_ls_inner_tablet())) {
            need_retry = true;
            LOG_WARN("remove ls inner tablet failed", K(tmp_ret));
          } else {
            ls_create_state = ObLSCreateState::CREATE_STATE_PALF_ENABLED;
          }
        }
        if (OB_TMP_FAIL(tmp_ret)) {
        } else if (ls_create_state >= ObLSCreateState::CREATE_STATE_PALF_ENABLED) {
          if (OB_TMP_FAIL(ls->remove_ls())) {
            need_retry = true;
            LOG_WARN("ls inner remove failed", K(tmp_ret));
          } else {
            ls_create_state = ObLSCreateState::CREATE_STATE_WRITE_PREPARE_SLOG;
          }
        }
        if (OB_TMP_FAIL(tmp_ret)) {
        } else if (ls_create_state >= ObLSCreateState::CREATE_STATE_WRITE_PREPARE_SLOG) {
          if (OB_TMP_FAIL(ls->set_remove_state())) {
            need_retry = true;
            LOG_ERROR("fail to set ls remove state", K(tmp_ret), KPC(ls));
          } else if (OB_TMP_FAIL(LOCAL_STORAGE_META_PERSISTER.abort_create_ls())) {
            need_retry = true;
            LOG_ERROR("fail to write create log stream abort slog", K(tmp_ret), KPC(ls));
          } else {
            ls_create_state = ObLSCreateState::CREATE_STATE_PUBLISHED;
          }
        }
        if (OB_TMP_FAIL(tmp_ret)) {
        } else if (ls_create_state >= ObLSCreateState::CREATE_STATE_PUBLISHED) {
          ls_ = nullptr;
          ls_create_state = ObLSCreateState::CREATE_STATE_LS_ALLOCATED;
        }
        if (OB_TMP_FAIL(tmp_ret)) {
        } else if (ls_create_state >= ObLSCreateState::CREATE_STATE_LS_ALLOCATED) {
          if (OB_TMP_FAIL(ls->prepare_for_safe_destroy())) {
            need_retry = true;
            LOG_WARN("prepare failed ls for destroy failed", K(tmp_ret), KPC(ls));
          } else {
            free_ls_(ls);
            ls_create_state = ObLSCreateState::CREATE_STATE_INIT;
          }
        }
      }
    } while (need_retry);
  }
  in_ls_create_state = ls_create_state;
}

} // storage
} // oceanbase


// ===== definition moved from share/ob_share_util.cpp =====
// real user ObLSService/ObLS complete type(previously hidden behind share_util's removed include chain); declaration remains in share/ob_share_util.h(transitional state)
namespace oceanbase
{
namespace share
{

// get_ls_readable_scn is a storage helper backed by the tenant LS service.


}  // namespace share
}  // namespace oceanbase

// ===== definition moved from src/storage/allocator/ob_mds_allocator.cpp / src/storage/allocator/ob_tx_data_allocator.cpp =====
namespace oceanbase
{
namespace share
{

ObMdsThrottleGuard::~ObMdsThrottleGuard()
{
  int ret = OB_SUCCESS;
  storage::ObLS *ls = nullptr;
  ObThrottleInfoGuard share_ti_guard;
  ObThrottleInfoGuard module_ti_guard;

  if (OB_ISNULL(throttle_tool_)) {
    MDS_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "throttle tool is unexpected nullptr", KP(throttle_tool_));
  } else if (throttle_tool_->is_throttling<ObMdsAllocator>(share_ti_guard, module_ti_guard)) {

    if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
      STORAGE_LOG(WARN, "get ls failed", KR(ret));
    } else {
      (void)TxShareMemThrottleUtil::do_throttle<ObMdsAllocator>(for_replay_,
                                                                      abs_expire_time_,
                                                                      share::mds_throttled_alloc(),
                                                                      share::g_mp->memstore_freezer()->exist_ls_throttle_is_skipping(),
                                                                      ls->is_offline(),
                                                                      *throttle_tool_,
                                                                      share_ti_guard,
                                                                      module_ti_guard);
    }

    if (throttle_tool_->still_throttling<ObMdsAllocator>(share_ti_guard, module_ti_guard)) {
      (void)throttle_tool_->skip_throttle<ObMdsAllocator>(
          share::mds_throttled_alloc(), share_ti_guard, module_ti_guard);

      if (module_ti_guard.is_valid()) {
        module_ti_guard.throttle_info()->reset();
      }
    }

    // reset mds throttled alloc size
    share::mds_throttled_alloc() = 0;
  } else {
    // do not need throttle, exit directly
  }
}

ObTxDataThrottleGuard::~ObTxDataThrottleGuard()
{
  int ret = OB_SUCCESS;
  storage::ObLS *ls = nullptr;
  ObThrottleInfoGuard share_ti_guard;
  ObThrottleInfoGuard module_ti_guard;

  if (OB_ISNULL(throttle_tool_)) {
    MDS_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "throttle tool is unexpected nullptr", KP(throttle_tool_));
  } else if (throttle_tool_->is_throttling<ObTxDataAllocator>(share_ti_guard, module_ti_guard)) {
    if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
      STORAGE_LOG(WARN, "get ls failed", KR(ret));
    } else {
      (void)TxShareMemThrottleUtil::do_throttle<ObTxDataAllocator>(for_replay_,
                                                                         abs_expire_time_,
                                                                         share::tx_data_throttled_alloc(),
                                                                         share::g_mp->memstore_freezer()->exist_ls_throttle_is_skipping(),
                                                                      ls->is_offline(),
                                                                         *throttle_tool_,
                                                                         share_ti_guard,
                                                                         module_ti_guard);
    }

    if (throttle_tool_->still_throttling<ObTxDataAllocator>(share_ti_guard, module_ti_guard)) {
      (void)throttle_tool_->skip_throttle<ObTxDataAllocator>(
          share::tx_data_throttled_alloc(), share_ti_guard, module_ti_guard);

      if (module_ti_guard.is_valid()) {
        module_ti_guard.throttle_info()->reset();
      }
    }

    // reset tx data throttled alloc size
    share::tx_data_throttled_alloc() = 0;
  } else {
    // do not need throttle, exit directly
  }
}

}  // namespace share
}  // namespace oceanbase

// ===== definition moved from share resource_limit_calculator(X-macro inventory 2fn) =====
namespace oceanbase
{
namespace share
{

int ObResourceLimitCalculator::init()
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("resource limit calculator already initialized", K(ret));
  } else {
    WLockGuard guard(lock_);
#define DEF_RESOURCE_LIMIT_CALCULATOR(n, type, name, subhandler)      \
    if (OB_SUCC(ret)) {                                               \
        handlers_[n] = subhandler;                                    \
    }
#include "share/resource_limit_calculator/ob_resource_limit_calculator_def.h"
#undef DEF_RESOURCE_LIMIT_CALCULATOR
    is_inited_ = true;
  }
  return ret;
}


}  // namespace share
}  // namespace oceanbase

// ===== definition moved from share resource_limit_calculator(second overload) =====
namespace oceanbase
{
namespace share
{

int ObResourceLimitCalculator::get_min_phy_resource_value(
    const ObUserResourceCalculateArg &arg,
    ObMinPhyResourceResult &res)
{
  int ret = OB_SUCCESS;
  ObIResourceLimitCalculatorHandler *handler = NULL;
  ObMinPhyResourceResult min_res;
  ObMinPhyResourceResult tmp;
  int64_t res_type = 0;
  int64_t need_num = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_RUNNING;
    LOG_WARN("resource limit calculator not running", K(ret));
  } else {
    RLockGuard guard(lock_);
#define DEF_RESOURCE_LIMIT_CALCULATOR(n, type, name, subhandler)              \
    if (OB_SUCC(ret)) {                                                       \
      if (OB_ISNULL(handler = handlers_[n])) {                                \
        ret = OB_NOT_RUNNING;                                                 \
        LOG_WARN("resource handler is unavailable", K(ret), K(n), KP(handler)); \
      } else if (OB_FAIL(arg.get_type_value(n, need_num))) {                  \
        LOG_WARN("get needed num failed", K(ret), K(n));                      \
      } else if (OB_FAIL(handler->cal_min_phy_resource_needed(need_num,       \
                                                              tmp))) {        \
        LOG_WARN("get resource stat failed", K(ret), K(n), K(need_num));      \
      } else if (OB_FAIL(min_res.inc_update(tmp))) {                          \
        LOG_WARN("inc_update failed", K(ret), K(min_res), K(tmp));            \
      } else {                                                                \
        tmp.reset();                                                          \
      }                                                                       \
    }
#include "share/resource_limit_calculator/ob_resource_limit_calculator_def.h"
#include "storage/tx/ob_tx_data_define.h"  // needed by relocated functions
#undef DEF_RESOURCE_LIMIT_CALCULATOR

    if (OB_SUCC(ret)) {
      res = min_res;
      ret = res.get_copy_assign_ret();
    }
  }
  return ret;
}


}  // namespace share
}  // namespace oceanbase

// ===== tx_data_allocator(TX_DATA_SLICE_SIZE fns) =====
namespace oceanbase
{
namespace share
{


}  // namespace share
}  // namespace oceanbase

// ===== tx_data init/alloc =====
namespace oceanbase
{
namespace share
{

OB_WEAK_SYMBOL int ObTxDataAllocator::init(const char *label)
{
  int ret = OB_SUCCESS;
  ObMemAttr mem_attr;
  mem_attr.label_ = label;
  mem_attr.ctx_id_ = ObCtxIds::TX_DATA_TABLE;
  ObSharedMemAllocMgr *share_mem_alloc_mgr = share::g_mp->shared_mem_alloc_mgr();
  throttle_tool_ = &(share_mem_alloc_mgr->share_resource_throttle_tool());
  if (IS_INIT){
    ret = OB_INIT_TWICE;
    SHARE_LOG(WARN, "init tx-data allocator twice", KR(ret), KPC(this));
  } else if (OB_ISNULL(throttle_tool_)) {
    ret = OB_ERR_UNEXPECTED;
    SHARE_LOG(WARN, "throttle tool is unexpected null", KP(throttle_tool_), KP(share_mem_alloc_mgr));
  } else if (OB_FAIL(slice_allocator_.init(
                 storage::TX_DATA_SLICE_SIZE, OB_MALLOC_NORMAL_BLOCK_SIZE, block_alloc_, mem_attr))) {
    SHARE_LOG(WARN, "init slice allocator failed", KR(ret));
  } else {
    slice_allocator_.set_nway(ObTxDataAllocator::ALLOC_TX_DATA_MAX_CONCURRENCY);
    is_inited_ = true;
  }
  return ret;
}


OB_WEAK_SYMBOL void *ObTxDataAllocator::alloc(const bool enable_throttle, const int64_t abs_expire_time)
{
  // do throttle if needed
  if (OB_LIKELY(enable_throttle)) {
    bool is_throttled = false;
    (void)throttle_tool_->alloc_resource<ObTxDataAllocator>(
        storage::TX_DATA_SLICE_SIZE, abs_expire_time, is_throttled);

    if (OB_UNLIKELY(is_throttled)) {
      share::tx_data_throttled_alloc() += storage::TX_DATA_SLICE_SIZE;
    }
  }

  // allocate memory
  void *res = slice_allocator_.alloc();
  return res;
}


}  // namespace share
}  // namespace oceanbase

// from share::ObShareUtil demoted  storage free function(A-set member-split cleanup)
namespace oceanbase
{
namespace storage
{
using namespace oceanbase::share;
int get_sys_ls_readable_scn(SCN &readable_scn)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
      LOG_WARN("get log stream failed", KR(ret));
  } else if (OB_FAIL(ls->get_max_decided_scn(readable_scn))) {
    LOG_WARN("failed to get_max_decided_scn", KR(ret), KPC(ls));
  }
  return ret;
}
}  // namespace storage
}  // namespace oceanbase
