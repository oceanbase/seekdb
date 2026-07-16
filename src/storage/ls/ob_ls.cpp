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

#include "storage/multi_data_source/runtime_utility/common_define.h"
#include "share/rc/ob_module_provider.h"
#define USING_LOG_PREFIX STORAGE

#include "logservice/ob_log_service.h"
#include "observer/ob_srv_network_frame.h"
#include "rootserver/freeze/ob_major_freeze_service.h"
#include "observer/dbms_scheduler/ob_dbms_sched_service.h"
#include "rootserver/ddl_task/ob_ddl_scheduler.h" // for ObDDLScheduler
#include "rootserver/ob_ddl_service_launcher.h" // for ObDDLServiceLauncher
#include "observer/ob_sys_tenant_load_sys_package_service.h" // for ObSysTenantLoadSysPackageService
#include "share/ob_global_autoinc_service.h"
#include "share/ob_internal_table_change_notifier.h"
#include "storage/compaction/ob_tenant_tablet_scheduler.h"
#include "storage/compaction/ob_tablet_merge_ctx.h"
#include "storage/ls/ob_ls.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "storage/tx/ob_timestamp_service.h"
#include "storage/tx/ob_trans_id_service.h"
#include "observer/vector_index/ob_plugin_vector_index_service.h"
#include "storage/tx_storage/ob_tenant_freezer.h"

namespace oceanbase
{
using namespace share;
using namespace logservice;
using namespace transaction;
using namespace rootserver;

namespace storage
{

using namespace checkpoint;
using namespace mds;

const share::SCN ObLS::LS_INNER_TABLET_FROZEN_SCN = share::SCN::base_scn();

const uint64_t ObLS::INNER_TABLET_ID_LIST[TOTAL_INNER_TABLET_NUM] = {
    common::ObTabletID::LS_TX_CTX_TABLET_ID,
    common::ObTabletID::LS_TX_DATA_TABLET_ID,
    common::ObTabletID::LS_LOCK_TABLET_ID,
};

ObLS::ObLS()
  : ls_tx_svr_(this),
    replay_handler_(this),
    ls_freezer_(this),
    ls_sync_tablet_seq_handler_(),
    ls_ddl_log_handler_(),
    is_inited_(false),
    running_state_(),
    state_seq_(-1),
    switch_epoch_(0),
    ls_meta_(),
    ls_epoch_(0),
    need_delay_resource_recycle_(false)
{}

ObLS::~ObLS()
{
  destroy();
}

int ObLS::init(const ObRestoreStatus &restore_status,
               const SCN &create_scn)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObLogService *logservice = share::g_mp->log_service();
  ObTransService *txs_svr = share::g_mp->trans_service();

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ls is already initialized", K(ret), K_(ls_meta));
  } else if (OB_FAIL(ls_meta_.init(restore_status,
                                   create_scn))) {
    LOG_WARN("failed to init ls meta", K(ret));
  } else if (OB_FAIL(ls_freezer_.init(this))) {
    LOG_WARN("init freezer failed", K(ret));
  } else {
    ObTxPalfParam tx_palf_param(get_log_handler());
    common::ObInOutBandwidthThrottle *bandwidth_throttle = GCTX.bandwidth_throttle_;
    if (OB_ISNULL(bandwidth_throttle)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("bandwidth throttle should not be NULL", KR(ret));
    } else if (OB_FAIL(txs_svr->create_ls(*this, &tx_palf_param, nullptr))) {
      LOG_WARN("create trans service failed.", K(ret));
    } else if (OB_FAIL(ls_tablet_svr_.init(this))) {
      LOG_WARN("ls tablet service init failed.", K(ret));
    } else if (OB_FAIL(tx_table_.init(this))) {
      LOG_WARN("init tx table failed",K(ret));
    } else if (OB_FAIL(checkpoint_executor_.init(this, get_log_handler()))) {
      LOG_WARN("checkpoint executor init failed", K(ret));
    } else if (OB_FAIL(data_checkpoint_.init(this))) {
      LOG_WARN("init data checkpoint failed",K(ret));
    } else if (OB_FAIL(ls_tx_svr_.register_common_checkpoint(checkpoint::DATA_CHECKPOINT_TYPE, &data_checkpoint_))) {
      LOG_WARN("data_checkpoint_ register_common_checkpoint failed", K(ret));
    } else if (OB_FAIL(lock_table_.init(this))) {
      LOG_WARN("init lock table failed",K(ret));
    } else if (OB_FAIL(ls_sync_tablet_seq_handler_.init(this))) {
      LOG_WARN("init ls sync tablet seq handler failed", K(ret));
    } else if (OB_FAIL(ls_ddl_log_handler_.init(this))) {
      LOG_WARN("init ls ddl log handler failed", K(ret));
    } else if (OB_FAIL(keep_alive_ls_handler_.init(get_log_handler()))) {
      LOG_WARN("init keep_alive_ls_handler failed", K(ret));
    } else if (OB_FAIL(ls_wrs_handler_.init())) {
      LOG_WARN("ls loop worker init failed", K(ret));
    } else if (OB_FAIL(tablet_gc_handler_.init(this))) {
      LOG_WARN("failed to init tablet gc handler", K(ret));
    } else if (OB_FAIL(tablet_empty_shell_handler_.init(this))) {
      LOG_WARN("failed to init tablet_empty_shell_handler", K(ret));
    } else if (OB_FAIL(reserved_snapshot_mgr_.init(this, &log_handler_))) {
      LOG_WARN("failed to init reserved snapshot mgr", K(ret));
    } else if (OB_FAIL(reserved_snapshot_clog_handler_.init(this))) {
      LOG_WARN("failed to init reserved snapshot clog handler", K(ret));
    } else if (OB_FAIL(medium_compaction_clog_handler_.init(this))) {
      LOG_WARN("failed to init medium compaction clog handler", K(ret));
    } else if (OB_FAIL(register_to_service_())) {
      LOG_WARN("register to service failed", K(ret));
    } else {
      need_delay_resource_recycle_ = false;
      is_inited_ = true;
      LOG_INFO("ls init success");
    }
    // do some rollback work
    if (OB_FAIL(ret)) {
      destroy();
    }
  }
  return ret;
}

int ObLS::create_ls_inner_tablet(const lib::Worker::CompatMode compat_mode,
                                 const SCN &create_scn)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_FAIL(tx_table_.create_tablet(compat_mode, create_scn))) {
    LOG_WARN("tx table create tablet failed", K(ret), K_(ls_meta), K(compat_mode), K(create_scn));
  } else if (OB_FAIL(lock_table_.create_tablet(compat_mode, create_scn))) {
    LOG_WARN("lock table create tablet failed", K(ret), K_(ls_meta), K(compat_mode), K(create_scn));
  }
  if (OB_FAIL(ret)) {
    do {
      if (OB_TMP_FAIL(remove_ls_inner_tablet())) {
        LOG_WARN("remove ls inner tablet failed", K(tmp_ret));
      }
    } while (OB_TMP_FAIL(tmp_ret));
  }
  return ret;
}

int ObLS::remove_ls_inner_tablet()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tx_table_.remove_tablet())) {
    LOG_WARN("tx table remove tablet failed", K(ret), K_(ls_meta));
  } else if (OB_FAIL(lock_table_.remove_tablet())) {
    LOG_WARN("lock table remove tablet failed", K(ret), K_(ls_meta));
  }
  return ret;
}

