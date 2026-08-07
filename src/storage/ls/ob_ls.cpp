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
#include "share/rc/ob_server_runtime.h"
#define USING_LOG_PREFIX STORAGE

#include "data_plane/ob_log_service_handler.h"
#include "logservice/ob_log_service.h"
#include "storage/compaction/ob_tablet_scheduler.h"
#include "storage/compaction/ob_tablet_merge_ctx.h"
#include "storage/ls/ob_ls.h"
#include "storage/ls/ob_i_ls_runtime_adapter.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "storage/tx/ob_timestamp_service.h"
#include "storage/tx/ob_trans_id_service.h"
#include "storage/tx_storage/ob_memstore_freezer.h"

namespace oceanbase
{
using namespace share;
using namespace logservice;
using namespace transaction;

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
    replay_handler_(),
    ls_freezer_(this),
    ls_sync_tablet_seq_handler_(),
    ls_ddl_log_handler_(),
    vector_idx_scheduler_(nullptr),
    is_inited_(false),
    running_state_(),
    state_seq_(-1),
    switch_epoch_(0),
    is_local_append_mode_(false),
    ls_meta_(),
    ls_epoch_(0)
{}

ObLS::~ObLS()
{
  destroy();
}

int ObLS::init(const ObRestoreStatus &restore_status,
               const SCN &create_scn,
               const palf::LSN &clog_base_lsn)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObLogService *logservice = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
  ObTransService *txs_svr = ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>();

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ls is already initialized", K(ret), K_(ls_meta));
  } else if (OB_FAIL(ls_meta_.init(restore_status, create_scn, clog_base_lsn))) {
  } else if (OB_FAIL(ls_freezer_.init(this))) {
  } else {
    ObTxPalfParam tx_palf_param(get_log_handler());
    common::ObInOutBandwidthThrottle *bandwidth_throttle = GCTX.bandwidth_throttle_;
    if (OB_ISNULL(bandwidth_throttle)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("bandwidth throttle should not be NULL", KR(ret));
    } else if (OB_FAIL(txs_svr->create_ls(*this, &tx_palf_param, nullptr))) {
    } else if (OB_FAIL(ls_tablet_svr_.init(this))) {
    } else if (OB_FAIL(tx_table_.init(this))) {
    } else if (OB_FAIL(checkpoint_executor_.init(this, get_log_handler()))) {
    } else if (OB_FAIL(data_checkpoint_.init(this))) {
    } else if (OB_FAIL(ls_tx_svr_.register_common_checkpoint(checkpoint::DATA_CHECKPOINT_TYPE, &data_checkpoint_))) {
    } else if (OB_FAIL(lock_table_.init(this))) {
    } else if (OB_FAIL(ls_sync_tablet_seq_handler_.init(this))) {
    } else if (OB_FAIL(ls_ddl_log_handler_.init(this))) {
    } else if (OB_FAIL(keep_alive_ls_handler_.init(get_log_handler()))) {
    } else if (OB_FAIL(ls_wrs_handler_.init())) {
    } else if (OB_FAIL(tablet_gc_handler_.init(this))) {
    } else if (OB_FAIL(tablet_empty_shell_handler_.init(this))) {
    } else if (OB_FAIL(reserved_snapshot_mgr_.init(this, &log_handler_))) {
    } else if (OB_FAIL(reserved_snapshot_clog_handler_.init(this))) {
    } else if (OB_FAIL(medium_compaction_clog_handler_.init(this))) {
    } else if (OB_FAIL(register_to_service_())) {
    } else {
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

int ObLS::create_ls_inner_tablet(const SCN &create_scn)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_FAIL(tx_table_.create_tablet(create_scn))) {
  } else if (OB_FAIL(lock_table_.create_tablet(create_scn))) {
  }
  if (OB_FAIL(ret)) {
    do {
      if (OB_TMP_FAIL(remove_ls_inner_tablet())) {
      }
    } while (OB_TMP_FAIL(tmp_ret));
  }
  return ret;
}

