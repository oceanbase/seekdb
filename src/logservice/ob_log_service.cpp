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
#include "share/rc/ob_module_provider.h"
#include "ob_server_log_block_mgr.h"
#include "logservice/palf_handle_guard.h"
#include "logservice/ob_tenant_mutil_allocator_mgr.h"
#include "share/rc/ob_tenant_module_init_ctx.h"
#include "observer/ob_srv_network_frame.h"
#include "storage/ob_file_system_router.h"
#include "logservice/ob_net_keepalive_adapter.h"            // ObNetKeepAliveAdapter
#include "share/ob_io_device_helper.h"
#include "lib/ob_running_mode.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/ob_share_util.h"  // relocated-definition owner

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
  self_(),
  palf_env_(NULL),
  net_keepalive_adapter_(NULL),
  alloc_mgr_(NULL),
  apply_service_(),
  replay_service_(),
  ls_adapter_(),
  rpc_proxy_(),
  monitor_(),
  update_palf_opts_lock_()
{}

ObLogService::~ObLogService()
{
  destroy();
}

int ObLogService::mtl_init(ObLogService* &logservice)
{
  int ret = OB_SUCCESS;
  const ObAddr &self = GCTX.self_addr();
  
  observer::ObSrvNetworkFrame *net_frame = GCTX.net_frame_;
  //log_disk_usage_limit_size cannot be actively obtained from the configuration item, and needs to be passed as a parameter during mtl initialization
  const palf::PalfOptions &palf_options = MTL_INIT_CTX()->palf_options_;
  const char *tenant_clog_dir = MTL_INIT_CTX()->tenant_clog_dir_;
  const char *clog_dir = OB_FILE_SYSTEM_ROUTER.get_clog_dir();
  ObServerLogBlockMgr *log_block_mgr = GCTX.log_block_mgr_;
  common::ObILogAllocator *alloc_mgr = NULL;
  ObNetKeepAliveAdapter *net_keepalive_adapter = NULL;
  if (OB_FAIL(TMA_MGR_INSTANCE.get_tenant_log_allocator(alloc_mgr))) {
    CLOG_LOG(WARN, "get_tenant_log_allocator failed", K(ret));
  } else if (OB_ISNULL(net_keepalive_adapter = MTL_NEW(ObNetKeepAliveAdapter, "logservice"))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    CLOG_LOG(WARN, "alloc memory failed", KR(ret), KP(net_keepalive_adapter));
  } else if (OB_FAIL(logservice->init(palf_options,
                                      tenant_clog_dir,
                                      self,
                                      alloc_mgr,
                                      share::g_mp->ls_service(),
                                      log_block_mgr,
                                      net_keepalive_adapter))) {
    CLOG_LOG(ERROR, "init ObLogService failed", K(ret), K(tenant_clog_dir));
  } else if (OB_FAIL(FileDirectoryUtils::fsync_dir(clog_dir))) {
    CLOG_LOG(ERROR, "fsync_dir failed", K(ret), K(clog_dir));
  } else {
    CLOG_LOG(INFO, "ObLogService mtl_init success");
  }
  if (OB_FAIL(ret) && NULL != net_keepalive_adapter) {
    MTL_DELETE(ObNetKeepAliveAdapter, "logservice", net_keepalive_adapter);
  }
  return ret;
}

void ObLogService::mtl_destroy(ObLogService* &logservice)
{
  common::ob_delete(logservice);
  logservice = nullptr;
  // Free tenant_log_allocator for this tenant after destroy logservice.
  
  int ret = OB_SUCCESS;
  if (OB_FAIL(TMA_MGR_INSTANCE.delete_tenant_log_allocator())) {
    CLOG_LOG(WARN, "delete_tenant_log_allocator failed", K(ret));
  }
}