int ObLS::create_ls(const palf::PalfBaseInfo &palf_base_info)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  bool is_palf_exist = false;
  bool need_retry = false;
  static const int64_t SLEEP_TS = 100_ms;
  int64_t retry_cnt = 0;
  ObLogService *logservice = share::g_mp->log_service();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls do not init", K(ret));
  } else if (OB_FAIL(logservice->check_palf_exist(is_palf_exist))) {
    LOG_WARN("check_palf_exist failed", K(ret), K_(ls_meta));
  } else if (is_palf_exist) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("palf should not exist now", K(ret), K_(ls_meta));
  } else if (OB_FAIL(logservice->create_ls(palf_base_info, log_handler_))) {
    LOG_WARN("create palf failed", K(ret), K_(ls_meta));
  } else {
    if (OB_FAIL(ret)) {
      do {
        // TODO: yanyuan.cxf every remove disable or stop function need be re-entrant
        need_retry = false;
        if (OB_TMP_FAIL(remove_ls())) {
          need_retry = true;
          LOG_WARN("remove_ls from disk failed", K(tmp_ret), K_(ls_meta));
        }
        if (need_retry) {
          retry_cnt++;
          ob_usleep(SLEEP_TS);
          if (retry_cnt % 100 == 0) {
            LOG_ERROR("remove_ls from disk cost too much time", K(tmp_ret), K(need_retry), K_(ls_meta));
          }
        }
      } while (need_retry);
    }
  }
  return ret;
}

int ObLS::load_ls()
{
  int ret = OB_SUCCESS;
  ObLogService *logservice = share::g_mp->log_service();
  bool is_palf_exist = false;

  if (OB_FAIL(logservice->check_palf_exist(is_palf_exist))) {
    LOG_WARN("check_palf_exist failed", K(ret), K_(ls_meta));
  } else if (!is_palf_exist) {
    LOG_WARN("there is no ls at disk, skip load", K_(ls_meta));
  } else if (OB_FAIL(logservice->add_ls(log_handler_))) {
    LOG_WARN("add ls failed", K(ret), K_(ls_meta));
  } else {
    // TODO: add_ls has no interface to rollback now, something can not rollback.
    if (OB_FAIL(ret)) {
      LOG_ERROR("load ls failed", K(ret), K(ls_meta_));
    }
  }
  return ret;
}

int ObLS::remove_ls()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObLogService *logservice = share::g_mp->log_service();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls do not init", K(ret));
  } else {
    if (OB_FAIL(logservice->remove_ls(log_handler_))) {
      LOG_ERROR("remove log stream from logservice failed", K(ret));
    }
  }
  LOG_INFO("remove ls from disk", K(ret), K(ls_meta_));
  return ret;
}

void ObLS::update_state_seq_()
{
  inc_update(&state_seq_, max(ObTimeUtil::current_time(), state_seq_ + 1));
}

int ObLS::set_start_work_state()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ls_meta_.set_start_work_state())) {
    LOG_WARN("set start work state failed", K(ret), K_(ls_meta));
  } else {
    update_state_seq_();
  }
  return ret;
}

int ObLS::set_remove_state()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ls_meta_.set_remove_state())) {
    LOG_WARN("set remove state failed", K(ret), K_(ls_meta));
  } else {
    update_state_seq_();
  }
  return ret;
}

ObLSPersistentState ObLS::get_persistent_state() const
{
  return ls_meta_.get_persistent_state();
}

int ObLS::finish_create_ls()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(running_state_.create_finish())) {
    LOG_WARN("finish create ls failed", KR(ret), K(ls_meta_));
  } else {
    update_state_seq_();
  }
  return ret;
}

int ObLS::stop()
{
  int64_t read_lock = 0;
  int64_t write_lock = LSLOCKALL;

  ObLSLockGuard lock_myself(this, lock_, read_lock, write_lock);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else if (OB_FAIL(stop_())) {
    LOG_WARN("stop ls failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(running_state_.stop())) {
    LOG_WARN("set stop state failed", K(ret), K(ls_meta_));
  } else {
    inc_update(&state_seq_, max(ObTimeUtil::current_time(), state_seq_ + 1));
  }
  return ret;
}

int ObLS::stop_()
{
  int ret = OB_SUCCESS;

  tx_table_.stop();
  keep_alive_ls_handler_.stop();
  if (OB_FAIL(log_handler_.stop())) {
    LOG_WARN("stop log handler failed", K(ret), KPC(this));
  }
  ls_tablet_svr_.stop();
  stop_vector_idx_scheduler_();

  return ret;
}

void ObLS::wait()
{
  ObTimeGuard time_guard("ObLS::wait", 10 * 1000 * 1000);
  int64_t read_lock = LSLOCKALL;
  int64_t write_lock = 0;
  bool wait_finished = true;
  int64_t start_ts = ObTimeUtility::current_time();
  int64_t retry_times = 0;

  do {
    retry_times++;
    {
      ObLSLockGuard lock_myself(this, lock_, read_lock, write_lock);
    }
    if (!wait_finished) {
      ob_usleep(100 * 1000); // 100 ms
      if (retry_times % 100 == 0) { // every 10 s
        LOG_WARN_RET(OB_ERR_UNEXPECTED, "ls wait not finished.", K(ls_meta_), K(start_ts));
      }
    }
  } while (!wait_finished);
}

void ObLS::wait_()
{
  ObTimeGuard time_guard("ObLS::wait", 10 * 1000 * 1000);
  bool wait_finished = true;
  int64_t start_ts = ObTimeUtility::current_time();
  int64_t retry_times = 0;
  do {
    retry_times++;
    if (vector_idx_scheduler_timer_.inited()) {
      vector_idx_scheduler_timer_.wait();
    }
    if (!wait_finished) {
      ob_usleep(100 * 1000); // 100 ms
      if (retry_times % 100 == 0) { // every 10 s
        LOG_WARN_RET(OB_ERR_UNEXPECTED, "ls wait not finished.", K(ls_meta_), K(start_ts));
      }
    }
  } while (!wait_finished);
}

int ObLS::prepare_for_safe_destroy()
{
  return prepare_for_safe_destroy_();
}

// a class should implement prepare_for_safe_destroy() if it has
// resource which depend on ls. the resource here is refer to all kinds of
// memtables which are delayed GC in t3m due to performance problem.
int ObLS::prepare_for_safe_destroy_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(lock_table_.prepare_for_safe_destroy())) {
    LOG_WARN("fail to prepare_for_safe_destroy", K(ret));
  } else if (OB_FAIL(ls_tablet_svr_.prepare_for_safe_destroy())) {
    LOG_WARN("fail to prepare_for_safe_destroy", K(ret));
  } else if (OB_FAIL(tx_table_.prepare_for_safe_destroy())) {
    LOG_WARN("fail to prepare_for_safe_destroy", K(ret));
  }
  return ret;
}