int ObLS::remove_ls_inner_tablet()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tx_table_.remove_tablet())) {
  } else if (OB_FAIL(lock_table_.remove_tablet())) {
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
  ObLogService *logservice = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls do not init", K(ret));
  } else if (OB_FAIL(logservice->check_palf_exist(is_palf_exist))) {
  } else if (is_palf_exist) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("palf should not exist now", K(ret), K_(ls_meta));
  } else if (OB_FAIL(logservice->create_ls(palf_base_info, log_handler_))) {
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
  ObLogService *logservice = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
  bool is_palf_exist = false;

  if (OB_FAIL(logservice->check_palf_exist(is_palf_exist))) {
  } else if (!is_palf_exist) {
    LOG_WARN("there is no ls at disk, skip load", K_(ls_meta));
  } else if (OB_FAIL(logservice->add_ls(log_handler_))) {
  } else {
    // TODO: add_ls has no interface to rollback now, something can not rollback.
    if (OB_FAIL(ret)) {
    }
  }
  return ret;
}

int ObLS::remove_ls()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObLogService *logservice = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls do not init", K(ret));
  } else {
    if (OB_FAIL(logservice->remove_ls(log_handler_))) {
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
  } else {
    update_state_seq_();
  }
  return ret;
}

int ObLS::set_start_restore_state()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ls_meta_.set_start_restore_state())) {
  } else {
    update_state_seq_();
  }
  return ret;
}

int ObLS::set_remove_state()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ls_meta_.set_remove_state())) {
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
  } else if (OB_FAIL(running_state_.stop())) {
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
  } else if (OB_FAIL(ls_tablet_svr_.prepare_for_safe_destroy())) {
  } else if (OB_FAIL(tx_table_.prepare_for_safe_destroy())) {
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
  ObTransService *txs_svr = (true ? ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>() : nullptr);
  FLOG_INFO("ObLS destroy", K(this), K(*this), K(lbt()));
  if (running_state_.is_running()) {
    if (OB_TMP_FAIL(offline_(start_ts))) {
    }
  }
  if (OB_TMP_FAIL(stop_())) {
  } else {
    wait_();
    if (OB_TMP_FAIL(prepare_for_safe_destroy_())) {
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
}

int ObLS::offline_tx_(const int64_t start_ts)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ls_tx_svr_.prepare_offline(start_ts))) {
  } else if (OB_FAIL(tx_table_.prepare_offline())) {
  } else if (OB_FAIL(ls_tx_svr_.offline())) {
  } else if (OB_FAIL(tx_table_.offline())) {
  }
  return ret;
}

int ObLS::offline_compaction_()
{
  int ret = OB_SUCCESS;
  if (FALSE_IT(ls_freezer_.offline())) {
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::compaction::ObTabletScheduler>()->check_ls_compaction_finish())) {
  }
  return ret;
}

