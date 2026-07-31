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

#include "ob_server_reload_config.h"
#include "storage/tx_storage/ob_memstore_freezer.h"  // previously hidden behind the allocator_mgr.h include chain, make the dependency explicit
#include "share/rc/ob_server_runtime.h"
#include "lib/alloc/ob_malloc_sample_struct.h"
#include "observer/ob_server.h"
#include "observer/ob_server_utils.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"
#include "storage/compaction/ob_tablet_scheduler.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::storage;
using namespace oceanbase::share;

ObServerReloadConfig::ObServerReloadConfig(ObServerConfig &config, ObGlobalContext &gctx)
  : ObReloadConfig(&config),
    gctx_(gctx)
{
}

ObServerReloadConfig::~ObServerReloadConfig()
{

}

int ObServerReloadConfig::operator()()
{
  int tmp_ret = OB_SUCCESS;
  int ret = tmp_ret;

  if (!gctx_.is_inited()) {
    ret = tmp_ret = OB_INNER_STAT_ERROR;
    LOG_WARN("gctx not init", "gctx inited", gctx_.is_inited(), K(tmp_ret));
  } else {
    if (OB_TMP_FAIL(ObReloadConfig::operator()())) {
      LOG_WARN("ObReloadConfig operator() failed", K(tmp_ret));
    }
    if (OB_TMP_FAIL(OBSERVER.reload_config())) {
      LOG_WARN("reload configuration for ob service fail", K(tmp_ret));
    }
    if (OB_TMP_FAIL(OBSERVER.get_net_frame().reload_config())) {
      LOG_WARN("reload configuration for net frame fail", K(tmp_ret));
    }

  }
  {
    GMEMCONF.reload_config(GCONF);
    OB_LOGGER.set_info_as_wdiag(false);
    // Reload log configuration after applying the latest configuration values.
    if (OB_TMP_FAIL(ObReloadConfig::operator()())) {
      LOG_WARN("ObReloadConfig operator() failed", K(tmp_ret));
    }
    const int64_t reserved_memory = GCONF.cache_wash_threshold;
    LOG_INFO("set reserved memory", K(reserved_memory));
    ob_set_reserved_memory(reserved_memory);
    ObMallocSampleLimiter::set_interval(GCONF._max_malloc_sample_interval,
                                     GCONF._min_malloc_sample_interval);
    enable_memleak_light_backtrace(GCONF._enable_memleak_light_backtrace);
      ObIOConfig io_config;
      int64_t cpu_cnt = GCONF.cpu_count;
      if (cpu_cnt <= 0) {
        cpu_cnt = common::get_cpu_num();
      }
      io_config.disk_io_thread_count_ = GCONF.disk_io_thread_count;
      io_config.sync_io_thread_count_ = GCONF.sync_io_thread_count;
      // In the 2.x version, reuse the sys_bkgd_io_timeout configuration item to indicate the data disk io timeout time
      // After version 3.1, use the data_storage_io_timeout configuration item.
      io_config.data_storage_io_timeout_ms_ = GCONF._data_storage_io_timeout / 1000L;
      io_config.data_storage_warning_tolerance_time_ = GCONF.data_storage_warning_tolerance_time;
      if (OB_TMP_FAIL(ObIOManager::get_instance().set_io_config(io_config))) {
        LOG_WARN("reload io manager config fail, ", K(tmp_ret));
      }

      (void)reload_diagnose_info_config(GCONF.enable_perf_event);
      (void)reload_trace_log_config(GCONF.enable_record_trace_log);


      reload_memstore_freezer_config_();
      reload_scheduler_config_();
      if (OB_NOT_NULL(::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>())) {
        ::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>()->reload_request_queue_size();
      }
  }

  int64_t cache_size = GCONF.memory_chunk_cache_size;
  bool use_large_chunk_cache = false;
  if (0 == cache_size || 1 == cache_size) {
    cache_size = lib::AChunkMgr::get_default_max_chunk_cache_size();
  }
  lib::AChunkMgr::instance().set_max_chunk_cache_size(cache_size, use_large_chunk_cache);

  // syslog bandwidth limitation
  share::ObTaskController::get().set_log_rate_limit(
      GCONF.syslog_io_bandwidth_limit.get_value());
  share::ObTaskController::get().set_diag_per_error_limit(
      GCONF.diag_syslog_per_error_limit.get_value());

  lib::g_runtime_enabled = true;

    common::ObKVGlobalCache::get_instance().reload_config(
        common::ObKVCacheRuntimeOptions(
            GCONF._cache_wash_interval));
    int64_t data_disk_size = 0;
    int64_t data_disk_percentage = 0;
    int64_t reserved_size = 0;
    if (OB_TMP_FAIL(ObServerUtils::get_data_disk_info_in_config(data_disk_size,
                                                                data_disk_percentage))) {
      LOG_ERROR("cal_all_part_disk_size failed", KR(tmp_ret));
    } else if (OB_TMP_FAIL(SERVER_STORAGE_META_SERVICE.get_reserved_size(reserved_size))) {
      LOG_WARN("fail to get reserved size", KR(tmp_ret), K(reserved_size));
    } else if (OB_TMP_FAIL(OB_STORAGE_OBJECT_MGR.resize_local_device(
        OB_STORAGE_OBJECT_MGR.get_total_macro_block_count()
            * OB_STORAGE_OBJECT_MGR.get_macro_block_size(),
        data_disk_size, data_disk_percentage, reserved_size))) {
      LOG_WARN("fail to resize file", KR(tmp_ret),
          K(data_disk_size), K(data_disk_percentage), K(reserved_size));
    }

  {
    ObSysVariables::set_value("datadir", GCONF.data_dir);
  }

  {
    common::g_enable_backtrace = GCONF._enable_backtrace_function;
  }

  // moved from share ObConfigManager::reload_config(share base must not touch observer components;
  // this function is the original reload_config_func_ call site,order and fail-fast semantics are preserved)
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>()->refresh_runtime_resources())) {
    LOG_WARN("refresh server runtime resources failed", K(ret));
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
  ::oceanbase::share::server_service<::oceanbase::share::ObSharedMemAllocMgr>()->update_throttle_config();
}
