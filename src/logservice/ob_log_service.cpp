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

#define USING_LOG_PREFIX CLOG
#include "ob_log_service.h"
#include "logservice/wal_handle_guard.h"
#include "logservice/ob_log_allocator_mgr.h"
#include "lib/ob_running_mode.h"
#include "share/ob_share_util.h"

namespace oceanbase
{
using namespace share;
using namespace common;
using namespace wal;

namespace logservice
{
using namespace oceanbase::share;
using namespace oceanbase::common;

ObLogService::ObLogService() :
  is_inited_(false),
  is_running_(false),
  enable_shared_storage_(false),
  self_(),
  wal_env_(NULL),
  alloc_mgr_(NULL),
  apply_service_(),
  replay_lock_(),
  replay_status_lock_(),
  replay_status_(nullptr),
  replay_enabled_(false),
  replay_handoff_complete_(false),
  replay_service_(nullptr),
  replay_thread_quota_(1),
  log_storage_(NULL),
  runtime_config_(),
  monitor_(),
  update_wal_opts_lock_()
{}

ObLogService::~ObLogService()
{
  destroy();
}

int ObLogService::server_module_init(
    ObLogService *&logservice,
    const wal::Options &wal_options,
    share::LocalStore &store,
    const char *runtime_clog_dir,
    const char *clog_dir,
    const common::ObAddr &self,
    ObILogStorage *log_storage,
    common::ObIOManager *io_manager,
    const bool is_shared_storage_mode,
    const int64_t replay_thread_quota,
    const ObLogRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;
  common::ObILogAllocator *alloc_mgr = NULL;
  if (OB_ISNULL(logservice) || OB_ISNULL(runtime_clog_dir) ||
      OB_ISNULL(clog_dir) || OB_ISNULL(log_storage) ||
      OB_ISNULL(io_manager) || OB_UNLIKELY(!self.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid log service composition", K(ret), KP(logservice),
        KP(runtime_clog_dir), KP(clog_dir), KP(log_storage),
        KP(io_manager), K(self));
  } else if (OB_FAIL(LOG_ALLOCATOR_MGR_INSTANCE.get_log_allocator(alloc_mgr))) {
  } else if (OB_FAIL(logservice->init(wal_options, store,
                                      runtime_clog_dir,
                                      self,
                                      alloc_mgr,
                                      log_storage,
                                      io_manager,
                                      is_shared_storage_mode,
                                      replay_thread_quota,
                                      runtime_config))) {
  } else if (OB_FAIL(FileDirectoryUtils::fsync_dir(clog_dir))) {
  } else {
    CLOG_LOG(INFO, "ObLogService server_module_init success");
  }
  return ret;
}

void ObLogService::server_module_destroy(ObLogService* &logservice)
{
  common::ob_delete(logservice);
  logservice = nullptr;
}

int ObLogService::start()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(wal_env_->start())) {
  } else if (OB_FAIL(apply_service_.start())) {
  } else {
    is_running_ = true;
    FLOG_INFO("ObLogService is started");
  }
  return ret;
}

void ObLogService::stop()
{
  is_running_ = false;
  CLOG_LOG(INFO, "begin to stop ObLogService");
  (void)apply_service_.stop();
  {
    lib::ObMutexGuard guard(replay_lock_);
    if (nullptr != replay_service_) {
      replay_service_->stop();
    }
  }
  FLOG_INFO("ObLogService is stopped");
}

void ObLogService::wait()
{
  apply_service_.wait();
  {
    lib::ObMutexGuard guard(replay_lock_);
    if (nullptr != replay_service_) {
      replay_service_->wait();
    }
  }
}

void ObLogService::destroy()
{
  is_inited_ = false;
  self_.reset();
  apply_service_.destroy();
  (void)disable_replay();
  log_storage_ = NULL;
  if (NULL != wal_env_) {
    Env::destroy_wal_env(wal_env_);
    wal_env_ = NULL;
  }
  alloc_mgr_ = NULL;
  FLOG_INFO("ObLogService is destroyed");
}