void ObLS::destroy()
{
  // TODO: (yanyuan.cxf) destroy all the sub module.
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  int64_t start_ts = ObTimeUtility::current_time();
  {
    
  }
  ObTransService *txs_svr = (OB_NOT_NULL(share::g_mp) ? share::g_mp->trans_service() : nullptr);
  FLOG_INFO("ObLS destroy", K(this), K(*this), K(lbt()));
  if (running_state_.is_running()) {
    if (OB_TMP_FAIL(offline_(start_ts))) {
      LOG_WARN("offline a running ls failed", K(tmp_ret));
    }
  }
  if (OB_TMP_FAIL(stop_())) {
    LOG_WARN("ls stop failed.", K(tmp_ret));
  } else {
    wait_();
    if (OB_TMP_FAIL(prepare_for_safe_destroy_())) {
      LOG_WARN("failed to prepare for safe destroy", K(ret));
    }
  }
  unregister_from_service_();
  tx_table_.destroy();
  lock_table_.destroy();
  ls_tablet_svr_.destroy();
  keep_alive_ls_handler_.destroy();
  // may be not ininted, need bypass remove at txs_svr
  // test case may not init ls and ObTransService may have been destroyed before ls destroy.
  if (OB_ISNULL(txs_svr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tx service is null, may be memory leak", KP(txs_svr));
  } else if (OB_FAIL(txs_svr->remove_ls(false))) {
    // we may has remove it before.
    LOG_WARN("remove log stream from txs service failed", K(ret));
  }
  checkpoint_executor_.reset();
  log_handler_.destroy();
  ls_meta_.reset();
  ls_epoch_ = 0;
  ls_sync_tablet_seq_handler_.reset();
  ls_ddl_log_handler_.reset();
  tablet_gc_handler_.reset();
  tablet_empty_shell_handler_.reset();
  reserved_snapshot_mgr_.destroy();
  reserved_snapshot_clog_handler_.reset();
  medium_compaction_clog_handler_.reset();
  is_inited_ = false;
  
  need_delay_resource_recycle_ = false;
}

int ObLS::offline_tx_(const int64_t start_ts)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ls_tx_svr_.prepare_offline(start_ts))) {
    LOG_WARN("prepare offline ls tx service failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(tx_table_.prepare_offline())) {
    LOG_WARN("tx table prepare offline failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(ls_tx_svr_.offline())) {
    LOG_WARN("offline ls tx service failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(tx_table_.offline())) {
    LOG_WARN("tx table offline failed", K(ret), K(ls_meta_));
  }
  return ret;
}

int ObLS::offline_compaction_()
{
  int ret = OB_SUCCESS;
  if (FALSE_IT(ls_freezer_.offline())) {
  } else if (OB_FAIL(share::g_mp->tenant_tablet_scheduler()->check_ls_compaction_finish())) {
    LOG_WARN("check compaction finish failed", K(ret), K(ls_meta_));
  }
  return ret;
}

int ObLS::start_local_log_()
{
  int ret = OB_SUCCESS;
  palf::LSN end_lsn;
  bool is_done = false;
  bool is_clear = false;
  logservice::ObLogApplyService *apply_service = share::g_mp->log_service()->get_log_apply_service();
  logservice::ObLogReplayService *replay_service = share::g_mp->log_service()->get_log_replay_service();
  if (OB_FAIL(log_handler_.get_end_lsn(end_lsn))) {
    LOG_WARN("get local log end failed", K(ret));
  }
  while (OB_SUCC(ret) && !is_done) {
    if (OB_FAIL(replay_service->is_replay_done(end_lsn, is_done))) {
      LOG_WARN("check local replay failed", K(ret));
    } else if (!is_done) {
      ob_usleep(50 * 1000);
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(apply_service->start_local_append())) {
    LOG_WARN("start local apply failed", K(ret));
  } else if (OB_SUCC(ret) && OB_FAIL(replay_service->disable_local_replay())) {
    LOG_WARN("stop local replay failed", K(ret));
  }
  while (OB_SUCC(ret) && !is_clear) {
    if (OB_FAIL(replay_service->is_submit_task_clear(is_clear))) {
      LOG_WARN("check local replay tasks failed", K(ret));
    } else if (!is_clear) {
      ob_usleep(1000);
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(local_log_handler_set_.activate())) {
      LOG_WARN("activate local log handlers failed", K(ret));
    }
  }
  return ret;
}

int ObLS::stop_local_log_()
{
  int ret = OB_SUCCESS;
  bool is_done = false;
  palf::LSN end_lsn;
  logservice::ObLogApplyService *apply_service = share::g_mp->log_service()->get_log_apply_service();
  logservice::ObLogReplayService *replay_service = share::g_mp->log_service()->get_log_replay_service();
  if (OB_FAIL(apply_service->wait_append_sync())) {
    LOG_WARN("wait local append callbacks failed", K(ret));
  } else if (OB_FAIL(apply_service->stop_local_append())) {
    LOG_WARN("stop local apply failed", K(ret));
  }
  while (OB_SUCC(ret) && !is_done) {
    if (OB_FAIL(apply_service->is_apply_done(is_done, end_lsn))) {
      LOG_WARN("check local apply failed", K(ret));
    } else if (!is_done) {
      ob_usleep(5 * 1000);
    }
  }
  if (OB_SUCC(ret)) {
    local_log_handler_set_.deactivate();
    if (OB_FAIL(replay_service->enable_local_replay(end_lsn))) {
      LOG_WARN("stop local replay failed", K(ret));
    }
  }
  return ret;
}

int ObLS::offline_(const int64_t start_ts)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else if (running_state_.is_stopped()) {
    LOG_INFO("ls is stopped state, do nothing", K(ret), K(ls_meta_));
  } else if (OB_FAIL(running_state_.pre_offline())) {
    LOG_WARN("ls pre offline failed", K(ret), K(ls_meta_));
  } else if (FALSE_IT(update_state_seq_())) {
  } else if (OB_FAIL(offline_advance_epoch_())) {
  } else if (FALSE_IT(checkpoint_executor_.offline())) {
    LOG_WARN("checkpoint executor offline failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(stop_local_log_())) {
    LOG_WARN("failed to stop local log", K(ret));
  } else if (OB_FAIL(log_handler_.offline())) {
    LOG_WARN("failed to offline log", K(ret));
  // TODO: delete it if apply sequence
  // force set allocators frozen to reduce active tenant_memory
  } else if (OB_FAIL(ls_tablet_svr_.set_frozen_for_all_memtables())) {
    LOG_WARN("tablet service offline failed", K(ret), K(ls_meta_));
  }
  // make sure no new dag(tablet_gc_handler may generate new dag) is generated after offline offline_compaction_
  else if (OB_FAIL(tablet_gc_handler_.offline())) {
    LOG_WARN("tablet gc handler offline failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(offline_compaction_())) {
    LOG_WARN("compaction offline failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(ls_wrs_handler_.offline())) {
    LOG_WARN("weak read handler offline failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(ls_ddl_log_handler_.offline())) {
    LOG_WARN("ddl log handler offline failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(offline_tx_(start_ts))) {
    LOG_WARN("offline tx service failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(lock_table_.offline())) {
    LOG_WARN("lock table offline failed", K(ret), K(ls_meta_));
  // force release memtables created by tablet_freeze_with_rewrite_meta called during major
  } else if (OB_FAIL(ls_tablet_svr_.offline())) {
    LOG_WARN("tablet service offline failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(tablet_empty_shell_handler_.offline())) {
    LOG_WARN("tablet_empty_shell_handler  failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(running_state_.post_offline())) {
    LOG_WARN("ls post offline failed", KR(ret), K(ls_meta_));
  } else {
    update_state_seq_();
  }

  return ret;
}

int ObLS::offline()
{
  int ret = OB_SUCCESS;
  int64_t read_lock = 0;
  int64_t write_lock = LSLOCKALL;
  int64_t start_ts = ObTimeUtility::current_time();
  int64_t retry_times = 0;

  do {
    retry_times++;
    {
      ObLSLockGuard lock_myself(this, lock_, read_lock, write_lock);
      if (OB_FAIL(offline_(start_ts))) {
        LOG_WARN("ls offline failed", K(ret), K(ls_meta_));
      }
    }
    if (OB_EAGAIN == ret) {
      ob_usleep(100 * 1000); // 100 ms
      if (retry_times % 100 == 0) { // every 10 s
        LOG_WARN_RET(OB_ERR_TOO_MUCH_TIME, "ls offline use too much time.", K(ls_meta_), K(start_ts));
      }
    }
  } while (OB_EAGAIN == ret);
  FLOG_INFO("ls offline end", KR(ret));
  return ret;
}

int ObLS::online_tx_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ls_tx_svr_.online())) {
    LOG_WARN("ls tx service online failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(ls_tx_svr_.advance_commit_version(ls_meta_.get_clog_checkpoint_scn()))) {
    LOG_WARN("advance commit version failed", K(ret), K(ls_meta_.get_clog_checkpoint_scn()));
  } else if (OB_FAIL(tx_table_.online())) {
    LOG_WARN("tx table online failed", K(ret), K(ls_meta_));
  }
  return ret;
}

int ObLS::online_compaction_()
{
  int ret = OB_SUCCESS;
  ls_freezer_.online();
  return ret;
}

int ObLS::offline_advance_epoch_()
{
  int ret = OB_SUCCESS;
  if (ATOMIC_LOAD(&switch_epoch_) & 1) {
    ATOMIC_AAF(&switch_epoch_, 1);
    LOG_INFO("offline advance epoch", K(ret), K(ls_meta_), K_(switch_epoch));
  } else {
    LOG_INFO("offline not advance epoch(maybe repeat call)", K(ret), K(ls_meta_), K_(switch_epoch));
  }
  return ret;
}

int ObLS::online_advance_epoch_()
{
  int ret = OB_SUCCESS;
  if (ATOMIC_LOAD(&switch_epoch_) & 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("switch_epoch_ is odd, means online already", K(ret));
  } else {
    ATOMIC_AAF(&switch_epoch_, 1);
    LOG_INFO("online advance epoch", K(ret), K(ls_meta_), K_(switch_epoch));
  }
  return ret;
}

int ObLS::register_common_service()
{
  int ret = OB_SUCCESS;
  REGISTER_TO_LOGSERVICE(TRANS_SERVICE_LOG_BASE_TYPE, &ls_tx_svr_);
  REGISTER_TO_LOGSERVICE(STORAGE_SCHEMA_LOG_BASE_TYPE, &ls_tablet_svr_);
  REGISTER_TO_LOGSERVICE(TABLET_SEQ_SYNC_LOG_BASE_TYPE, &ls_sync_tablet_seq_handler_);
  REGISTER_TO_LOGSERVICE(DDL_LOG_BASE_TYPE, &ls_ddl_log_handler_);
  REGISTER_TO_LOGSERVICE(KEEP_ALIVE_LOG_BASE_TYPE, &keep_alive_ls_handler_);
  REGISTER_TO_LOGSERVICE(RESERVED_SNAPSHOT_LOG_BASE_TYPE, &reserved_snapshot_clog_handler_);
  REGISTER_TO_LOGSERVICE(MEDIUM_COMPACTION_LOG_BASE_TYPE, &medium_compaction_clog_handler_);

  REGISTER_REPLAY_CHECKPOINT_HANDLER(TIMESTAMP_LOG_BASE_TYPE, share::g_mp->timestamp_service());
  REGISTER_REPLAY_CHECKPOINT_HANDLER(TRANS_ID_LOG_BASE_TYPE, share::g_mp->trans_id_service());
  REGISTER_TO_LOGSERVICE(MAJOR_FREEZE_LOG_BASE_TYPE, share::g_mp->primary_major_freeze_service());
  return ret;
}

int ObLS::register_local_services_()
{
  int ret = OB_SUCCESS;

  ObIngressBWAllocService *ingress_service = GCTX.net_frame_->get_ingress_service();
  REGISTER_TO_LOGSERVICE(NET_ENDPOINT_INGRESS_LOG_BASE_TYPE, ingress_service);
  REGISTER_TO_LOGSERVICE(DBMS_SCHEDULER_LOG_BASE_TYPE, share::g_mp->dbms_sched_service());
  REGISTER_TO_LOGSERVICE(SYS_DDL_SCHEDULER_LOG_BASE_TYPE, share::g_mp->ddl_scheduler());
  REGISTER_TO_LOGSERVICE(DDL_SERVICE_LAUNCHER_LOG_BASE_TYPE, share::g_mp->ddl_service_launcher());
  REGISTER_TO_LOGSERVICE(SYS_TENANT_LOAD_SYS_PACKAGE_SERVICE_LOG_BASE_TYPE, share::g_mp->sys_tenant_load_sys_package_service());
  // ObInternalTableChangeNotifier only needs local lifecycle callbacks.
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(local_log_handler_set_.register_handler(
      INTERNAL_TABLE_NOTIFIER_LOG_BASE_TYPE,
      &share::ObInternalTableChangeNotifier::get_instance()))) {
    LOG_WARN("local_log_handler_set_ register notifier failed", K(ret));
  }
  REGISTER_TO_LOGSERVICE(VEC_INDEX_SERVICE_LOG_BASE_TYPE, share::g_mp->plugin_vector_index_service());
  if (OB_SUCC(ret)) {
    // table_api removed from build: TTL_LOG_BASE_TYPE handler is gone, only the
    // vector index scheduler (formerly hosted by the tablet ttl mgr) is kept.
    if (OB_FAIL(init_vector_idx_scheduler_())) {
      LOG_WARN("fail to init vector index scheduler", KR(ret));
    } else {
      REGISTER_TO_LOGSERVICE(VEC_INDEX_LOG_BASE_TYPE, &vector_idx_scheduler_);
    }
  }

  return ret;
}

int ObLS::register_to_service_()
{
  int ret = OB_SUCCESS;
  
  if (OB_FAIL(register_common_service())) {
    LOG_WARN("common tenant register failed", K(ret));
  } else if (OB_FAIL(register_local_services_())) {
    LOG_WARN("register local services failed", K(ret));
  }

  return ret;
}

// table_api removed from build: the following helpers preserve the vector index
// scheduler lifecycle that used to be managed by table::ObTenantTabletTTLMgr.
int ObLS::init_vector_idx_scheduler_()
{
  int ret = OB_SUCCESS;
  if (vector_idx_scheduler_timer_.inited()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("vector index scheduler init twice", KR(ret));
  } else if (OB_FAIL(vector_idx_scheduler_timer_.init(
      "TTLTabletMgr", common::ObMemAttr("TTLTabletMgr")))) {
    LOG_WARN("fail to init vector index scheduler timer", KR(ret));
  } else if (OB_FAIL(vector_idx_scheduler_.init(this, vector_idx_scheduler_timer_))) {
    LOG_WARN("fail to init vector idx scheduler", KR(ret));
  }
  return ret;
}

void ObLS::stop_vector_idx_scheduler_()
{
  if (vector_idx_scheduler_timer_.inited()) {
    vector_idx_scheduler_timer_.stop();
    vector_idx_scheduler_.stop();
  }
}

void ObLS::destroy_vector_idx_scheduler_()
{
  if (vector_idx_scheduler_timer_.inited()) {
    vector_idx_scheduler_timer_.wait();
    vector_idx_scheduler_timer_.destroy();
    vector_idx_scheduler_.destroy();
  }
}

void ObLS::unregister_common_service_()
{
  UNREGISTER_FROM_LOGSERVICE(TRANS_SERVICE_LOG_BASE_TYPE, &ls_tx_svr_);
  UNREGISTER_FROM_LOGSERVICE(STORAGE_SCHEMA_LOG_BASE_TYPE, &ls_tablet_svr_);
  UNREGISTER_FROM_LOGSERVICE(TABLET_SEQ_SYNC_LOG_BASE_TYPE, &ls_sync_tablet_seq_handler_);
  UNREGISTER_FROM_LOGSERVICE(DDL_LOG_BASE_TYPE, &ls_ddl_log_handler_);
  UNREGISTER_FROM_LOGSERVICE(KEEP_ALIVE_LOG_BASE_TYPE, &keep_alive_ls_handler_);
  UNREGISTER_FROM_LOGSERVICE(RESERVED_SNAPSHOT_LOG_BASE_TYPE, &reserved_snapshot_clog_handler_);
  UNREGISTER_FROM_LOGSERVICE(MEDIUM_COMPACTION_LOG_BASE_TYPE, &medium_compaction_clog_handler_);
  UNREGISTER_REPLAY_CHECKPOINT_HANDLER(TIMESTAMP_LOG_BASE_TYPE);
  UNREGISTER_REPLAY_CHECKPOINT_HANDLER(TRANS_ID_LOG_BASE_TYPE);
  UNREGISTER_FROM_LOGSERVICE(MAJOR_FREEZE_LOG_BASE_TYPE, nullptr);
}

void ObLS::unregister_local_services_()
{
  UNREGISTER_FROM_LOGSERVICE(NET_ENDPOINT_INGRESS_LOG_BASE_TYPE, nullptr);
  UNREGISTER_FROM_LOGSERVICE(DBMS_SCHEDULER_LOG_BASE_TYPE, share::g_mp->dbms_sched_service());
  UNREGISTER_FROM_LOGSERVICE(SYS_DDL_SCHEDULER_LOG_BASE_TYPE, share::g_mp->ddl_scheduler());
  UNREGISTER_FROM_LOGSERVICE(DDL_SERVICE_LAUNCHER_LOG_BASE_TYPE, share::g_mp->ddl_service_launcher());
  UNREGISTER_FROM_LOGSERVICE(SYS_TENANT_LOAD_SYS_PACKAGE_SERVICE_LOG_BASE_TYPE, share::g_mp->sys_tenant_load_sys_package_service());
  local_log_handler_set_.unregister_handler(INTERNAL_TABLE_NOTIFIER_LOG_BASE_TYPE);
  UNREGISTER_FROM_LOGSERVICE(VEC_INDEX_SERVICE_LOG_BASE_TYPE, share::g_mp->plugin_vector_index_service());
  UNREGISTER_FROM_LOGSERVICE(VEC_INDEX_LOG_BASE_TYPE, &vector_idx_scheduler_);
  destroy_vector_idx_scheduler_();
}

void ObLS::unregister_from_service_()
{
  unregister_common_service_();
  unregister_local_services_();
}

int ObLS::online()
{
  int64_t read_lock = 0;
  int64_t write_lock = LSLOCKALL;
  ObLSLockGuard lock_myself(this, lock_, read_lock, write_lock);
  return online_without_lock();
}

int ObLS::online_without_lock()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else if (running_state_.is_running()) {
    LOG_INFO("ls is running state, do nothing", K(ret));
  } else if (OB_FAIL(ls_tablet_svr_.online())) {
    LOG_WARN("tablet service online failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(lock_table_.online())) {
    LOG_WARN("lock table online failed", K(ret), K(ls_meta_));
    // TODO: weixiaoxian remove this start
  } else if (OB_FAIL(online_tx_())) {
    LOG_WARN("ls tx online failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(ls_ddl_log_handler_.online())) {
    LOG_WARN("ddl log handler online failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(log_handler_.online(ls_meta_.get_clog_base_lsn(),
                                         ls_meta_.get_clog_checkpoint_scn()))) {
    LOG_WARN("failed to online log", K(ret));
  } else if (OB_FAIL(start_local_log_())) {
    LOG_WARN("failed to start local log", K(ret));
  } else if (OB_FAIL(ls_wrs_handler_.online())) {
    LOG_WARN("weak read handler online failed", K(ret), K(ls_meta_));
  } else if (OB_FAIL(online_compaction_())) {
    LOG_WARN("compaction online failed", K(ret), K(ls_meta_));
  } else if (FALSE_IT(checkpoint_executor_.online())) {
  } else if (FALSE_IT(tablet_gc_handler_.online())) {
  } else if (FALSE_IT(tablet_empty_shell_handler_.online())) {
  } else if (OB_FAIL(online_advance_epoch_())) {
  } else if (OB_FAIL(running_state_.online())) {
    LOG_WARN("ls online failed", KR(ret), K(ls_meta_));
  } else {
    update_state_seq_();
  }

  FLOG_INFO("ls online end", KR(ret));
  return ret;
}

int ObLS::set_ls_meta(const ObLSMeta &ls_meta)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else {
    ls_meta_ = ls_meta;
    ObAllIDMeta all_id_meta;
    if (OB_FAIL(ls_meta_.get_all_id_meta(all_id_meta))) {
      LOG_WARN("get all id meta failed", K(ret), K(ls_meta_));
    } else if (OB_FAIL(ObIDService::update_id_service(all_id_meta))) {
      LOG_WARN("update id service fail", K(ret), K(all_id_meta), K(*this));
    }
  }
  return ret;
}
int ObLS::set_ls_epoch(const int64_t ls_epoch)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else {
    ls_epoch_ = ls_epoch;
  }
  return ret;
}

int ObLS::get_ls_meta(ObLSMeta &ls_meta) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else {
    ls_meta = ls_meta_;
  }
  return ret;
}

int ObLS::try_sync_reserved_snapshot(
    const int64_t new_reserved_snapshot,
    const bool update_flag)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else if (!is_stopped()) {
    ret = reserved_snapshot_mgr_.try_sync_reserved_snapshot(new_reserved_snapshot, update_flag);
  }
  return ret;
}

int ObLS::get_ls_info(ObLSVTInfo &ls_info)
{
  int ret = OB_SUCCESS;
  bool tx_blocked = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else if (OB_FAIL(ls_tx_svr_.check_tx_blocked(tx_blocked))) {
    LOG_WARN("check tx ls state error", K(ret),KPC(this));
  } else {
    // The readable point of the primary tenant is weak read ts,
    // and the readable point of the standby tenant is readable scn
    ls_info.weak_read_scn_ = ls_wrs_handler_.get_ls_weak_read_ts();
    if (OB_SUCC(ret)) {
      ls_info.tablet_count_ = ls_tablet_svr_.get_tablet_count();
      ls_info.checkpoint_scn_ = ls_meta_.get_clog_checkpoint_scn();
      ls_info.checkpoint_lsn_ = ls_meta_.get_clog_base_lsn().val_;
      ls_info.tablet_change_checkpoint_scn_ = ls_meta_.get_tablet_change_checkpoint_scn();
      ls_info.tx_blocked_ = tx_blocked;
      if (tx_blocked) {
        TRANS_LOG(INFO, "current ls is blocked", K(ls_info));
      }
    }
  }
  return ret;
}

int ObLS::ObLSInnerTabletIDIter::get_next(common::ObTabletID  &tablet_id)
{
  int ret = OB_SUCCESS;
  if (pos_ >= TOTAL_INNER_TABLET_NUM) {
    ret = OB_ITER_END;
  } else {
    tablet_id = INNER_TABLET_ID_LIST[pos_++];
  }
  return ret;
}

ObLS::RDLockGuard::RDLockGuard(RWLock &lock, const int64_t abs_timeout_us)
  : lock_(lock), ret_(OB_SUCCESS), start_ts_(0)
{
  ObTimeGuard tg("ObLS::rwlock", LOCK_CONFLICT_WARN_TIME);
  if (OB_UNLIKELY(OB_SUCCESS != (ret_ = lock_.rdlock(ObLatchIds::LS_LOCK,
                                                     abs_timeout_us)))) {
    STORAGE_LOG_RET(WARN, ret_, "Fail to read lock, ", K_(ret));
  } else {
    start_ts_ = ObTimeUtility::current_time();
  }
}

ObLS::RDLockGuard::~RDLockGuard()
{
  if (OB_LIKELY(OB_SUCCESS == ret_)) {
    if (OB_UNLIKELY(OB_SUCCESS != (ret_ = lock_.unlock()))) {
      STORAGE_LOG_RET(WARN, ret_, "Fail to unlock, ", K_(ret));
    }
  }
  const int64_t end_ts = ObTimeUtility::current_time();
  if (end_ts - start_ts_ > 5 * 1000 * 1000) {
    STORAGE_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "ls lock cost too much time", K_(start_ts),
                    "cost_us", end_ts - start_ts_, K(lbt()));
  }
  start_ts_ = INT64_MAX;
}

ObLS::WRLockGuard::WRLockGuard(RWLock &lock, const int64_t abs_timeout_us)
  : lock_(lock), ret_(OB_SUCCESS), start_ts_(0)
{
  ObTimeGuard tg("ObLS::rwlock", LOCK_CONFLICT_WARN_TIME);
  if (OB_UNLIKELY(OB_SUCCESS != (ret_ = lock_.wrlock(ObLatchIds::LS_LOCK,
                                                     abs_timeout_us)))) {
    STORAGE_LOG_RET(WARN, ret_, "Fail to read lock, ", K_(ret));
  } else {
    start_ts_ = ObTimeUtility::current_time();
  }
}

ObLS::WRLockGuard::~WRLockGuard()
{
  if (OB_LIKELY(OB_SUCCESS == ret_)) {
    if (OB_UNLIKELY(OB_SUCCESS != (ret_ = lock_.unlock()))) {
      STORAGE_LOG_RET(WARN, ret_, "Fail to unlock, ", K_(ret));
    }
  }
  const int64_t end_ts = ObTimeUtility::current_time();
  if (end_ts - start_ts_ > 5 * 1000 * 1000) {
    STORAGE_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "ls lock cost too much time", K_(start_ts),
                    "cost_us", end_ts - start_ts_, K(lbt()));
  }
  start_ts_ = INT64_MAX;
}

int ObLS::update_tablet_table_store(
    const ObTabletID &tablet_id,
    const ObUpdateTableStoreParam &param,
    ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  RDLockGuard guard(meta_rwlock_);

  return update_tablet_table_store_without_lock_(tablet_id, param, handle);
}

int ObLS::update_tablet_table_store_without_lock_(
    const ObTabletID &tablet_id,
    const ObUpdateTableStoreParam &param,
    ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || !param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("update tablet table store get invalid argument", K(ret), K(tablet_id), K(param));
  } else if (OB_FAIL(ls_tablet_svr_.update_tablet_table_store(tablet_id, param, handle))) {
    LOG_WARN("failed to update tablet table store", K(ret), K(tablet_id), K(param));
  }
  return ret;
}

