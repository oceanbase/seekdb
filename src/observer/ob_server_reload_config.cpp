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

#define USING_LOG_PREFIX SERVER

#include "config_bridge.h"
#include "ob_server_reload_config.h"
#include "storage/tx_storage/ob_memstore_freezer.h"  // previously hidden behind the allocator_mgr.h include chain, make the dependency explicit
#include "share/rc/ob_server_runtime.h"
#include "observer/ob_server.h"
#include "observer/ob_server_utils.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"
#include "storage/compaction/ob_tablet_scheduler.h"
#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include <atomic>

#include <string>

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::storage;
using namespace oceanbase::share;

namespace
{
void warn_deprecated_allocator_parameters_once()
{
  static std::atomic<bool> warned(false);
  if (!warned.exchange(true, std::memory_order_acq_rel)) {
    LOG_WARN_RET(OB_SUCCESS, "cache_wash_threshold is deprecated and has no effect",
        "value", oceanbase::config::cache_wash_threshold());
    LOG_WARN_RET(OB_SUCCESS, "memory_chunk_cache_size is deprecated and has no effect",
        "value", oceanbase::config::memory_chunk_cache_size());
    LOG_WARN_RET(OB_SUCCESS, "_min_malloc_sample_interval is deprecated and has no effect",
        "value", oceanbase::config::_min_malloc_sample_interval());
    LOG_WARN_RET(OB_SUCCESS, "_max_malloc_sample_interval is deprecated and has no effect",
        "value", oceanbase::config::_max_malloc_sample_interval());
    LOG_WARN_RET(OB_SUCCESS, "_ctx_memory_limit is deprecated and has no effect",
        "value", oceanbase::config::_ctx_memory_limit().c_str());
    LOG_WARN_RET(OB_SUCCESS, "_enable_memleak_light_backtrace is deprecated and has no effect",
        "value", oceanbase::config::_enable_memleak_light_backtrace());
  }
}
} // namespace

ObServerReloadConfig::ObServerReloadConfig(ObGlobalContext &gctx)
  : gctx_(gctx)
{
}

ObServerReloadConfig::~ObServerReloadConfig()
{

}

int ObServerReloadConfig::operator()()
{
  int tmp_ret = OB_SUCCESS;
  int ret = tmp_ret;
  warn_deprecated_allocator_parameters_once();

  if (!gctx_.is_inited()) {
    ret = tmp_ret = OB_INNER_STAT_ERROR;
  } else {
    if (OB_TMP_FAIL(ObReloadConfig::operator()())) {
    }
    if (OB_TMP_FAIL(OBSERVER.reload_config())) {
    }
    if (OB_TMP_FAIL(OBSERVER.get_net_frame().reload_config())) {
    }

  }
  {
    GMEMCONF.reload_config();
    OB_LOGGER.set_info_as_wdiag(false);
    // Reload log configuration after applying the latest configuration values.
    if (OB_TMP_FAIL(ObReloadConfig::operator()())) {
    }
      ObIOConfig io_config;
      int64_t cpu_cnt = config::cpu_count();
      if (cpu_cnt <= 0) {
        cpu_cnt = common::get_cpu_num();
      }
      io_config.disk_io_thread_count_ = config::disk_io_thread_count();
      io_config.sync_io_thread_count_ = config::sync_io_thread_count();
      // In the 2.x version, reuse the sys_bkgd_io_timeout configuration item to indicate the data disk io timeout time
      // After version 3.1, use the data_storage_io_timeout configuration item.
      io_config.data_storage_io_timeout_ms_ = config::_data_storage_io_timeout() / 1000L;
      io_config.data_storage_warning_tolerance_time_ = config::data_storage_warning_tolerance_time();
      if (OB_TMP_FAIL(ObIOManager::get_instance().set_io_config(io_config))) {
      }

      (void)reload_diagnose_info_config(config::enable_perf_event());
      (void)reload_trace_log_config(config::enable_record_trace_log());


      reload_memstore_freezer_config_();
      reload_scheduler_config_();
      storage::ObStorageMetaMemMgr *meta_memory_mgr =
          share::server_service<storage::ObStorageMetaMemMgr>();
      if (OB_NOT_NULL(meta_memory_mgr)) {
        meta_memory_mgr->refresh_memory_quota_limit();
      }
      if (OB_NOT_NULL(::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>())) {
        ::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>()->reload_request_queue_size();
      }
  }

  // syslog bandwidth limitation
  share::ObTaskController::get().set_log_rate_limit(
      config::syslog_io_bandwidth_limit());
  share::ObTaskController::get().set_diag_per_error_limit(
      config::diag_syslog_per_error_limit());

  lib::g_runtime_enabled = true;

    common::ObKVGlobalCache::get_instance().reload_config(
        common::ObKVCacheRuntimeOptions(
            config::_cache_wash_interval(),
            GMEMCONF.get_kvcache_memory_limit()));
    int64_t data_disk_size = 0;
    int64_t data_disk_percentage = 0;
    int64_t reserved_size = 0;
    if (OB_TMP_FAIL(ObServerUtils::get_data_disk_info_in_config(data_disk_size,
                                                                data_disk_percentage))) {
    } else if (OB_TMP_FAIL(SERVER_STORAGE_META_SERVICE.get_reserved_size(reserved_size))) {
    } else if (OB_TMP_FAIL(OB_STORAGE_OBJECT_MGR.resize_local_device(
        OB_STORAGE_OBJECT_MGR.get_total_macro_block_count()
            * OB_STORAGE_OBJECT_MGR.get_macro_block_size(),
        data_disk_size, data_disk_percentage, reserved_size))) {
    }

  {
    static const std::string data_dir(config::data_dir().c_str());
    ObSysVariables::set_value("datadir", data_dir.c_str());
  }

  {
    common::g_enable_backtrace = config::_enable_backtrace_function();
  }

  // moved from share ObConfigManager::reload_config(share base must not touch observer components;
  // this function is the original reload_config_func_ call site,order and fail-fast semantics are preserved)
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>()->refresh_runtime_resources())) {
  }
  return ret;
}

void ObServerReloadConfig::reload_scheduler_config_()
{
  (void) ::oceanbase::share::server_service<::oceanbase::share::ObDagScheduler>()->reload_config();
  (void) ::oceanbase::share::server_service<::oceanbase::compaction::ObTabletScheduler>()->reload_runtime_config();
}


void ObServerReloadConfig::reload_memstore_freezer_config_()
{
  // The memstore freezer must be updated before ObSharedMemAllocMgr.
  ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>()->reload_config();
  share::ObSharedMemAllocMgr *shared_memory_mgr =
      share::server_service<share::ObSharedMemAllocMgr>();
  if (OB_NOT_NULL(shared_memory_mgr)) {
    shared_memory_mgr->update_throttle_config();
    shared_memory_mgr->vector_allocator().refresh_memory_quota_limit();
  }
}