int ObLogService::enable_replay(const LSN &lsn, const SCN &scn)
{
  lib::ObMutexGuard guard(replay_lock_);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (nullptr == replay_service_) {
    replay_service_ = OB_NEW(ObLogReplayService, "ReplayService");
    if (nullptr == replay_service_) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(replay_service_->init(wal_env_, log_storage_,
                                            alloc_mgr_, replay_thread_quota_))) {
    } else if (OB_FAIL(replay_service_->start())) {
    } else if (OB_FAIL(replay_service_->create_status())) {
    }
  }
  if (OB_SUCC(ret)) {
    ObReplayStatusGuard status_guard;
    if (OB_FAIL(replay_service_->get_replay_status(status_guard))) {
    } else {
      // Publish before enable can dispatch tasks that query their own replay progress.
      {
        ObLockGuard<ObSpinLock> status_lock(replay_status_lock_);
        replay_status_ = status_guard.get_replay_status();
        replay_enabled_ = true;
        replay_handoff_complete_ = false;
      }
      ret = replay_service_->enable(lsn, scn);
    }
  }
  if (OB_FAIL(ret)) {
    destroy_replay_();
  }
  return ret;
}

int ObLogService::disable_replay()
{
  lib::ObMutexGuard guard(replay_lock_);
  destroy_replay_();
  return OB_SUCCESS;
}

void ObLogService::destroy_replay_()
{
  {
    ObLockGuard<ObSpinLock> status_lock(replay_status_lock_);
    replay_status_ = nullptr;
    replay_enabled_ = false;
  }
  if (nullptr != replay_service_) {
    replay_service_->stop();
    replay_service_->wait();
    OB_DELETE(ObLogReplayService, "ReplayService", replay_service_);
    replay_service_ = nullptr;
  }
}

int ObLogService::finish_replay(const int64_t deadline_us)
{
  lib::ObMutexGuard guard(replay_lock_);
  int ret = OB_SUCCESS;
  if (nullptr != replay_service_) {
    ObReplayStatusGuard status_guard;
    SCN recovered_scn;
    if (OB_FAIL(replay_service_->finish(deadline_us))) {
    } else if (OB_FAIL(replay_service_->get_replay_status(status_guard))) {
    } else if (OB_FAIL(status_guard.get_replay_status()->get_max_replayed_scn(recovered_scn))) {
    } else if (OB_FAIL(apply_service_.start_local_append(recovered_scn))) {
    } else {
      // Apply owns the continuous completion watermark before readers lose the status.
      {
        ObLockGuard<ObSpinLock> status_lock(replay_status_lock_);
        replay_status_ = nullptr;
        replay_handoff_complete_ = true;
      }
      OB_DELETE(ObLogReplayService, "ReplayService", replay_service_);
      replay_service_ = nullptr;
      CLOG_LOG(INFO, "replay executor released", K(recovered_scn));
    }
  }
  return ret;
}

int ObLogService::is_replayed_to(const LSN &target_lsn, bool &reached)
{
  reached = false;
  ObReplayStatusGuard guard;
  int ret = get_replay_status_(guard);
  if (OB_SUCC(ret)) {
    ret = guard.get_replay_status()->is_replayed_to(target_lsn, reached);
  }
  return ret;
}

int ObLogService::get_replay_status_(ObReplayStatusGuard &guard) const
{
  ObLockGuard<ObSpinLock> lock(replay_status_lock_);
  if (nullptr == replay_status_) {
    return OB_STATE_NOT_MATCH;
  }
  guard.set_replay_status(replay_status_);
  return OB_SUCCESS;
}

bool ObLogService::is_replay_enabled() const
{
  ObLockGuard<ObSpinLock> lock(replay_status_lock_);
  return replay_enabled_;
}

bool ObLogService::is_replay_handoff_complete() const
{
  ObLockGuard<ObSpinLock> lock(replay_status_lock_);
  return replay_handoff_complete_;
}

int ObLogService::get_max_replayed_scn(SCN &scn)
{
  ObReplayStatusGuard guard;
  int ret = get_replay_status_(guard);
  if (OB_SUCC(ret)) {
    ret = guard.get_replay_status()->get_max_replayed_scn(scn);
  }
  return ret;
}

int ObLogService::get_min_unreplayed_scn(SCN &scn)
{
  ObReplayStatusGuard guard;
  int ret = get_replay_status_(guard);
  if (OB_SUCC(ret)) {
    ret = guard.get_replay_status()->get_min_unreplayed_scn(scn);
  }
  return ret;
}