int ObLS::start_local_log_()
{
  int ret = OB_SUCCESS;
  palf::LSN end_lsn;
  bool is_done = false;
  bool is_clear = false;
  logservice::ObLogApplyService *apply_service = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>()->get_log_apply_service();
  logservice::ObLogReplayService *replay_service = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>()->get_log_replay_service();
  if (OB_FAIL(log_handler_.get_end_lsn(end_lsn))) {
  }
  while (OB_SUCC(ret) && !is_done) {
    if (OB_FAIL(replay_service->is_replay_done(end_lsn, is_done))) {
    } else if (!is_done) {
      ob_usleep(50 * 1000);
    }
  }
  if (OB_SUCC(ret)) {
    int tmp_ret = apply_service->start_local_append();
    if (OB_STATE_NOT_MATCH == tmp_ret) {
      tmp_ret = OB_SUCCESS;
    }
    if (OB_SUCCESS != tmp_ret) {
      ret = tmp_ret;
      LOG_WARN("start local apply failed", K(ret));
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(replay_service->disable_local_replay())) {
    LOG_WARN("stop local replay failed", K(ret));
  }
  while (OB_SUCC(ret) && !is_clear) {
    if (OB_FAIL(replay_service->is_submit_task_clear(is_clear))) {
    } else if (!is_clear) {
      ob_usleep(1000);
    }
  }
  if (OB_SUCC(ret)) {
    log_handler_.set_local_append_enabled(true);
    if (OB_FAIL(local_log_handler_set_.activate())) {
      log_handler_.set_local_append_enabled(false);
    } else {
      is_local_append_mode_ = true;
    }
  }
  return ret;
}

int ObLS::start_local_replay_()
{
  int ret = OB_SUCCESS;
  palf::LSN end_lsn;
  share::SCN end_scn;
  logservice::ObLogService *log_service =
      ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
  if (OB_ISNULL(log_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log service is null", K(ret));
  } else if (OB_FAIL(log_handler_.get_end_lsn(end_lsn))) {
    LOG_WARN("get local log end failed", K(ret));
  } else if (OB_FAIL(log_handler_.get_end_scn(end_scn))) {
    LOG_WARN("get local log end scn failed", K(ret), K(end_lsn));
  } else {
    log_handler_.set_local_append_enabled(false);
    local_log_handler_set_.deactivate();
    if (OB_FAIL(log_service->get_log_apply_service()->start_local_append())) {
      LOG_WARN("start standby import callbacks failed", K(ret));
    } else if (OB_FAIL(log_service->get_log_replay_service()->enable_local_replay(
        end_lsn, share::SCN::scn_inc(end_scn)))) {
      LOG_WARN("start local replay failed", K(ret), K(end_lsn), K(end_scn));
    } else {
      is_local_append_mode_ = false;
    }
  }
  return ret;
}

int ObLS::stop_local_log_(const bool keep_import_callbacks)
{
  int ret = OB_SUCCESS;
  bool is_done = false;
  palf::LSN end_lsn;
  share::SCN end_scn;
  logservice::ObLogApplyService *apply_service = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>()->get_log_apply_service();
  logservice::ObLogReplayService *replay_service = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>()->get_log_replay_service();
  log_handler_.set_local_append_enabled(false);
  local_log_handler_set_.deactivate();
  if (OB_FAIL(apply_service->wait_append_sync())) {
  } else if (OB_FAIL(apply_service->stop_local_append())) {
  }
  while (OB_SUCC(ret) && !is_done) {
    if (OB_FAIL(apply_service->is_apply_done(is_done, end_lsn))) {
    } else if (!is_done) {
      ob_usleep(5 * 1000);
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(log_handler_.get_end_scn(end_scn))) {
    } else if (OB_FAIL(replay_service->enable_local_replay(
        end_lsn, share::SCN::scn_inc(end_scn)))) {
    } else if (keep_import_callbacks && OB_FAIL(apply_service->start_local_append())) {
    } else {
      is_local_append_mode_ = false;
    }
  }
  return ret;
}

int ObLS::switch_to_local_append_mode_()
{
  int ret = OB_SUCCESS;
  if (is_local_append_mode_) {
    LOG_INFO("local log is already in append mode", K_(ls_meta));
  } else if (OB_FAIL(start_local_log_())) {
    LOG_WARN("failed to switch local log to append mode", K(ret), K_(ls_meta));
  }
  return ret;
}

int ObLS::switch_to_local_replay_mode_()
{
  int ret = OB_SUCCESS;
  if (!is_local_append_mode_) {
    LOG_INFO("local log is already in replay mode", K_(ls_meta));
  } else if (OB_FAIL(stop_local_log_(true))) {
    LOG_WARN("failed to switch local log to replay mode", K(ret), K_(ls_meta));
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
  } else if (FALSE_IT(update_state_seq_())) {
  } else if (OB_FAIL(offline_advance_epoch_())) {
  } else if (FALSE_IT(checkpoint_executor_.offline())) {
    LOG_WARN("checkpoint executor offline failed", K(ret), K(ls_meta_));
  } else if (is_local_append_mode_ && OB_FAIL(stop_local_log_(false))) {
  } else if (OB_FAIL(log_handler_.offline())) {
  } else if (OB_FAIL(ls_tablet_svr_.set_frozen_for_all_memtables())) {
  }
  // make sure no new dag(tablet_gc_handler may generate new dag) is generated after offline offline_compaction_
  else if (OB_FAIL(tablet_gc_handler_.offline())) {
  } else if (OB_FAIL(offline_compaction_())) {
  } else if (OB_FAIL(ls_wrs_handler_.offline())) {
  } else if (OB_FAIL(ls_ddl_log_handler_.offline())) {
  } else if (OB_FAIL(offline_tx_(start_ts))) {
  } else if (OB_FAIL(lock_table_.offline())) {
  } else if (OB_FAIL(ls_tablet_svr_.offline())) {
  } else if (OB_FAIL(tablet_empty_shell_handler_.offline())) {
  } else if (OB_FAIL(running_state_.post_offline())) {
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
  } else if (OB_FAIL(tx_table_.online())) {
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

int ObLS::register_vector_index_log_handler_(
    const logservice::ObLogBaseType type,
    data_plane::ObIVectorIndexLogHandler &handler)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(replay_handler_.register_handler(type, &handler.replay_handler()))) {
  } else if (OB_FAIL(local_log_handler_set_.register_handler(
                 type, &handler.local_handler()))) {
    LOG_WARN("local handler register failed", K(ret), K(type), K(ls_meta_));
    replay_handler_.unregister_handler(type);
  } else if (OB_FAIL(checkpoint_executor_.register_handler(
                 type, &handler.checkpoint_handler()))) {
    LOG_WARN("checkpoint handler register failed", K(ret), K(type), K(ls_meta_));
    local_log_handler_set_.unregister_handler(type);
    replay_handler_.unregister_handler(type);
  }
  return ret;
}

void ObLS::unregister_vector_index_log_handler_(
    const logservice::ObLogBaseType type)
{
  replay_handler_.unregister_handler(type);
  local_log_handler_set_.unregister_handler(type);
  checkpoint_executor_.unregister_handler(type);
}

int ObLS::register_composition_log_handler_(
    const logservice::ObLogBaseType type)
{
  int ret = OB_SUCCESS;
  data_plane::ObLogServiceHandler handler;
  ObILSRuntimeAdapter *adapter =
      ::oceanbase::share::server_service<::oceanbase::storage::ObILSRuntimeAdapter>();
  if (OB_ISNULL(adapter)) {
    ret = OB_NOT_INIT;
    LOG_WARN("LS runtime adapter is not initialized", K(ret), K(type));
  } else if (OB_FAIL(adapter->resolve_log_handler(type, handler))) {
  } else if (OB_UNLIKELY(!handler.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("composition log handler is invalid", K(ret), K(type), K(ls_meta_));
  } else if (OB_FAIL(replay_handler_.register_handler(type, handler.replay_))) {
  } else if (OB_FAIL(local_log_handler_set_.register_handler(type, handler.local_))) {
    LOG_WARN("local handler register failed", K(ret), K(type), K(ls_meta_));
    replay_handler_.unregister_handler(type);
  } else if (OB_FAIL(checkpoint_executor_.register_handler(type, handler.checkpoint_))) {
    LOG_WARN("checkpoint handler register failed", K(ret), K(type), K(ls_meta_));
    local_log_handler_set_.unregister_handler(type);
    replay_handler_.unregister_handler(type);
  }
  return ret;
}

void ObLS::unregister_composition_log_handler_(
    const logservice::ObLogBaseType type)
{
  replay_handler_.unregister_handler(type);
  local_log_handler_set_.unregister_handler(type);
  checkpoint_executor_.unregister_handler(type);
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

  REGISTER_REPLAY_CHECKPOINT_HANDLER(TIMESTAMP_LOG_BASE_TYPE, ::oceanbase::share::server_service<::oceanbase::transaction::ObTimestampService>());
  REGISTER_REPLAY_CHECKPOINT_HANDLER(TRANS_ID_LOG_BASE_TYPE, ::oceanbase::share::server_service<::oceanbase::transaction::ObTransIDService>());
  if (OB_SUCC(ret) &&
      OB_FAIL(register_composition_log_handler_(MAJOR_FREEZE_LOG_BASE_TYPE))) {
    LOG_WARN("failed to register major freeze log handler", K(ret), K(ls_meta_));
  }
  return ret;
}

int ObLS::register_local_services_()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(register_composition_log_handler_(DBMS_SCHEDULER_LOG_BASE_TYPE))) {
  } else if (OB_FAIL(register_composition_log_handler_(SYS_DDL_SCHEDULER_LOG_BASE_TYPE))) {
  } else if (OB_FAIL(register_composition_log_handler_(DDL_SERVICE_LAUNCHER_LOG_BASE_TYPE))) {
  } else if (OB_FAIL(register_composition_log_handler_(
                 SYSTEM_PACKAGE_LOAD_SERVICE_LOG_BASE_TYPE))) {
  } else if (OB_FAIL(register_composition_log_handler_(VEC_INDEX_SERVICE_LOG_BASE_TYPE))) {
  }
  logservice::ObILocalLogHandler *refresh_handler =
      ::oceanbase::share::server_service<::oceanbase::logservice::ObILocalLogHandler>();
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(refresh_handler)) {
    ret = OB_NOT_INIT;
    LOG_WARN("internal table refresh handler is not initialized", K(ret));
  } else if (OB_FAIL(local_log_handler_set_.register_handler(
      INTERNAL_TABLE_NOTIFIER_LOG_BASE_TYPE, refresh_handler))) {
  }
#ifdef OB_BUILD_SYS_VEC_IDX
  if (OB_SUCC(ret)) {
    // The vector index scheduler owns its lifecycle independently.
    if (OB_FAIL(init_vector_idx_scheduler_())) {
    } else {
      if (OB_FAIL(register_vector_index_log_handler_(
              VEC_INDEX_LOG_BASE_TYPE, *vector_idx_scheduler_))) {
      }
    }
  }
#endif

  return ret;
}

