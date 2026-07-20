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
#include "storage/tx_storage/ob_tenant_freezer.h"  // previously hidden behind the allocator_mgr.h include chain, make the dependency explicit
#include "share/ob_encryption_util.h"  // ObTdeEncryptEngineLoader(moved from config_manager)
#include "share/rc/ob_module_provider.h"
#include "lib/alloc/ob_malloc_sample_struct.h"
#include "lib/allocator/ob_mem_leak_checker.h"
#include "share/ob_resource_limit.h"
#include "observer/ob_server.h"
#include "observer/ob_server_utils.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"
#include "storage/compaction/ob_tenant_tablet_scheduler.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "rpc/frame/ob_net_consts.h"
#include "rpc/frame/ob_req_packet_code.h"  // rpc::frame::ObReqCheckSumCheckLevel (relocated)

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::storage;
using namespace oceanbase::share;

namespace oceanbase
{
namespace observer
{

int set_cluster_name_hash(const ObString &cluster_name)
{
  int ret = OB_SUCCESS;
  uint64_t cluster_name_hash = 0/*INVALID_CLUSTER_NAME_HASH*/;

  if (OB_FAIL(calc_cluster_name_hash(cluster_name, cluster_name_hash))) {
    LOG_WARN("failed to calc_cluster_name_hash", KR(ret), K(cluster_name));
  } else {
    rpc::frame::ObNetConsts::CLUSTER_NAME_HASH = cluster_name_hash;
    LOG_INFO("set cluster_name_hash", KR(ret), K(cluster_name), K(cluster_name_hash));
  }
  return ret;
}

int calc_cluster_name_hash(const ObString &cluster_name, uint64_t &cluster_name_hash)
{
  int ret = OB_SUCCESS;
  cluster_name_hash = 0/*INVALID_CLUSTER_NAME_HASH*/;

  if (0 == cluster_name.length()) {
    cluster_name_hash = 0/*INVALID_CLUSTER_NAME_HASH*/;
    LOG_INFO("set cluster_name_hash to invalid", K(cluster_name));
  } else {
    cluster_name_hash = common::murmurhash(cluster_name.ptr(), cluster_name.length(), 0);
    LOG_INFO("calc cluster_name_hash for rpc", K(cluster_name), K(cluster_name_hash));
  }

  return ret;
}
}
}
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
    if (OB_TMP_FAIL(ObClusterVersion::get_instance().reload_config())) {
      LOG_WARN("cluster version reload config failed", K(tmp_ret));
    }

    if (OB_TMP_FAIL(OBSERVER.reload_config())) {
      LOG_WARN("reload configuration for ob service fail", K(tmp_ret));
    }
    if (OB_TMP_FAIL(OBSERVER.get_net_frame().reload_config())) {
      LOG_WARN("reload configuration for net frame fail", K(tmp_ret));
    }
    if (OB_TMP_FAIL(OBSERVER.get_net_frame().reload_ssl_config())) {
      LOG_WARN("reload ssl config for net frame fail", K(tmp_ret));
    }

    if (OB_TMP_FAIL(ObTdeEncryptEngineLoader::get_instance().reload_config())) {
      LOG_WARN("reload config for tde encrypt engine fail", K(tmp_ret));
    }
    if (OB_TMP_FAIL(ObSrvNetworkFrame::reload_rpc_auth_method())) {
      LOG_WARN("reload config for rpc auth method fail", K(tmp_ret));
    }

  }
  {
    enable_malloc_v2(GCONF._enable_malloc_v2);
    GMEMCONF.reload_config(GCONF);
    OB_LOGGER.set_info_as_wdiag(false);
    // reload log config again after get MIN_CLUSTER_VERSION
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


      reload_tenant_freezer_config_();
      reload_tenant_scheduler_config_();
      if (OB_NOT_NULL(GCTX.omt_)) {
        GCTX.omt_->reload_tenant_task_queue_size();
      }
  }

  int64_t cache_size = GCONF.memory_chunk_cache_size;
  bool use_large_chunk_cache = false;
  if (0 == cache_size || 1 == cache_size) {
    cache_size = GMEMCONF.get_server_memory_limit();
    if (cache_size >= (32L<<30)) {
      cache_size -= (4L<<30);
    }
  }
  lib::AChunkMgr::instance().set_max_chunk_cache_size(cache_size, use_large_chunk_cache);

    // Refresh cluster_name_hash for non arbitration mode
    if (FAILEDx(set_cluster_name_hash(GCONF.cluster.str()))) {
      LOG_WARN("failed to set_cluster_name_hash", KR(ret), "cluster_name", GCONF.cluster.str(),
                                                "cluster_name_len", strlen(GCONF.cluster.str()));
    }

  // reset mem leak
  {
    static common::ObMemLeakChecker::TCharArray last_value;
    static bool do_once __attribute__((unused)) = [&]() {
                            STRNCPY(&last_value[0], GCONF.leak_mod_to_check.str(), sizeof(last_value));
                            return false;
                          }();
    if (0 == STRNCMP(last_value, GCONF.leak_mod_to_check.str(), sizeof(last_value))) {
      // At the end of the observer startup, the config will be reloaded once. If the status is not judged, the trace caught during the startup process will be flushed.
      // do-nothing
    } else {
      reset_mem_leak_checker_label(GCONF.leak_mod_to_check.str());

      STRNCPY(last_value, GCONF.leak_mod_to_check.str(), sizeof(last_value));
      last_value[sizeof(last_value) - 1] = '\0';
    }
  }

