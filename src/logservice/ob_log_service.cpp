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
#include "logservice/palf_handle_guard.h"
#include "logservice/ob_log_allocator_mgr.h"
#include "lib/ob_running_mode.h"
#include "share/ob_share_util.h"

namespace oceanbase
{
using namespace share;
using namespace common;
using namespace palf;

namespace logservice
{
using namespace oceanbase::share;
using namespace oceanbase::common;

ObLogService::ObLogService() :
  is_inited_(false),
  is_running_(false),
  enable_shared_storage_(false),
  self_(),
  palf_env_(NULL),
  alloc_mgr_(NULL),
  apply_service_(),
  replay_service_(),
  log_storage_(NULL),
  runtime_config_(),
  monitor_(),
  update_palf_opts_lock_()
{}

ObLogService::~ObLogService()
{
  destroy();
}

int ObLogService::server_module_init(
    ObLogService *&logservice,
    const palf::PalfOptions &palf_options,
    const char *runtime_clog_dir,
    const char *clog_dir,
    const common::ObAddr &self,
    ObILogStorage *log_storage,
    palf::ILogBlockPool *log_block_pool,
    common::ObIODevice *log_local_device,
    common::ObIOManager *io_manager,
    const bool is_shared_storage_mode,
    const int64_t replay_thread_quota,
    const ObLogRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;
  common::ObILogAllocator *alloc_mgr = NULL;
  if (OB_ISNULL(logservice) || OB_ISNULL(runtime_clog_dir) ||
      OB_ISNULL(clog_dir) || OB_ISNULL(log_storage) ||
      OB_ISNULL(log_block_pool) || OB_ISNULL(log_local_device) ||
      OB_ISNULL(io_manager) || OB_UNLIKELY(!self.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid log service composition", K(ret), KP(logservice),
        KP(runtime_clog_dir), KP(clog_dir), KP(log_storage),
        KP(log_block_pool), KP(log_local_device), KP(io_manager), K(self));
  } else if (OB_FAIL(LOG_ALLOCATOR_MGR_INSTANCE.get_log_allocator(alloc_mgr))) {
  } else if (OB_FAIL(logservice->init(palf_options,
                                      runtime_clog_dir,
                                      self,
                                      alloc_mgr,
                                      log_storage,
                                      log_block_pool,
                                      log_local_device,
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
  if (OB_FAIL(palf_env_->start())) {
  } else if (OB_FAIL(apply_service_.start())) {
  } else if (OB_FAIL(replay_service_.start())) {
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
  (void)replay_service_.stop();
  FLOG_INFO("ObLogService is stopped");
}

void ObLogService::wait()
{
  apply_service_.wait();
  replay_service_.wait();
}

void ObLogService::destroy()
{
  is_inited_ = false;
  self_.reset();
  apply_service_.destroy();
  replay_service_.destroy();
  log_storage_ = NULL;
  if (NULL != palf_env_) {
    PalfEnv::destroy_palf_env(palf_env_);
    palf_env_ = NULL;
  }
  alloc_mgr_ = NULL;
  FLOG_INFO("ObLogService is destroyed");
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

int ObLogService::init(const PalfOptions &options,
                       const char *base_dir,
                       const common::ObAddr &self,
                       common::ObILogAllocator *alloc_mgr,
                       ObILogStorage *log_storage,
                       palf::ILogBlockPool *log_block_pool,
                       common::ObIODevice *log_local_device,
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
      || OB_ISNULL(log_block_pool) || OB_ISNULL(log_local_device)
      || OB_ISNULL(io_manager)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid arguments", K(ret), K(options), KP(base_dir), K(self),
             KP(alloc_mgr), KP(log_storage), KP(log_block_pool),
             KP(log_local_device), KP(io_manager));
  } else if (OB_FAIL(PalfEnv::create_palf_env(options, base_dir, self,
                                              alloc_mgr, log_block_pool, &monitor_,
                                              log_local_device, io_manager, palf_env_))) {
  } else if (OB_ISNULL(palf_env_)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "palf_env_ is NULL", K(ret));
  } else if (OB_FAIL(apply_service_.init(palf_env_, log_storage))) {
  } else if (OB_FAIL(replay_service_.init(
      palf_env_, log_storage, alloc_mgr, replay_thread_quota))) {
  } else {
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

int ObLogService::create_ls(const palf::PalfBaseInfo &palf_base_info,
                            ObLogHandler &log_handler)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (!palf_base_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid palf base info", K(ret), K(palf_base_info));
  } else if (OB_FAIL(create_ls_(palf_base_info, log_handler))) {
  } else {
    FLOG_INFO("ObLogService create_ls success", K(ret), K(palf_base_info), K(log_handler));
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
  } else if (OB_FAIL(replay_service_.remove_status())) {
  } else {
    // NB: can not execute destroy, otherwise, each interface in log_handler or restore_handler
    // may return OB_NOT_INIT.
    // TODO by runlin: create_ls don't init ObLogHandler and ObLogRestoreHandler.
    //
    // In normal case(for gc), stop has been executed, this stop has no effect.
    // In abnormal case(create ls failed, need remove ls directlly), there is no possibility for dead lock.
    log_handler.stop();
    if (OB_FAIL(palf_env_->remove())) {
    } else {
      FLOG_INFO("ObLogService remove_ls success", K(ret));
    }
  }

  return ret;
}

int ObLogService::check_palf_exist(bool &exist) const
{
  int ret = OB_SUCCESS;
  PalfHandle handle;
  exist = true;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "ObLogService is not inited", K(ret));
  } else if (OB_FAIL(palf_env_->open(handle))) {
    if (OB_ENTRY_NOT_EXIST == ret ) {
      ret = OB_SUCCESS;
      exist = false;
    } else {
      CLOG_LOG(WARN, "open palf failed", K(ret));
    }
  }

  if (true == handle.is_valid()) {
    palf_env_->close(handle);
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
  } else if (OB_FAIL(replay_service_.create_status())) {
  } else if (OB_FAIL(log_handler.init(self_, &apply_service_, &replay_service_, palf_env_))) {
  } else {
    FLOG_INFO("add_ls success", K(ret), KP(this));
  }

  return ret;
}

int ObLogService::open_palf(palf::PalfHandleGuard &palf_handle_guard)
{
  int ret = OB_SUCCESS;
  palf::PalfHandle palf_handle;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (OB_FAIL(palf_env_->open(palf_handle))) {
  } else if (FALSE_IT(palf_handle_guard.set(palf_handle, palf_env_))) {
  } else {
  }

  if (OB_FAIL(ret)) {
    if (true == palf_handle.is_valid()) {
      palf_env_->close(palf_handle);
    }
  }

  return ret;
}

int ObLogService::update_replayable_point(const SCN &replayable_point)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (OB_FAIL(replay_service_.update_replayable_point(replayable_point))) {
  }
  return ret;
}

int ObLogService::get_replayable_point(SCN &replayable_point)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (OB_FAIL(replay_service_.get_replayable_point(replayable_point))) {
  }
  return ret;
}

int ObLogService::get_palf_disk_usage(int64_t &used_size_byte, int64_t &total_size_byte)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ret = palf_env_->get_disk_usage(used_size_byte, total_size_byte);
  }
  return ret;
}