int ObLS::update_tablet_table_store(
    const ObTabletHandle &old_tablet_handle,
    const ObIArray<storage::ObITable *> &tables)
{
  int ret = OB_SUCCESS;
  RDLockGuard guard(meta_rwlock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls hasn't been inited", K(ret));
  } else if (OB_UNLIKELY(!old_tablet_handle.is_valid() || 0 == tables.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(old_tablet_handle), K(tables));
  } else {
    const common::ObTabletID &tablet_id = old_tablet_handle.get_obj()->get_tablet_meta().tablet_id_;
    if (OB_FAIL(ls_tablet_svr_.update_tablet_table_store(old_tablet_handle, tables))) {
      LOG_WARN("fail to replace small sstables in the tablet", K(ret), K(tablet_id), K(old_tablet_handle), K(tables));
    }
  }
  return ret;
}

int ObLS::build_tablet_with_batch_tables(
    const ObTabletID &tablet_id,
    const ObBatchUpdateTableStoreParam &param)
{
  int ret = OB_SUCCESS;
  const int64_t MAX_RETRY_NUM = 3;
  const int64_t SLEEP_TS = 100 * 1000L; //100ms;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else {
    ret = OB_EAGAIN;
    int64_t retry_count = 0;
    while (OB_EAGAIN == ret && retry_count < MAX_RETRY_NUM) {
      if (OB_FAIL(inner_build_tablet_with_batch_tables_(tablet_id, param))) {
        if (OB_EAGAIN != ret) {
          LOG_ERROR("failed to build tablet with batch tables", KR(ret), K(tablet_id));
        } else {
          ob_usleep(SLEEP_TS);
        }
      }
      ++retry_count;
    }
  }
  return ret;
}