int ObLS::register_to_service_()
{
  int ret = OB_SUCCESS;
  
  if (OB_FAIL(register_common_service())) {
  } else if (OB_FAIL(register_local_services_())) {
  }

  return ret;
}

int ObLS::init_vector_idx_scheduler_()
{
  int ret = OB_SUCCESS;
  ObILSRuntimeAdapter *adapter =
      ::oceanbase::share::server_service<::oceanbase::storage::ObILSRuntimeAdapter>();
  if (OB_ISNULL(adapter)) {
    ret = OB_NOT_INIT;
    LOG_WARN("LS runtime adapter is not initialized", K(ret));
  } else if (vector_idx_scheduler_timer_.inited() ||
             OB_NOT_NULL(vector_idx_scheduler_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("vector index scheduler init twice", KR(ret));
  } else if (OB_FAIL(vector_idx_scheduler_timer_.init(
      "VecIdxSched", common::ObMemAttr("VecIdxSched")))) {
  } else if (OB_FAIL(adapter->create_vector_index_scheduler(
                 *this, vector_idx_scheduler_timer_, vector_idx_scheduler_))) {
  }
  if (OB_SUCCESS != ret) {
    if (OB_NOT_NULL(vector_idx_scheduler_) && OB_NOT_NULL(adapter)) {
      adapter->destroy_vector_index_scheduler(vector_idx_scheduler_);
    }
    if (vector_idx_scheduler_timer_.inited()) {
      vector_idx_scheduler_timer_.stop();
      vector_idx_scheduler_timer_.wait();
      vector_idx_scheduler_timer_.destroy();
    }
  }
  return ret;
}

void ObLS::stop_vector_idx_scheduler_()
{
  if (vector_idx_scheduler_timer_.inited()) {
    vector_idx_scheduler_timer_.stop();
  }
  if (OB_NOT_NULL(vector_idx_scheduler_)) {
    vector_idx_scheduler_->stop();
  }
}

void ObLS::destroy_vector_idx_scheduler_()
{
  if (vector_idx_scheduler_timer_.inited()) {
    vector_idx_scheduler_timer_.wait();
    vector_idx_scheduler_timer_.destroy();
  }
  ObILSRuntimeAdapter *adapter =
      ::oceanbase::share::server_service<::oceanbase::storage::ObILSRuntimeAdapter>();
  if (OB_NOT_NULL(vector_idx_scheduler_)) {
    if (OB_NOT_NULL(adapter)) {
      adapter->destroy_vector_index_scheduler(vector_idx_scheduler_);
    } else {
      FLOG_ERROR_RET(OB_ERR_UNEXPECTED,
          "cannot destroy vector index scheduler without runtime adapter");
    }
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
  unregister_composition_log_handler_(MAJOR_FREEZE_LOG_BASE_TYPE);
}

void ObLS::unregister_local_services_()
{
  unregister_composition_log_handler_(DBMS_SCHEDULER_LOG_BASE_TYPE);
  unregister_composition_log_handler_(SYS_DDL_SCHEDULER_LOG_BASE_TYPE);
  unregister_composition_log_handler_(DDL_SERVICE_LAUNCHER_LOG_BASE_TYPE);
  unregister_composition_log_handler_(
      SYSTEM_PACKAGE_LOAD_SERVICE_LOG_BASE_TYPE);
  local_log_handler_set_.unregister_handler(INTERNAL_TABLE_NOTIFIER_LOG_BASE_TYPE);
#ifdef OB_BUILD_SYS_VEC_IDX
  unregister_composition_log_handler_(VEC_INDEX_SERVICE_LOG_BASE_TYPE);
  unregister_vector_index_log_handler_(VEC_INDEX_LOG_BASE_TYPE);
  destroy_vector_idx_scheduler_();
#endif
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
  return online_without_lock_(true);
}

int ObLS::online_for_physical_restore_without_lock()
{
  return online_without_lock_(false);
}

int ObLS::online_without_lock_(const bool start_in_append_mode)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else if (running_state_.is_running()) {
    LOG_INFO("ls is running state, do nothing", K(ret));
  } else if (OB_FAIL(ls_tablet_svr_.online())) {
  } else if (OB_FAIL(lock_table_.online())) {
  } else if (OB_FAIL(online_tx_())) {
  } else if (!start_in_append_mode && OB_FAIL(ls_tx_svr_.block_tx())) {
  } else if (OB_FAIL(ls_ddl_log_handler_.online())) {
  } else if (OB_FAIL(log_handler_.online(ls_meta_.get_clog_base_lsn(),
                                         ls_meta_.get_clog_checkpoint_scn()))) {
  } else if (OB_FAIL(ls_wrs_handler_.online())) {
  } else if (OB_FAIL(online_compaction_())) {
  } else if (start_in_append_mode && OB_FAIL(start_local_log_())) {
  } else if (!start_in_append_mode && OB_FAIL(start_local_replay_())) {
  } else if (FALSE_IT(checkpoint_executor_.online())) {
  } else if (FALSE_IT(tablet_gc_handler_.online())) {
  } else if (FALSE_IT(tablet_empty_shell_handler_.online())) {
  } else if (OB_FAIL(online_advance_epoch_())) {
  } else if (OB_FAIL(running_state_.online())) {
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
    } else if (OB_FAIL(ObIDService::update_id_service(all_id_meta))) {
    }
  }
  return ret;
}