int ObLogService::get_max_decided_scn(SCN &scn)
{
  ObReplayStatusGuard guard;
  SCN replayed_scn;
  SCN applied_scn;
  int ret = get_replay_status_(guard);
  if (OB_SUCC(ret)) {
    ret = guard.get_replay_status()->get_max_replayed_scn(replayed_scn);
  }
  // Read apply after replay: retirement publishes its watermark before detaching the status.
  if (OB_SUCCESS == ret || OB_STATE_NOT_MATCH == ret) {
    const int apply_ret = apply_service_.get_max_applied_scn(applied_scn);
    if (OB_SUCCESS != apply_ret) {
      ret = apply_ret;
    } else if (OB_SUCCESS == ret || applied_scn.is_valid()) {
      ret = OB_SUCCESS;
      scn = std::max(SCN::min_scn(), std::max(replayed_scn, applied_scn));
    }
  }
  return ret;
}

int ObLogService::has_replay_fatal_error(bool &has_error)
{
  ObReplayStatusGuard guard;
  int ret = get_replay_status_(guard);
  has_error = false;
  if (OB_STATE_NOT_MATCH == ret) {
    ret = OB_SUCCESS;
  } else if (OB_SUCC(ret)) {
    has_error = guard.get_replay_status()->has_fatal_error();
  }
  return ret;
}

int ObLogService::block_submit_replay()
{
  lib::ObMutexGuard guard(replay_lock_);
  return nullptr == replay_service_ ? OB_SUCCESS : replay_service_->block_submit_log();
}

int ObLogService::unblock_submit_replay()
{
  lib::ObMutexGuard guard(replay_lock_);
  return nullptr == replay_service_ ? OB_SUCCESS : replay_service_->unblock_submit_log();
}

int check_and_prepare_dir(const char *dir)
{
  bool is_exist = false;
  int ret = OB_SUCCESS;
  if (OB_FAIL(common::FileDirectoryUtils::is_exists(dir, is_exist))) {
  } else if (is_exist == true) {
    CLOG_LOG(INFO, "director exist", K(ret), K(dir));
  } else if (OB_FAIL(common::FileDirectoryUtils::create_directory(dir))) {
  } else {
    CLOG_LOG(INFO, "check_and_prepare_dir success", K(ret), K(dir));
  }
  return ret;
}

int ObLogService::init(const Options &options,
                       share::LocalStore &store,
                       const char *base_dir,
                       const common::ObAddr &self,
                       common::ObILogAllocator *alloc_mgr,
                       ObILogStorage *log_storage,
                       common::ObIOManager *io_manager,
                       const bool is_shared_storage_mode,
                       const int64_t replay_thread_quota,
                       const ObLogRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;

  
  if (OB_FAIL(check_and_prepare_dir(base_dir))) {
  } else if (is_inited_) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(WARN, "ObLogService init twice", K(ret));
  } else if (false == options.is_valid() || OB_ISNULL(base_dir) || OB_UNLIKELY(!self.is_valid())
      || OB_ISNULL(alloc_mgr) || OB_ISNULL(log_storage)
      || OB_ISNULL(io_manager)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid arguments", K(ret), K(options), KP(base_dir), K(self),
             KP(alloc_mgr), KP(log_storage), KP(io_manager));
  } else if (OB_FAIL(Env::create_wal_env(options, store, base_dir, self,
                                              &monitor_, wal_env_))) {
  } else if (OB_ISNULL(wal_env_)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "wal_env_ is NULL", K(ret));
  } else if (OB_FAIL(apply_service_.init(wal_env_, log_storage))) {
  } else {
    replay_thread_quota_ = replay_thread_quota;
    alloc_mgr_ = alloc_mgr;
    log_storage_ = log_storage;
    runtime_config_ = runtime_config;
    enable_shared_storage_ = is_shared_storage_mode;
    self_ = self;
    is_inited_ = true;
    FLOG_INFO("ObLogService init success", K(ret), K(base_dir), K(self),
        KP(log_storage), K(enable_shared_storage_));
  }

  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    destroy();
  }
  return ret;
}

int ObLogService::create_ls(const wal::BaseInfo &wal_base_info,
                            ObLogHandler &log_handler)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (!wal_base_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid wal base info", K(ret), K(wal_base_info));
  } else if (OB_FAIL(create_ls_(wal_base_info, log_handler))) {
  } else {
    FLOG_INFO("ObLogService create_ls success", K(ret), K(wal_base_info), K(log_handler));
  }
  return ret;
}