int ObLS::inner_build_tablet_with_batch_tables_(
    const ObTabletID &tablet_id,
    const ObBatchUpdateTableStoreParam &param)
{
  int ret = OB_SUCCESS;
  RDLockGuard guard(meta_rwlock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else if (!tablet_id.is_valid() || !param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build ha tablet new table store get invalid argument", K(ret), K(tablet_id), K(param));
  } else if (OB_FAIL(ls_tablet_svr_.build_tablet_with_batch_tables(tablet_id, param))) {
    LOG_WARN("failed to update tablet table store", K(ret), K(tablet_id), K(param));
  }
  return ret;
}

int ObLS::build_new_tablet_from_mds_table(
    compaction::ObTabletMergeCtx &ctx,
    const common::ObTabletID &tablet_id,
    const ObTableHandleV2 &mds_mini_sstable_handle,
    const share::SCN &flush_scn,
    ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  RDLockGuard guard(meta_rwlock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || !flush_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id), K(flush_scn));
  } else if (OB_FAIL(ls_tablet_svr_.build_new_tablet_from_mds_table(ctx, tablet_id, mds_mini_sstable_handle, flush_scn, handle))) {
    LOG_WARN("failed to build new tablet from mds table", K(ret), K(tablet_id), K(flush_scn));
  }
  return ret;
}