int ObLogService::start()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(palf_env_->start())) {
    PALF_LOG(WARN, "start palf env failed", K(ret));
  } else if (OB_FAIL(apply_service_.start())) {
    CLOG_LOG(WARN, "failed to start apply_service_", K(ret));
  } else if (OB_FAIL(replay_service_.start())) {
    CLOG_LOG(WARN, "failed to start replay_service_", K(ret));
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
  ls_adapter_.destroy();
  rpc_proxy_.destroy();
  if (NULL != palf_env_) {
    PalfEnv::destroy_palf_env(palf_env_);
    palf_env_ = NULL;
  }
  if (NULL != net_keepalive_adapter_) {
    MTL_DELETE(IObNetKeepAliveAdapter, "logservice", net_keepalive_adapter_);
    net_keepalive_adapter_ = NULL;
  }
  alloc_mgr_ = NULL;
  FLOG_INFO("ObLogService is destroyed");
}

int check_and_prepare_dir(const char *dir)
{
  bool is_exist = false;
  int ret = OB_SUCCESS;
  if (OB_FAIL(common::FileDirectoryUtils::is_exists(dir, is_exist))) {
    CLOG_LOG(WARN, "chcck dir exist failed", K(ret), K(dir));
    // means it's restart
  } else if (is_exist == true) {
    CLOG_LOG(INFO, "director exist", K(ret), K(dir));
    // means it's create tenant
  } else if (OB_FAIL(common::FileDirectoryUtils::create_directory(dir))) {
    CLOG_LOG(WARN, "create_directory failed", K(ret), K(dir));
  } else {
    CLOG_LOG(INFO, "check_and_prepare_dir success", K(ret), K(dir));
  }
  return ret;
}