int ObLogService::remove_ls(ObLogHandler &log_handler)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (OB_FAIL(apply_service_.remove_status())) {
  } else if (OB_FAIL(disable_replay())) {
  } else {
    // NB: can not execute destroy, otherwise, each interface in log_handler or restore_handler
    // may return OB_NOT_INIT.
    // TODO by runlin: create_ls don't init ObLogHandler and ObLogRestoreHandler.
    //
    // In normal case(for gc), stop has been executed, this stop has no effect.
    // In abnormal case(create ls failed, need remove ls directlly), there is no possibility for dead lock.
    log_handler.stop();
    if (OB_FAIL(wal_env_->remove())) {
    } else {
      FLOG_INFO("ObLogService remove_ls success", K(ret));
    }
  }

  return ret;
}

int ObLogService::check_wal_exist(bool &exist) const
{
  int ret = OB_SUCCESS;
  Handle handle;
  exist = true;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "ObLogService is not inited", K(ret));
  } else if (OB_FAIL(wal_env_->open(handle))) {
    if (OB_ENTRY_NOT_EXIST == ret ) {
      ret = OB_SUCCESS;
      exist = false;
    } else {
      CLOG_LOG(WARN, "open wal failed", K(ret));
    }
  }

  if (true == handle.is_valid()) {
    wal_env_->close(handle);
  }
  return ret;
}

int ObLogService::add_ls(ObLogHandler &log_handler)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (OB_FAIL(apply_service_.create_status())) {
  } else if (OB_FAIL(log_handler.init(self_, &apply_service_, this, wal_env_))) {
  } else {
    FLOG_INFO("add_ls success", K(ret), KP(this));
  }

  return ret;
}

int ObLogService::open_wal(wal::HandleGuard &wal_handle_guard)
{
  int ret = OB_SUCCESS;
  wal::Handle wal_handle;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (OB_FAIL(wal_env_->open(wal_handle))) {
  } else if (FALSE_IT(wal_handle_guard.set(wal_handle, wal_env_))) {
  } else {
  }

  if (OB_FAIL(ret)) {
    if (true == wal_handle.is_valid()) {
      wal_env_->close(wal_handle);
    }
  }

  return ret;
}

int ObLogService::update_replayable_point(const SCN &replayable_point)
{
  lib::ObMutexGuard guard(replay_lock_);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (nullptr != replay_service_) {
    ret = replay_service_->update_replayable_point(replayable_point);
  } else {
    ret = OB_STATE_NOT_MATCH;
  }
  return ret;
}

int ObLogService::get_wal_disk_usage(int64_t &used_size_byte, int64_t &total_size_byte)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ret = wal_env_->get_disk_usage(used_size_byte, total_size_byte);
  }
  return ret;
}

int ObLogService::update_log_runtime_config(const ObLogRuntimeConfig &runtime_config)
{
  ObSpinLockGuard guard(update_wal_opts_lock_);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    runtime_config_ = runtime_config;
  }
  return ret;
}
//log_disk_usage_limit_size cannot be proactively detected, it can only be passed in when triggered by the upper layer
int ObLogService::update_log_disk_usage_limit_size(const int64_t log_disk_usage_limit_size)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(update_wal_opts_lock_);
  Options wal_opts;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(wal_env_->get_options(wal_opts))) {
  } else if (FALSE_IT(wal_opts.disk_options_.log_disk_usage_limit_size_ = log_disk_usage_limit_size)) {
  } else if (OB_FAIL(wal_env_->update_options(wal_opts))) {
  } else {
    CLOG_LOG(INFO, "update_log_disk_usage_limit_size success", K(log_disk_usage_limit_size));
  }
  return ret;
}

int ObLogService::get_wal_options(wal::Options &opts)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ret = wal_env_->get_options(opts);
  }
  return ret;
}

int ObLogService::stat_wal(Stat &wal_stat)
{
  int ret = OB_SUCCESS;
  HandleGuard guard;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(open_wal(guard))) {
  } else if (OB_FAIL(guard.get_wal_handle()->stat(wal_stat))) {
  }
  return ret;
}

int ObLogService::stat_apply(LSApplyStat &apply_stat)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ret = apply_service_.stat(apply_stat);
  }
  return ret;
}

int ObLogService::stat_replay(LSReplayStat &replay_stat)
{
  int ret = OB_SUCCESS;
  ObReplayStatusGuard guard;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_SUCC(ret = get_replay_status_(guard))) {
    ret = guard.get_replay_status()->stat(replay_stat);
  }
  if (OB_STATE_NOT_MATCH == ret) {
    // No row after retirement: do not keep or fabricate a historical submit cursor.
    ret = OB_ITER_END;
  } else if (OB_SUCC(ret)) {
    wal::HandleGuard handle;
    if (OB_FAIL(open_wal(handle))) {
    } else {
      ret = handle.get_wal_handle()->get_end_lsn(replay_stat.end_lsn_);
    }
  }
  return ret;
}