int ObLS::finish_storage_meta_replay()
{
  int ret = OB_SUCCESS;
  int64_t read_lock = 0;
  int64_t write_lock = LSLOCKALL;
  const int64_t start_ts = ObTimeUtility::current_time();
  ObLSLockGuard lock_myself(this, lock_, read_lock, write_lock);

  if (OB_FAIL(running_state_.create_finish())) {
    LOG_WARN("create finish failed", KR(ret), K(ls_meta_));
  } else {
    // after slog replayed, the ls must be offlined state.
    update_state_seq_();
  }
  return ret;
}

int ObLS::replay_get_tablet_no_check(
    const common::ObTabletID &tablet_id,
    const SCN &scn,
    const bool replay_allow_tablet_not_exist,
    ObTabletHandle &handle) const
{
  int ret = OB_SUCCESS;
  const ObTabletMapKey key(tablet_id);
  const SCN tablet_change_checkpoint_scn = ls_meta_.get_tablet_change_checkpoint_scn();
  SCN max_scn;
  ObTabletHandle tablet_handle;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", KR(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || !scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(tablet_id), K(scn));
  } else if (OB_FAIL(ObTabletCreateDeleteHelper::get_tablet(key, tablet_handle))) {
    if (OB_TABLET_NOT_EXIST != ret) {
      LOG_WARN("failed to get tablet", K(ret), K(key));
    } else if (scn <= tablet_change_checkpoint_scn) {
      ret = OB_OBSOLETE_CLOG_NEED_SKIP;
      LOG_WARN("tablet already gc", K(ret), K(key), K(scn), K(tablet_change_checkpoint_scn));
    } else if (OB_FAIL(share::g_mp->log_service()->get_log_replay_service()->get_max_replayed_scn(max_scn))) {
      LOG_WARN("failed to get_max_replayed_scn", KR(ret), K_(ls_meta), K(scn), K(tablet_id));
    }
    // double check for this scenario:
    // 1. get_tablet return OB_TABLET_NOT_EXIST
    // 2. create tablet
    // 3. get_max_replayed_scn > scn
    else if (OB_FAIL(ObTabletCreateDeleteHelper::get_tablet(key, tablet_handle))) {
      if (OB_TABLET_NOT_EXIST != ret) {
        LOG_WARN("failed to get tablet", K(ret), K(key));
      } else if (!max_scn.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("max_scn is invalid", KR(ret), K(key), K(scn), K(tablet_change_checkpoint_scn));
      } else if (scn > SCN::scn_inc(max_scn) || !replay_allow_tablet_not_exist) {
        ret = OB_EAGAIN;
        LOG_INFO("tablet does not exist, but need retry", KR(ret), K(key), K(scn),
            K(tablet_change_checkpoint_scn), K(max_scn), K(replay_allow_tablet_not_exist));
      } else {
        ret = OB_OBSOLETE_CLOG_NEED_SKIP;
        LOG_INFO("tablet already gc, but scn is more than tablet_change_checkpoint_scn", KR(ret),
            K(key), K(scn), K(tablet_change_checkpoint_scn), K(max_scn));
      }
    }
  }

  if (OB_SUCC(ret)) {
    handle = tablet_handle;
  }

  return ret;
}