int ObLogService::init(const PalfOptions &options,
                       const char *base_dir,
                       const common::ObAddr &self,
                       common::ObILogAllocator *alloc_mgr,
                       ObLSService *ls_service,
                       palf::ILogBlockPool *log_block_pool,
                       IObNetKeepAliveAdapter *net_keepalive_adapter)
{
  int ret = OB_SUCCESS;

  
  if (OB_FAIL(check_and_prepare_dir(base_dir))) {
    CLOG_LOG(WARN, "check_and_prepare_dir failed", K(ret), K(base_dir));
  } else if (is_inited_) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(WARN, "ObLogService init twice", K(ret));
  } else if (false == options.is_valid() || OB_ISNULL(base_dir) || OB_UNLIKELY(!self.is_valid())
      || OB_ISNULL(alloc_mgr) || OB_ISNULL(ls_service)
      || OB_ISNULL(log_block_pool) || OB_ISNULL(net_keepalive_adapter)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid arguments", K(ret), K(options), KP(base_dir), K(self),
             KP(alloc_mgr), KP(ls_service), KP(log_block_pool), KP(net_keepalive_adapter));
  } else if (OB_FAIL(PalfEnv::create_palf_env(options, base_dir, self,
                                              alloc_mgr, log_block_pool, &monitor_, &LOCAL_DEVICE_INSTANCE,
                                              &OB_IO_MANAGER, palf_env_))) {
    CLOG_LOG(WARN, "failed to create_palf_env", K(base_dir), K(ret));
  } else if (OB_ISNULL(palf_env_)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "palf_env_ is NULL", K(ret));
  } else if (OB_FAIL(ls_adapter_.init(ls_service))) {
    CLOG_LOG(ERROR, "failed to init ls_adapter", K(ret));
  } else if (OB_FAIL(apply_service_.init(palf_env_, &ls_adapter_))) {
    CLOG_LOG(WARN, "failed to init apply_service", K(ret));
  } else if (OB_FAIL(replay_service_.init(palf_env_, &ls_adapter_, alloc_mgr))) {
    CLOG_LOG(WARN, "failed to init replay_service", K(ret));
  } else if (OB_FAIL(rpc_proxy_.init())) {
    CLOG_LOG(WARN, "LogServiceRpcProxy init failed", K(ret));
  } else {
    net_keepalive_adapter_ = net_keepalive_adapter;
    alloc_mgr_ = alloc_mgr;
    self_ = self;
    is_inited_ = true;
    FLOG_INFO("ObLogService init success", K(ret), K(base_dir), K(self),
        KP(ls_service), K(enable_shared_storage_));
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
    CLOG_LOG(WARN, "create ls failed", K(ret), K(palf_base_info));
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
    CLOG_LOG(WARN, "failed to remove from apply_service", K(ret));
  } else if (OB_FAIL(replay_service_.remove_status())) {
    CLOG_LOG(WARN, "failed to remove from replay_service", K(ret));
  } else {
    // NB: can not execute destroy, otherwise, each interface in log_handler or restore_handler
    // may return OB_NOT_INIT.
    // TODO by runlin: create_ls don't init ObLogHandler and ObLogRestoreHandler.
    //
    // In normal case(for gc), stop has been executed, this stop has no effect.
    // In abnormal case(create ls failed, need remove ls directlly), there is no possibility for dead lock.
    log_handler.stop();
    if (OB_FAIL(palf_env_->remove())) {
      CLOG_LOG(WARN, "failed to remove from palf_env_", K(ret));
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
    CLOG_LOG(WARN, "failed to create apply status", K(ret));
  } else if (OB_FAIL(replay_service_.create_status())) {
    CLOG_LOG(WARN, "failed to create replay status", K(ret));
  } else if (OB_FAIL(log_handler.init(self_, &apply_service_, &replay_service_,
          palf_env_, alloc_mgr_))) {
    CLOG_LOG(WARN, "ObLogHandler init failed", K(ret), KP(palf_env_));
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
    CLOG_LOG(WARN, "failed to get palf_handle", K(ret));
  } else if (FALSE_IT(palf_handle_guard.set(palf_handle, palf_env_))) {
  } else {
    CLOG_LOG(TRACE, "ObLogService open_palf success", K(ret));
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
    CLOG_LOG(WARN, "update_replayable_point failed", K(ret), K(replayable_point));
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
    CLOG_LOG(WARN, "get_replayable_point failed", K(ret), K(replayable_point));
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

int ObLogService::update_palf_options_except_disk_usage_limit_size()
{
  ObSpinLockGuard guard(update_palf_opts_lock_);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    PalfOptions palf_opts;
    if (OB_FAIL(palf_env_->get_options(palf_opts))) {
      CLOG_LOG(WARN, "palf get_options failed", K(ret));
    } else {
      palf_opts.disk_options_.log_disk_utilization_threshold_ = GCONF.log_disk_utilization_threshold;
      palf_opts.disk_options_.log_disk_utilization_limit_threshold_ = GCONF.log_disk_utilization_limit_threshold;
      palf_opts.disk_options_.log_disk_throttling_percentage_ = GCONF.log_disk_throttling_percentage;
      palf_opts.disk_options_.log_disk_throttling_maximum_duration_ = GCONF.log_disk_throttling_maximum_duration;
      palf_opts.enable_log_cache_ = GCONF._enable_log_cache;
      if (OB_FAIL(palf_env_->update_options(palf_opts))) {
        CLOG_LOG(WARN, "palf update_options failed", K(ret), K(palf_opts));
      } else {
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
    CLOG_LOG(WARN, "palf get_options failed", K(ret));
  } else if (FALSE_IT(palf_opts.disk_options_.log_disk_usage_limit_size_ = log_disk_usage_limit_size)) {
  } else if (OB_FAIL(palf_env_->update_options(palf_opts))) {
    CLOG_LOG(WARN, "palf update_options failed", K(ret), K(log_disk_usage_limit_size));
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
    CLOG_LOG(WARN, "failed to open palf", K(ret));
  } else if (OB_FAIL(guard.get_palf_handle()->stat(palf_stat))) {
    CLOG_LOG(WARN, "failed to stat palf", K(ret));
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
    CLOG_LOG(WARN, "check_palf_exist failed", K(ret), K(palf_base_info));
  } else if (palf_exist) {
    ret = OB_ENTRY_EXIST;
    CLOG_LOG(WARN, "palf has eixst", K(ret), K(palf_base_info));
  } else {
    if (OB_FAIL(palf_env_->create(palf::AccessMode::APPEND, palf_base_info, palf_handle))) {
      CLOG_LOG(WARN, "failed to get palf_handle", K(ret));
    } else if (OB_FAIL(apply_service_.create_status())) {
      CLOG_LOG(WARN, "failed to create apply status", K(ret));
    } else if (OB_FAIL(replay_service_.create_status())) {
      CLOG_LOG(WARN, "failed to create replay status", K(ret));
    } else if (OB_FAIL(log_handler.init(self_, &apply_service_, &replay_service_,
          palf_env_, alloc_mgr_))) {
      CLOG_LOG(WARN, "ObLogHandler init failed", K(ret), KP(palf_env_), K(palf_handle));
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
    CLOG_LOG(WARN, "replay_service diagnose failed", K(ret));
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
    CLOG_LOG(WARN, "apply_service diagnose failed", K(ret));
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
    CLOG_LOG(WARN, "palf_env get_io_start_time failed", K(ret));
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
    CLOG_LOG(WARN, "get_disk_usage failed", K(ret));
  } else {
    int64_t unrecyclable_log_disk_size = 0;
    const int64_t CHECKPOINT_PERCENTAGE = GCTX.is_shared_storage_mode() ? 60 : 30;
    if (OB_FAIL(get_unrecyclable_log_disk_size(unrecyclable_log_disk_size))) {
      CLOG_LOG(WARN, "get unrecyclable log disk size failed", K(ret));
    } else {
      need_do_checkpoint = (unrecyclable_log_disk_size * 100 >= total_size * CHECKPOINT_PERCENTAGE);
      CLOG_LOG(TRACE, "check_need_do_checkpoint", K(unrecyclable_log_disk_size), K(total_size), K(need_do_checkpoint));
    }
  }
  return ret;
}

int ObLogService::get_unrecyclable_log_disk_size(int64_t &unrecyclable_log_disk_size)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = share::g_mp->ls_service();
  ObLS *ls = nullptr;
  unrecyclable_log_disk_size = 0;
  if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "ls service is null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    CLOG_LOG(WARN, "get log stream failed", KP(ls_service), K(ret));
  } else {
    ObLogHandler *log_handler = ls->get_log_handler();
    LSN end_lsn;
    LSN base_lsn = ls->get_clog_base_lsn();
    if (OB_FAIL(log_handler->get_end_lsn(end_lsn))) {
      CLOG_LOG(WARN, "get_end_lsn failed", KP(ls), K(base_lsn));
    } else if (end_lsn < base_lsn) {
      ret = OB_ERR_UNEXPECTED;
      CLOG_LOG(WARN, "end_lsn is smaller than base_lsn", K(lbt()), K(end_lsn), K(base_lsn));
    } else {
      unrecyclable_log_disk_size = end_lsn - base_lsn;
    }
  }
  return ret;
}

}//end of namespace logservice
}//end of namespace oceanbase