int ObLogService::create_ls_(const wal::BaseInfo &wal_base_info,
                             ObLogHandler &log_handler)
{
  int ret = OB_SUCCESS;
  Handle wal_handle;
  bool wal_exist = true;
  if (false == wal_base_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(ret), K(wal_base_info));
  } else if (OB_FAIL(check_wal_exist(wal_exist))) {
  } else if (wal_exist) {
    ret = OB_ENTRY_EXIST;
    CLOG_LOG(WARN, "wal has eixst", K(ret), K(wal_base_info));
  } else {
    if (OB_FAIL(wal_env_->create(wal_base_info, wal_handle))) {
    } else if (OB_FAIL(apply_service_.create_status())) {
    } else if (OB_FAIL(log_handler.init(self_, &apply_service_, this, wal_env_))) {
    } else {
      CLOG_LOG(INFO, "ObLogService create_ls success", K(ret), K(log_handler));
    }
    if (wal_handle.is_valid() && nullptr != wal_env_) {
      wal_env_->close(wal_handle);
    }
    if (OB_FAIL(ret)) {
      CLOG_LOG(ERROR, "create_ls failed!!!", KR(ret));
      disable_replay();
      apply_service_.remove_status();
      log_handler.destroy();
      wal_env_->close(wal_handle);
      wal_env_->remove();
    }
  }
  return ret;
}

int ObLogService::diagnose_replay(ReplayDiagnoseInfo &diagnose_info)
{
  ObReplayStatusGuard guard;
  int ret = get_replay_status_(guard);
  if (OB_SUCC(ret)) {
    ret = guard.get_replay_status()->diagnose(diagnose_info);
  }
  return ret;
}

int ObLogService::diagnose_apply(ApplyDiagnoseInfo &diagnose_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (OB_FAIL(apply_service_.diagnose(diagnose_info))) {
  } else {
    // do nothing
  }
  return ret;
}

int ObLogService::get_io_start_time(int64_t &last_working_time)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (OB_FAIL(wal_env_->get_io_start_time(last_working_time))) {
  } else {
    // do nothing
  }
  return ret;
}

int ObLogService::check_need_do_checkpoint(bool &need_do_checkpoint)
{
  int ret = OB_SUCCESS;
  need_do_checkpoint = false;
  int64_t total_size = 0;
  int64_t used_size = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (OB_FAIL(wal_env_->get_disk_usage(used_size, total_size))) {
  } else {
    int64_t unrecyclable_log_disk_size = 0;
    const int64_t CHECKPOINT_PERCENTAGE = enable_shared_storage_ ? 60 : 30;
    if (OB_FAIL(get_unrecyclable_log_disk_size(unrecyclable_log_disk_size))) {
    } else {
      need_do_checkpoint = (unrecyclable_log_disk_size * 100 >= total_size * CHECKPOINT_PERCENTAGE);
    }
  }
  return ret;
}

int ObLogService::get_unrecyclable_log_disk_size(int64_t &unrecyclable_log_disk_size)
{
  int ret = OB_SUCCESS;
  unrecyclable_log_disk_size = 0;
  if (OB_ISNULL(log_storage_)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "log storage is null", K(ret));
  } else if (OB_FAIL(log_storage_->get_unrecyclable_log_disk_size(
      unrecyclable_log_disk_size))) {
  }
  return ret;
}

}//end of namespace logservice
}//end of namespace oceanbase

namespace oceanbase
{
namespace logservice
{
using namespace oceanbase::share;
int check_clog_disk_hang(ObLogService &log_service, bool &clog_disk_is_hang)
{
  int ret = OB_SUCCESS;
  clog_disk_is_hang = false;
  int64_t clog_disk_last_working_time = OB_INVALID_TIMESTAMP;
  const int64_t now = ObTimeUtility::current_time();
  if (OB_FAIL(log_service.get_io_start_time(clog_disk_last_working_time))) {
  } else {
    clog_disk_is_hang =
        OB_INVALID_TIMESTAMP != clog_disk_last_working_time &&
        now - clog_disk_last_working_time >
            log_service.get_log_storage_warning_tolerance_time();
  }
  return ret;
}
}
}