int ObLogService::get_palf_stable_disk_usage(int64_t &used_size_byte, int64_t &total_size_byte)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ret = palf_env_->get_stable_disk_usage(used_size_byte, total_size_byte);
  }
  return ret;
}

int ObLogService::update_palf_options_except_disk_usage_limit_size(
    const ObLogRuntimeConfig &runtime_config)
{
  ObSpinLockGuard guard(update_palf_opts_lock_);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    PalfOptions palf_opts;
    if (OB_FAIL(palf_env_->get_options(palf_opts))) {
    } else {
      palf_opts.disk_options_.log_disk_utilization_threshold_ =
          runtime_config.log_disk_utilization_threshold_;
      palf_opts.disk_options_.log_disk_utilization_limit_threshold_ =
          runtime_config.log_disk_utilization_limit_threshold_;
      palf_opts.disk_options_.log_disk_throttling_percentage_ =
          runtime_config.log_disk_throttling_percentage_;
      palf_opts.disk_options_.log_disk_throttling_maximum_duration_ =
          runtime_config.log_disk_throttling_maximum_duration_;
      palf_opts.enable_log_cache_ = runtime_config.enable_log_cache_;
      if (OB_FAIL(palf_env_->update_options(palf_opts))) {
      } else {
        runtime_config_ = runtime_config;
        CLOG_LOG(INFO, "palf update_options success", K(ret), K(palf_opts));
      }
    }
  }
  return ret;
}
//log_disk_usage_limit_size cannot be proactively detected, it can only be passed in when triggered by the upper layer
int ObLogService::update_log_disk_usage_limit_size(const int64_t log_disk_usage_limit_size)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(update_palf_opts_lock_);
  PalfOptions palf_opts;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(palf_env_->get_options(palf_opts))) {
  } else if (FALSE_IT(palf_opts.disk_options_.log_disk_usage_limit_size_ = log_disk_usage_limit_size)) {
  } else if (OB_FAIL(palf_env_->update_options(palf_opts))) {
  } else {
    CLOG_LOG(INFO, "update_log_disk_usage_limit_size success", K(log_disk_usage_limit_size));
  }
  return ret;
}