int ObLS::replay_get_tablet(
    const common::ObTabletID &tablet_id,
    const SCN &scn,
    const bool is_update_mds_table,
    ObTabletHandle &handle) const
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  ObTabletCreateDeleteMdsUserData data;
  const bool replay_allow_tablet_not_exist = true;
  mds::MdsWriter writer;// will be removed later
  mds::TwoPhaseCommitState trans_stat;// will be removed later
  share::SCN trans_version;// will be removed later

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", KR(ret));
  } else if (OB_FAIL(replay_get_tablet_no_check(tablet_id, scn, replay_allow_tablet_not_exist, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id), K(scn));
  } else if (tablet_id.is_ls_inner_tablet()) {
    // do nothing
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), KP(tablet), K(tablet_id), K(scn));
  } else if (tablet->is_empty_shell()) {
    ObTabletStatus::Status tablet_status = ObTabletStatus::MAX;
    if (OB_FAIL(tablet->get_latest(data, writer, trans_stat, trans_version))) {
      LOG_WARN("failed to get latest tablet status", K(ret), K(tablet_id));
    } else if (OB_UNLIKELY(mds::TwoPhaseCommitState::ON_COMMIT != trans_stat)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet is empty shell but user data is uncommitted, unexpected", K(ret), KPC(tablet));
    } else if (OB_UNLIKELY(!data.tablet_status_.is_deleted_for_gc())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet is empty shell but user data is unexpected", K(ret), K(data), KPC(tablet));
    } else {
      ret = OB_OBSOLETE_CLOG_NEED_SKIP;
      LOG_INFO("tablet is already deleted, need skip", KR(ret), K(tablet_id), K(scn));
    }
  } else if ((!is_update_mds_table && scn > tablet->get_clog_checkpoint_scn())
      || (is_update_mds_table && scn > tablet->get_mds_checkpoint_scn())) {
    if (OB_FAIL(tablet->get_latest(data, writer, trans_stat, trans_version))) {
      if (OB_EMPTY_RESULT == ret) {
        ret = OB_EAGAIN;
        LOG_INFO("read empty mds data, should retry", KR(ret), K(tablet_id), K(scn));
      } else {
        LOG_WARN("failed to get latest tablet status", K(ret), KPC(tablet));
      }
    } else if (mds::TwoPhaseCommitState::ON_COMMIT != trans_stat) {
      if (ObTabletStatus::NORMAL == data.tablet_status_
          && data.create_commit_version_ == ObTransVersion::INVALID_TRANS_VERSION) {
        ret = OB_EAGAIN;
        LOG_INFO("latest transaction has not committed yet, should retry", KR(ret), K(tablet_id),
            K(scn), "clog_checkpoint_scn", tablet->get_clog_checkpoint_scn(), K(data));
      }
    }
  }

  if (OB_SUCC(ret)) {
    handle = tablet_handle;
  }

  return ret;
}

int ObLS::logstream_freeze(const bool is_sync,
                           const int64_t input_abs_timeout_ts,
                           const ObFreezeSourceFlag source)
{
  int ret = OB_SUCCESS;

  if (!is_valid_freeze_source(source)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "unexpected freeze source", K(source));
  } else if (is_sync) {
    const int64_t abs_timeout_ts = (0 == input_abs_timeout_ts)
                                       ? ObClockGenerator::getClock() + ObFreezer::SYNC_FREEZE_DEFAULT_RETRY_TIME
                                       : input_abs_timeout_ts;
    ret = logstream_freeze_task(abs_timeout_ts);
  } else {
    const bool is_ls_freeze = true;
    (void)ls_freezer_.submit_an_async_freeze_task(is_ls_freeze);
  }

  if (OB_SUCC(ret)) {
    share::g_mp->tenant_freezer()->record_freezer_source_event(source);
  }

  return ret;
}

int ObLS::logstream_freeze_task(const int64_t abs_timeout_ts)
{
  int ret = OB_SUCCESS;
  const int64_t start_time = ObClockGenerator::getClock();
  {
    int64_t read_lock = LSLOCKALL;
    int64_t write_lock = 0;
    ObLSLockGuard lock_myself(this, lock_, read_lock, write_lock, abs_timeout_ts);
    if (!lock_myself.locked()) {
      ret = OB_TIMEOUT;
      STORAGE_LOG(WARN, "lock ls failed, please retry later", K(ret), K(ls_meta_));
    } else if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      STORAGE_LOG(WARN, "ls is not inited", K(ret));
    } else if (OB_UNLIKELY(is_offline())) {
      ret = OB_LS_OFFLINE;
      STORAGE_LOG(WARN, "offline ls not allowed freeze", K(ret), K_(ls_meta));
    } else if (OB_FAIL(ls_freezer_.logstream_freeze())) {
      STORAGE_LOG(WARN, "logstream freeze failed", K(ret), K_(ls_meta));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ls_freezer_.wait_ls_freeze_finish())) {
    STORAGE_LOG(WARN, "wait ls freeze finish failed", KR(ret));
  }

  const int64_t ls_freeze_task_spend_time = ObClockGenerator::getClock() - start_time;
  STORAGE_LOG(INFO,
              "[Freezer] logstream freeze task finish",
              K(ret),
              K(ls_freeze_task_spend_time),
              KTIME(abs_timeout_ts));
  return ret;
}

/**
 * @brief for single tablet freeze
 *
 */
int ObLS::tablet_freeze(const ObTabletID &tablet_id,
                        const bool is_sync,
                        const int64_t input_abs_timeout_ts,
                        const bool need_rewrite_meta,
                        const ObFreezeSourceFlag source)
{
  int ret = OB_SUCCESS;

  if (!is_valid_freeze_source(source)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "unexpected freeze source", K(source));
  } else if (tablet_id.is_ls_inner_tablet()) {
    ret = ls_freezer_.ls_inner_tablet_freeze(tablet_id);
  } else {
    ObSEArray<ObTabletID, 1> tablet_ids;
    if (OB_FAIL(tablet_ids.push_back(tablet_id))) {
      STORAGE_LOG(WARN, "push back tablet id failed", KR(ret), K(tablet_id));
    } else {
      ret = tablet_freeze(tablet_ids,
                          is_sync,
                          input_abs_timeout_ts,
                          need_rewrite_meta,
                          source);
    }
  }
  return ret;
}

int ObLS::tablet_freeze(const ObIArray<ObTabletID> &tablet_ids,
                        const bool is_sync,
                        const int64_t input_abs_timeout_ts,
                        const bool need_rewrite_meta,
                        const ObFreezeSourceFlag source)
{
  int ret = OB_SUCCESS;
  STORAGE_LOG(
      DEBUG, "start tablet freeze", K(tablet_ids), K(is_sync), KTIME(input_abs_timeout_ts), K(need_rewrite_meta));
  int64_t freeze_epoch = ATOMIC_LOAD(&switch_epoch_);

  if (!is_valid_freeze_source(source)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "unexpected freeze source", K(source));
  } else if (need_rewrite_meta && (!is_sync)) {
    ret = OB_NOT_SUPPORTED;
    STORAGE_LOG(ERROR,
                "tablet freeze for rewrite meta must be sync freeze ",
                KR(ret),
                K(need_rewrite_meta),
                K(is_sync),
                K(tablet_ids));
  } else if (is_sync) {
    const int64_t start_time = ObClockGenerator::getClock();
    const int64_t abs_timeout_ts =
        (0 == input_abs_timeout_ts) ? start_time + ObFreezer::SYNC_FREEZE_DEFAULT_RETRY_TIME : input_abs_timeout_ts;
    bool is_retry_code = false;
    bool is_not_timeout = false;
    do {
      ret = tablet_freeze_task(tablet_ids, need_rewrite_meta, is_sync, abs_timeout_ts, freeze_epoch);
      const int64_t current_time = ObClockGenerator::getClock();
      if (OB_FAIL(ret) &&
          current_time - start_time > 10LL * 1000LL * 1000LL &&
          REACH_TIME_INTERVAL(5LL * 1000LL * 1000LL)) {
        STORAGE_LOG(WARN, "sync tablet freeze for long time", KR(ret), KTIME(start_time), KTIME(abs_timeout_ts));
      }

      is_retry_code = OB_EAGAIN == ret || OB_MINOR_FREEZE_NOT_ALLOW == ret || OB_ALLOCATE_MEMORY_FAILED == ret;
      is_not_timeout = current_time < abs_timeout_ts;
    } while (is_retry_code && is_not_timeout);
  } else {
    //Async tablet freeze. Must record tablet ids before submit task
    const bool is_ls_freeze = false;
    (void)record_async_freeze_tablets_(tablet_ids, freeze_epoch);
    (void)ls_freezer_.submit_an_async_freeze_task(is_ls_freeze);
  }

  if (OB_SUCC(ret)) {
    share::g_mp->tenant_freezer()->record_freezer_source_event(source);
  }

  return ret;
}