int ObLS::update_meta_for_physical_restore(const ObLSMeta &source_meta)
{
  int ret = OB_SUCCESS;
  ObAllIDMeta all_id_meta;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls is not inited", K(ret));
  } else if (OB_FAIL(ls_meta_.update_for_physical_restore(ls_epoch_, source_meta))) {
    LOG_WARN("failed to update ls meta for physical restore", K(ret), K(source_meta));
  } else if (OB_FAIL(ls_meta_.get_all_id_meta(all_id_meta))) {
    LOG_WARN("failed to get restored id meta", K(ret), K_(ls_meta));
  } else if (OB_FAIL(ObIDService::update_id_service(all_id_meta))) {
    LOG_WARN("failed to update id services after physical restore", K(ret), K(all_id_meta));
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
  } else {
    // The primary database uses the weak-read timestamp; the standby database
    // uses its readable SCN.
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
    LOG_WARN("build tablet table store get invalid argument", K(ret), K(tablet_id), K(param));
  } else if (OB_FAIL(ls_tablet_svr_.build_tablet_with_batch_tables(tablet_id, param))) {
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
  }
  return ret;
}

int ObLS::finish_storage_meta_replay()
{
  int ret = OB_SUCCESS;
  int64_t read_lock = 0;
  int64_t write_lock = LSLOCKALL;
  ObLSLockGuard lock_myself(this, lock_, read_lock, write_lock);

  if (OB_FAIL(running_state_.create_finish())) {
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
    } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>()->get_log_replay_service()->get_max_replayed_scn(max_scn))) {
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
  } else if (tablet_id.is_ls_inner_tablet()) {
    // do nothing
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), KP(tablet), K(tablet_id), K(scn));
  } else if (tablet->is_empty_shell()) {
    ObTabletStatus::Status tablet_status = ObTabletStatus::MAX;
    if (OB_FAIL(tablet->get_latest(data, writer, trans_stat, trans_version))) {
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
    ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>()->record_freezer_source_event(source);
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
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ls_freezer_.wait_ls_freeze_finish())) {
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
    ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>()->record_freezer_source_event(source);
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
                                      const bool is_global_freeze,
                                      const ObFreezeSourceFlag source)
{
  int ret = OB_SUCCESS;
  if (is_global_freeze) {
    ObDataCheckpoint::set_global_freeze();
    LOG_INFO("set global freeze");
  }
  ObDataCheckpoint::set_freeze_source(source);
  ret = checkpoint_executor_.advance_checkpoint_by_flush(recycle_scn);
  ObDataCheckpoint::reset_freeze_source();
  ObDataCheckpoint::reset_global_freeze();
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
  }
  ObDataCheckpoint::reset_freeze_source();
  return ret;
}

int ObLS::check_ls_need_online(bool &need_online)
{
  int ret = OB_SUCCESS;
  need_online = true;
  if (OB_FAIL(ls_meta_.check_ls_need_online(need_online))) {
  }
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
  }
  return ret;
}

}
}