int ObLogService::get_palf_options(palf::PalfOptions &opts)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ret = palf_env_->get_options(opts);
  }
  return ret;
}

int ObLogService::stat_palf(PalfStat &palf_stat)
{
  int ret = OB_SUCCESS;
  PalfHandleGuard guard;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(open_palf(guard))) {
  } else if (OB_FAIL(guard.get_palf_handle()->stat(palf_stat))) {
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
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ret = replay_service_.stat(replay_stat);
  }
  return ret;
}

int ObLogService::create_ls_(const palf::PalfBaseInfo &palf_base_info,
                             ObLogHandler &log_handler)
{
  int ret = OB_SUCCESS;
  PalfHandle palf_handle;
  bool palf_exist = true;
  if (false == palf_base_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(ret), K(palf_base_info));
  } else if (OB_FAIL(check_palf_exist(palf_exist))) {
  } else if (palf_exist) {
    ret = OB_ENTRY_EXIST;
    CLOG_LOG(WARN, "palf has eixst", K(ret), K(palf_base_info));
  } else {
    if (OB_FAIL(palf_env_->create(palf::AccessMode::APPEND, palf_base_info, palf_handle))) {
    } else if (OB_FAIL(apply_service_.create_status())) {
    } else if (OB_FAIL(replay_service_.create_status())) {
    } else if (OB_FAIL(log_handler.init(self_, &apply_service_, &replay_service_, palf_env_))) {
    } else {
      CLOG_LOG(INFO, "ObLogService create_ls success", K(ret), K(log_handler));
    }
    if (palf_handle.is_valid() && nullptr != palf_env_) {
      palf_env_->close(palf_handle);
    }
    if (OB_FAIL(ret)) {
      CLOG_LOG(ERROR, "create_ls failed!!!", KR(ret));
      replay_service_.remove_status();
      apply_service_.remove_status();
      log_handler.destroy();
      palf_env_->close(palf_handle);
      palf_env_->remove();
    }
  }
  return ret;
}

int ObLogService::diagnose_replay(ReplayDiagnoseInfo &diagnose_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else if (OB_FAIL(replay_service_.diagnose(diagnose_info))) {
  } else {
    // do nothing
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
  } else if (OB_FAIL(palf_env_->get_io_start_time(last_working_time))) {
  } else {
    // do nothing
  }
  return ret;
}

int ObLogService::check_disk_space_enough(bool &is_disk_enough)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "log_service is not inited", K(ret));
  } else {
    is_disk_enough = palf_env_->check_disk_space_enough();
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
  } else if (OB_FAIL(palf_env_->get_disk_usage(used_size, total_size))) {
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
int check_clog_disk_full_or_hang(
    ObLogService &log_service,
    bool &clog_disk_is_full,
    bool &clog_disk_is_hang)
{
  int ret = OB_SUCCESS;
  clog_disk_is_full = false;
  clog_disk_is_hang = false;
  int64_t clog_disk_last_working_time = OB_INVALID_TIMESTAMP;
  const int64_t now = ObTimeUtility::current_time();
  bool is_disk_enough = true;
  if (OB_FAIL(log_service.get_io_start_time(clog_disk_last_working_time))) {
  } else if (OB_FAIL(log_service.check_disk_space_enough(is_disk_enough))) {
  } else {
    clog_disk_is_full = !is_disk_enough;
    clog_disk_is_hang =
        OB_INVALID_TIMESTAMP != clog_disk_last_working_time &&
        now - clog_disk_last_working_time >
            log_service.get_log_storage_warning_tolerance_time();
  }
  return ret;
}
}
}