#ifndef ENABLE_SANITY
  {
    ObMallocAllocator::get_instance()->force_explict_500_malloc_ =
      GCONF._force_explict_500_malloc;
  }
#else
  {
    sanity_set_whitelist(GCONF.sanity_whitelist.str());
    ObMallocAllocator::get_instance()->enable_tenant_leak_memory_protection_ =
      GCONF._enable_tenant_leak_memory_protection;
  }
#endif
  {
    ObResourceLimit rl;
    int tmp_ret = rl.load_config(GCONF._resource_limit_spec.str());
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN("load _resource_limit_spec failed", K(tmp_ret), K(GCONF._resource_limit_spec.str()));
    } else {
      LOG_INFO("load _resource_limit_spec succeed", "origin", RL_CONF, "current", rl,
               K(GCONF._resource_limit_spec.str()));
      RL_CONF.assign(rl);
    }
    ObResourceLimit::IS_ENABLED = GCONF._enable_resource_limit_spec;
  }

  {
    auto new_level = rpc::frame::get_rpc_checksum_check_level_from_string(GCONF._rpc_checksum.str());
    auto orig_level = rpc::frame::get_rpc_checksum_check_level();
    if (new_level != orig_level) {
      LOG_INFO("rpc_checksum_check_level changed",
               "orig", orig_level,
               "new", new_level);
    }
    rpc::frame::set_rpc_checksum_check_level(new_level);
  }

    auto new_upgrade_stage = obcall::get_upgrade_stage(GCONF._upgrade_stage.str());
    auto orig_upgrade_stage = GCTX.get_upgrade_stage();
    if (new_upgrade_stage != orig_upgrade_stage) {
      LOG_INFO("_upgrade_stage changed", K(new_upgrade_stage), K(orig_upgrade_stage));
    }
    (void)GCTX.set_upgrade_stage(new_upgrade_stage);

  // syslog bandwidth limitation
  share::ObTaskController::get().set_log_rate_limit(
      GCONF.syslog_io_bandwidth_limit.get_value());
  share::ObTaskController::get().set_diag_per_error_limit(
      GCONF.diag_syslog_per_error_limit.get_value());

  lib::g_runtime_enabled = true;

    common::ObKVGlobalCache::get_instance().reload_wash_interval();
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

  {
    ObMallocAllocator::get_instance()->force_malloc_for_absent_tenant_ = GCONF._force_malloc_for_absent_tenant;
  }

  // moved from share ObConfigManager::reload_config(share base must not touch observer components;
  // this function is the original reload_config_func_ call site,order and fail-fast semantics are preserved)
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(OBSERVER.get_net_frame().reload_ssl_config())) {
    LOG_WARN("reload ssl config for net frame fail", K(ret));
  } else if (OB_FAIL(OBSERVER.get_net_frame().reload_sql_thread_config())) {
    LOG_WARN("reload config for mysql login thread count failed", K(ret));
  } else if (OB_FAIL(ObTdeEncryptEngineLoader::get_instance().reload_config())) {
    LOG_WARN("reload config for tde encrypt engine fail", K(ret));
  } else if (OB_FAIL(GCTX.omt_->update_hidden_sys_tenant())) {
    LOG_WARN("update hidden sys tenant failed", K(ret));
  }
  return ret;
}

void ObServerReloadConfig::reload_tenant_scheduler_config_()
{
  (void) share::g_mp->tenant_dag_scheduler()->reload_config();
  (void) share::g_mp->tenant_tablet_scheduler()->reload_tenant_config();
}


void ObServerReloadConfig::reload_tenant_freezer_config_()
{
  // NOTICE: tenant freezer should update before ObSharedMemAllocMgr.
  share::g_mp->tenant_freezer()->reload_config();
  share::g_mp->shared_mem_alloc_mgr()->update_throttle_config();
}