int ObLS::tablet_freeze_task(const ObIArray<ObTabletID> &tablet_ids,
                             const bool need_rewrite_meta,
                             const bool is_sync,
                             const int64_t abs_timeout_ts,
                             const int64_t freeze_epoch)
{
  int ret = OB_SUCCESS;

  bool print_warn_log = false;
  const int64_t start_time = ObClockGenerator::getClock();
  ObSEArray<ObTableHandleV2, 32> frozen_memtable_handles;
  ObSEArray<ObTabletID, 32> freeze_failed_tablets;
  {
    int64_t read_lock = LSLOCKALL;
    int64_t write_lock = 0;
    ObLSLockGuard lock_myself(this, lock_, read_lock, write_lock, abs_timeout_ts);
    if (!lock_myself.locked()) {
      ret = OB_TIMEOUT;
      STORAGE_LOG(WARN, "lock failed, please retry later", K(ret), K(ls_meta_));
    } else if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      STORAGE_LOG(WARN, "ls is not inited", K(ret));
    } else if (OB_UNLIKELY(is_offline())) {
      ret = OB_LS_OFFLINE;
      STORAGE_LOG(WARN, "ls has offlined", K(ret), K_(ls_meta));
    } else if (OB_FAIL(ls_freezer_.tablet_freeze(
                   tablet_ids, need_rewrite_meta, frozen_memtable_handles, freeze_failed_tablets))) {
      if (REACH_TIME_INTERVAL(1LL * 1000LL * 1000LL)) {
        STORAGE_LOG(WARN, "tablet freeze failed", KR(ret), K(tablet_ids), K(freeze_failed_tablets));
      }
    }
  }

  // ATTENTION : if frozen memtable handles not empty, must wait freeze finish
  if (!frozen_memtable_handles.empty()) {
    (void)ls_freezer_.wait_tablet_freeze_finish(frozen_memtable_handles, freeze_failed_tablets);
  }

  // handle freeze failed tablets
  if (!freeze_failed_tablets.empty()) {
    if (OB_SUCC(ret)) {
      // some tablet freeze failed need retry
      ret = OB_EAGAIN;
    }
    if (!is_sync) {
      (void)record_async_freeze_tablets_(freeze_failed_tablets, freeze_epoch);
    }
  }

  if (OB_SUCC(ret)) {
    const int64_t tablet_freeze_task_spend_time = ObClockGenerator::getClock() - start_time;
    STORAGE_LOG(INFO,
                "[Freezer] tablet freeze task success",
                K(ret),
                K(need_rewrite_meta),
                K(is_sync),
                K(tablet_freeze_task_spend_time),
                KTIME(abs_timeout_ts));
  }
  return ret;
}

void ObLS::record_async_freeze_tablets_(const ObIArray<ObTabletID> &tablet_ids, const int64_t epoch)
{
  for (int64_t i = 0; i < tablet_ids.count(); i++) {
    AsyncFreezeTabletInfo tablet_info;
    tablet_info.tablet_id_ = tablet_ids.at(i);
    tablet_info.epoch_ = epoch;
    (void)ls_freezer_.record_async_freeze_tablet(tablet_info);
  }
}

int ObLS::advance_checkpoint_by_flush(SCN recycle_scn,
                                      const int64_t abs_timeout_ts,
                                      const bool is_tenant_freeze,
                                      const ObFreezeSourceFlag source)
{
  int ret = OB_SUCCESS;
  if (is_tenant_freeze) {
    ObDataCheckpoint::set_tenant_freeze();
    LOG_INFO("set tenant_freeze");
  }
  ObDataCheckpoint::set_freeze_source(source);
  ret = checkpoint_executor_.advance_checkpoint_by_flush(recycle_scn);
  ObDataCheckpoint::reset_freeze_source();
  ObDataCheckpoint::reset_tenant_freeze();
  return ret;
}

int ObLS::flush_to_recycle_clog()
{
  int ret = OB_SUCCESS;
  int64_t read_lock = LSLOCKALL;
  int64_t write_lock = 0;

  ObLSLockGuard lock_myself(this, lock_, read_lock, write_lock);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else if (OB_UNLIKELY(is_offline())) {
    ret = OB_MINOR_FREEZE_NOT_ALLOW;
    LOG_WARN("offline ls not allowed freeze", K(ret), K_(ls_meta));
  } else if (FALSE_IT(ObDataCheckpoint::set_freeze_source(ObFreezeSourceFlag::CLOG_CHECKPOINT))) {
  } else if (OB_FAIL(checkpoint_executor_.advance_checkpoint_by_flush(SCN::invalid_scn() /*recycle_scn*/))) {
    STORAGE_LOG(WARN, "advance_checkpoint_by_flush failed", KR(ret));
  }
  ObDataCheckpoint::reset_freeze_source();
  return ret;
}

int ObLS::check_ls_need_online(bool &need_online)
{
  int ret = OB_SUCCESS;
  need_online = true;
  if (OB_FAIL(ls_meta_.check_ls_need_online(need_online))) {
    LOG_WARN("fail to check ls need online", K(ret));
  }
  return ret;
}

int ObLS::disable_replay()
{
  int ret = OB_SUCCESS;
  int64_t read_lock = LSLOCKLSSTATE;
  int64_t write_lock = LSLOCKLOGSTATE;
  ObLSLockGuard lock_myself(this, lock_, read_lock, write_lock);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else if (OB_FAIL(log_handler_.disable_replay())) {
    LOG_WARN("disable replay failed", K(ret), K_(ls_meta));
  } else {
    LOG_INFO("disable replay successfully", K(ret), K_(ls_meta));
  }
  return ret;
}

int ObLS::diagnose(DiagnoseInfo &info) const
{
  int ret = OB_SUCCESS;
  ObLogService *log_service = share::g_mp->log_service();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ls is not inited", K(ret));
  } else if (OB_FAIL(checkpoint_executor_.get_clog_checkpoint_stat(info.ls_clog_checkpoint_stat_))) {
    STORAGE_LOG(WARN, "get clog checkpoint stat failed", K(ret));
  } else if (OB_FAIL(log_service->diagnose_apply(info.apply_diagnose_info_))) {
    STORAGE_LOG(WARN, "diagnose apply failed", K(ret));
  } else if (OB_FAIL(log_service->diagnose_replay(info.replay_diagnose_info_))) {
    STORAGE_LOG(WARN, "diagnose replay failed", K(ret));
  } else if (OB_FAIL(log_handler_.diagnose_palf(info.palf_diagnose_info_))) {
    STORAGE_LOG(WARN, "diagnose palf failed", K(ret));
  }
  ObReadOnlyTxDiagnoseFunctor fn(info.read_only_tx_info_, 0, sizeof(info.read_only_tx_info_));
  READ_CHECKER_FOR_EACH(fn);
  STORAGE_LOG(INFO, "diagnose finish", K(ret), K(info));
  return ret;
}

int ObLS::set_restore_status(const ObRestoreStatus &restore_status)
{
  int ret = OB_SUCCESS;
  WRLockGuard guard(meta_rwlock_);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ls is not inited", K(ret), K(ls_meta_));
  } else if (!restore_status.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("set restore status get invalid argument", K(ret), K(restore_status));
  // restore status should be update after ls stopped, to make sure restore task
  // will be finished later.
  } else if (!ls_meta_.get_persistent_state().can_update_ls_meta()) {
    ret = OB_STATE_NOT_MATCH;
    STORAGE_LOG(WARN, "state not match, cannot update ls meta", K(ret), K(ls_meta_));
  } else if (OB_FAIL(ls_meta_.set_restore_status(ls_epoch_, restore_status))) {
    LOG_WARN("failed to set restore status", K(ret), K(restore_status));
  }
  return ret;
}

bool ObLS::need_delay_resource_recycle() const
{
  LOG_INFO("need delay resource recycle", KPC(this));
  return need_delay_resource_recycle_;
}

void ObLS::set_delay_resource_recycle()
{
  need_delay_resource_recycle_ = true;
  LOG_INFO("set delay resource recycle", KPC(this));
}

void ObLS::clear_delay_resource_recycle()
{
  need_delay_resource_recycle_ = false;
  LOG_INFO("clear delay resource recycle", KPC(this));
}

}
}