// ===== definition moved from share/ob_share_util.cpp =====
// removes share→logservice inverted include; declaration remains in share/ob_share_util.h, resolved at link time(transitional state, final state should split the class)
namespace oceanbase
{
namespace share
{

// check_clog_disk_full_or_hang has been demoted to logservice::free function(see end of file)


}  // namespace share
}  // namespace oceanbase

// from share::ObShareUtil demoted(A-setmember split)
namespace oceanbase
{
namespace logservice
{
using namespace oceanbase::share;
int check_clog_disk_full_or_hang(
    bool &clog_disk_is_full,
    bool &clog_disk_is_hang)
{
  int ret = OB_SUCCESS;
  clog_disk_is_full = false;
  clog_disk_is_hang = false;
  int64_t clog_disk_last_working_time = OB_INVALID_TIMESTAMP;
  const int64_t now = ObTimeUtility::current_time();
  bool is_disk_enough = true;
  logservice::ObLogService *log_service = share::g_mp->log_service();
  if (OB_ISNULL(log_service)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(log_service));
  } else if (OB_FAIL(log_service->get_io_start_time(clog_disk_last_working_time))) {
    LOG_WARN("get_io_start_time failed", KR(ret));
  } else if (OB_FAIL(log_service->check_disk_space_enough(is_disk_enough))) {
    LOG_WARN("check_disk_space_enough failed", KR(ret));
  } else {
    clog_disk_is_full = !is_disk_enough;
    clog_disk_is_hang = OB_INVALID_TIMESTAMP != clog_disk_last_working_time
                        && now - clog_disk_last_working_time > GCONF.log_storage_warning_tolerance_time;
  }
  return ret;
}
}
}
