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

#pragma once
#include <sys/stat.h>
#include <sys/types.h>
#include <gmock/gmock.h>
#include <unistd.h>
#define protected public
#define private public
#include "share/rc/ob_tenant_base.h"
#include "lib/restore/ob_io_device.h"
#include "share/rc/ob_module_provider.h"
#include "lib/restore/ob_io_device.h"
#include "lib/file/file_directory_utils.h"
#include "lib/random/ob_mysql_random.h"
#include "lib/objectpool/ob_server_object_pool.h"
#include "logservice/ob_log_service.h"
#include "logservice/palf/palf_options.h"
#include "logservice/ob_server_log_block_mgr.h"
#include "observer/ob_server.h"
#include "observer/ob_service.h"
#include "observer/ob_srv_network_frame.h"
#include "observer/omt/ob_tenant_mtl_helper.h"
#include "observer/omt/ob_tenant.h"
#include "observer/omt/ob_worker_processor.h"
#include "observer/omt/ob_tenant_meta.h"
#include "observer/omt/ob_multi_tenant.h"
#include "observer/omt/ob_tenant_srs.h"
#include "logservice/ob_tenant_mutil_allocator_mgr.h"
#include "share/ob_device_manager.h"
#include "share/ob_io_device_helper.h"
#include "observer/scheduler/ob_tenant_dag_scheduler.h"
#include "observer/scheduler/ob_dag_warning_history_mgr.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_tenant_schema_service.h"
#include "sql/optimizer/stat/ob_opt_stat_monitor_manager.h"
#include "sql/ob_sql.h"
#include "storage/blocksstable/ob_log_file_spec.h"
#include "storage/meta_store/ob_tenant_storage_meta_service.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/compaction/ob_compaction_tablet_diagnose.h"
#include "storage/slog_ckpt/ob_tenant_checkpoint_slog_handler.h"
#include "storage/slog_ckpt/ob_server_checkpoint_slog_handler.h"
#include "storage/meta_mem/ob_tenant_meta_mem_mgr.h"
#include "storage/compaction/ob_tenant_tablet_scheduler.h"
#include "storage/compaction/ob_tenant_medium_checker.h"
#include "storage/slog/ob_storage_logger.h"
#include "storage/compaction/ob_sstable_merge_info_mgr.h"
#include "storage/compaction/ob_tenant_freeze_info_mgr.h"
#include "storage/compaction/ob_compaction_diagnose.h"
#include "storage/compaction/ob_compaction_suggestion.h"
#include "storage/tx_storage/ob_checkpoint_service.h"
#include "storage/tx_storage/ob_tenant_freezer.h"
#include "storage/tx_storage/ob_access_service.h"
#include "storage/lob/ob_lob_manager.h"
#include "storage/tablet/ob_mds_schema_helper.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/tx/ob_trans_service.h"
#include "storage/tx/ob_timestamp_service.h"
#include "storage/tx/ob_trans_id_service.h"
#include "storage/ob_tenant_tablet_stat_mgr.h"
#include "storage/tx/ob_tx_ctx.h"
#include "storage/ob_file_system_router.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "mtlenv/ob_mittest_utils.h"
#include "storage/mock_disk_usage_report.h"
#include "storage/deadlock/ob_deadlock_detector_mgr.h"
#include "storage/ob_relative_table.h"
#include "share/scn.h"
#include "storage/blocksstable/ob_shared_macro_block_manager.h"
#include "storage/multi_data_source/runtime_utility/mds_tenant_service.h"
#include "storage/concurrency_control/ob_multi_version_garbage_collector.h"
#include "storage/tablelock/ob_table_lock_service.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"   // ObSharedMemAllocMgr
#include "storage/tx_storage/ob_tenant_mem_limit_getter.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"
#include "logservice/palf/log_define.h"
#include "storage/access/ob_empty_read_bucket.h"
#include "share/index_usage/ob_index_usage_info_mgr.h"
#include "observer/ob_startup_accel_task_handler.h"
#include "storage/tmp_file/ob_tmp_file_manager.h" // ObTenantTmpFileManager
#include "storage/memtable/ob_lock_wait_mgr.h"
#include "share/roaringbitmap/ob_rb_memory_mgr.h"
#include "observer/omt/ob_tenant_ai_service.h"
#include "share/storage/ob_sqlite_connection_pool.h"
#include "observer/mysql/ob_query_response_time.h"

namespace oceanbase
{
using namespace common;

// Single-tenant seekdb: low-layer modules are owned by the global ObServer
// (OBSERVER) and reached through share::g_mp (ObIModuleProvider). The deleted
// MTL slot mechanism (MTL_ID/MTL_NEW/set<T>/get<T>/MTL_BIND2/MTL(T)) no longer
// exists. This env brings up the real single sys-tenant module set via
// ObTenant::create_tenant_module() -> OBSERVER.obs_construct/init/start_modules(),
// then publishes share::g_mp = &OBSERVER so MTL(T)/g_mp->xxx() resolve.
//
// Compatibility shim for dependents that still write MTL(SomeType *): route the
// type to its ObIModuleProvider getter. Add a specialization here when a new type
// is referenced by a dependent. (Pointer-returning getters, like the real macro.)
namespace mtlenv
{
template <class T> T mtl_get();
} // namespace mtlenv

// MTL(T *) -> mtlenv::mtl_get<T *>() -> share::g_mp->getter()
#ifndef MTL
#define MTL(TYPE) (::oceanbase::mtlenv::mtl_get<TYPE>())
#endif

namespace mtlenv
{
#define MTLENV_DEFINE_GET(TYPE, GETTER)                                  \
  template <> inline TYPE *mtl_get<TYPE *>()                             \
  { return ::oceanbase::share::g_mp->GETTER(); }
MTLENV_DEFINE_GET(storage::ObLSService, ls_service)
MTLENV_DEFINE_GET(storage::ObTenantMetaMemMgr, tenant_meta_mem_mgr)
MTLENV_DEFINE_GET(storage::ObTenantTabletStatMgr, tenant_tablet_stat_mgr)
MTLENV_DEFINE_GET(storage::ObTenantCompactionMemPool, tenant_compaction_mem_pool)
MTLENV_DEFINE_GET(storage::ObAccessService, access_service)
MTLENV_DEFINE_GET(storage::ObTabletMemtableMgrPool, tablet_memtable_mgr_pool)
MTLENV_DEFINE_GET(transaction::ObTransService, trans_service)
MTLENV_DEFINE_GET(tmp_file::ObTenantTmpFileManager, tenant_tmp_file_manager)
MTLENV_DEFINE_GET(logservice::ObLogService, log_service)
MTLENV_DEFINE_GET(compaction::ObTenantSSTableMergeInfoMgr, tenant_ss_table_merge_info_mgr)
MTLENV_DEFINE_GET(share::ObTenantDagScheduler, tenant_dag_scheduler)
MTLENV_DEFINE_GET(share::ObDagWarningHistoryManager, dag_warning_history_manager)
#undef MTLENV_DEFINE_GET
} // namespace mtlenv

namespace storage
{
using namespace transaction;
using namespace logservice;
using namespace concurrency_control;

int64_t ObTenantMetaMemMgr::cal_adaptive_bucket_num()
{
  return 1000;
}

class MockObService : public observer::ObService
{
public:
  MockObService(const oceanbase::observer::ObGlobalContext &gctx):observer::ObService(gctx)
  {}
};

std::string _executeShellCommand(std::string command)
{
  char buffer[256];
  std::string result = "";
  const char * cmd = command.c_str();
  FILE* pipe = popen(cmd, "r");
  if (!pipe) throw std::runtime_error("popen() failed!");
    try {
        while (!feof(pipe))
            if (fgets(buffer, 128, pipe) != NULL)
                result += buffer;
    } catch (...) {
        pclose(pipe);
        throw;
    }
  pclose(pipe);
  return result;
}

common::ObIODevice* get_device_inner(const common::ObString &storage_type_prefix)
{
  int ret = OB_SUCCESS;
  common::ObIODevice* device = NULL;
  const ObStorageIdMod storage_id_mod(0, ObStorageUsedMod::STORAGE_USED_DATA);
  if(OB_FAIL(common::ObDeviceManager::get_local_device(storage_type_prefix, storage_id_mod, device))) {
    STORAGE_LOG(WARN, "get_device_inner", K(ret));
  }
  return device;
}

class MockTenantModuleEnv
{
public:
  MockTenantModuleEnv() : rpc_port_(unittest::get_rpc_port(server_fd_)),
                          mysql_port_(rpc_port_ + 1),
                          self_addr_(ObAddr::IPV4, "127.0.0.1", int32_t(rpc_port_)),
                          net_frame_(GCTX),
                          multi_tenant_(),
                          ob_service_(GCTX),
                          schema_service_(share::schema::ObMultiVersionSchemaService::get_instance()),
                          session_mgr_(),
                          config_(common::ObServerConfig::get_instance()),
                          mock_disk_reporter_(),
                          inited_(false),
                          destroyed_(false)
  {}
  ~MockTenantModuleEnv()
  {
    destroy();
  }
  static MockTenantModuleEnv &get_instance()
  {
    static MockTenantModuleEnv env;
    return env;
  }
  static int construct_default_tenant_meta(const uint64_t tenant_id, omt::ObTenantMeta &meta);

  void init_gctx_gconf();
  int init_before_start_mtl();
  int init();
  int start_();
  int remove_sys_tenant();
  void release_guard();
  void destroy();
  bool is_inited() const { return inited_; }

public:
  static const int64_t TENANT_WORKER_COUNT = 5;

private:
  int init_dir();
  int prepare_io();

private:
  // env
  int64_t rpc_port_;
  int64_t mysql_port_;
  ObAddr self_addr_;
  observer::ObSrvNetworkFrame net_frame_;
  omt::ObMultiTenant multi_tenant_;
  MockObService ob_service_;
  share::schema::ObMultiVersionSchemaService &schema_service_;
  sql::ObSql sql_engine_;
  ObSQLSessionMgr session_mgr_;
  common::ObMysqlRandom scramble_rand_;
  common::ObMySQLProxy sql_proxy_;
  char *curr_dir_;
  std::string run_dir_;
  std::string env_dir_;
  std::string sstable_dir_;
  std::string clog_dir_;
  std::string slog_dir_;
  common::ObServerConfig &config_;
  MockDiskUsageReport mock_disk_reporter_;
  logservice::ObServerLogBlockMgr log_block_mgr_;
  ObLogCursor start_cursor_;
  ObInOutBandwidthThrottle bandwidth_throttle_;
  // param
  palf::PalfDiskOptions disk_options_;

  blocksstable::ObStorageEnv storage_env_;

  // tenant module readiness guard (single tenant)
  share::ObTenantSwitchGuard guard_;

  observer::ObStartupAccelTaskHandler startup_accel_handler_;
  share::ObSQLiteConnectionPool meta_db_pool_;
  share::ObTabletTableOperator tablet_operator_;

  bool inited_;
  bool destroyed_;

  int server_fd_;
};

int MockTenantModuleEnv::remove_sys_tenant()
{
  int ret = OB_SUCCESS;
  guard_.release();
  multi_tenant_.remove_tenant();

  return ret;
}

void MockTenantModuleEnv::release_guard()
{
  guard_.release();
}


int MockTenantModuleEnv::construct_default_tenant_meta(const uint64_t tenant_id, omt::ObTenantMeta &meta)
{
  int ret = OB_SUCCESS;
  // Single-tenant seekdb: ObTenantSuperBlock and ObTenantConfig::init no longer
  // carry a tenant_id. tenant_id is kept in the signature only to name the unit
  // config; the value is the process-level single context id (OB_SERVER_TENANT_ID).
  ObTenantSuperBlock super_block(false/*is_hidden*/);
  share::ObUnitInfoGetter::ObTenantConfig unit;
  uint64_t unit_id = 1000;
  const bool has_memstore = true;
  const int64_t create_timestamp = ObTimeUtility::current_time();

  share::ObUnitConfig unit_config;
  share::ObUnitConfigName name(std::to_string(tenant_id).c_str());
  const uint64_t unit_config_id = 1000;
  share::ObUnitResource ur(
      4, // max_cpu
      2, // min_cpu
      4L << 30, // memory_size
      4L << 30, // log_disk_size
      ObUnitResource::DEFAULT_DATA_DISK_SIZE,    // data_disk_size
      10000, // max_iops
      10000, // min_iops,
      0, //iops_weight
      INT64_MAX, // max_net_bandwidth
      0  /*net_bandwidth_weight*/);
  int64_t hidden_sys_data_disk_config_size = 0;
  if (OB_FAIL(unit_config.init(unit_config_id, name, ur))) {
    STORAGE_LOG(WARN, "fail to init unit config unit", KR(ret), K(unit_config_id), K(name), K(ur));
  } else if (OB_FAIL(unit.init(unit_id,
                        share::ObUnitInfoGetter::ObUnitStatus::UNIT_NORMAL,
                        unit_config,
                        create_timestamp,
                        has_memstore,
                        false /*is_removed*/,
                        hidden_sys_data_disk_config_size,
                        0 /*actual_data_disk_size*/))) {
    STORAGE_LOG(WARN, "fail to init tenant unit", K(ret), K(tenant_id));
  } else if (OB_FAIL(meta.build(unit, super_block))) {
    STORAGE_LOG(WARN, "fail to build tenant meta", K(ret), K(tenant_id));
  }

  return ret;
}


int MockTenantModuleEnv::init_dir()
{
  system(("rm -rf " + run_dir_).c_str());

#ifdef __APPLE__
  char buf[PATH_MAX];
  curr_dir_ = getcwd(buf, sizeof(buf));
#else
  curr_dir_ = getcwd(NULL, 0);
#endif

  int ret = OB_SUCCESS;
  sstable_dir_ = env_dir_ + "/sstable";
  clog_dir_ = env_dir_ + "/clog";
  slog_dir_ = env_dir_ + "/slog";
  if (OB_FAIL(mkdir(run_dir_.c_str(), 0777))) {
  } else if (OB_FAIL(chdir(run_dir_.c_str()))) {
  } else if (OB_FAIL(mkdir("./run", 0777))) {
  } else if (OB_FAIL(mkdir("./etc", 0777))) {
  } else if (OB_FAIL(mkdir(env_dir_.c_str(), 0777))) {
  } else if (OB_FAIL(mkdir(clog_dir_.c_str(), 0777))) {
  } else if (OB_FAIL(mkdir(sstable_dir_.c_str(), 0777))) {
  } else if (OB_FAIL(mkdir(slog_dir_.c_str(), 0777))) {
  }
  // Because the working directory has changed, set to absolute path
  for (int i=0;i<MAX_FD_FILE;i++) {
    int len = strlen(OB_LOGGER.log_file_[i].filename_);
    if (len > 0) {
      std::string ab_file = std::string(curr_dir_) + "/" + std::string(OB_LOGGER.log_file_[i].filename_);
      SERVER_LOG(INFO, "convert ab file", K(ab_file.c_str()));
      MEMCPY(OB_LOGGER.log_file_[i].filename_, ab_file.c_str(), ab_file.size());
    }
  }
  return ret;
}

int MockTenantModuleEnv::prepare_io()
{
  int ret = OB_SUCCESS;

  ObIODOpt iod_opt_array[5];
  ObIODOpts iod_opts;
  iod_opts.opts_ = iod_opt_array;
  int64_t macro_block_count = 5 * 1024;
  int64_t macro_block_size = 64 * 1024;
  char* data_dir = (char*)env_dir_.c_str();
  char file_dir[OB_MAX_FILE_NAME_LENGTH];
  char clog_dir[OB_MAX_FILE_NAME_LENGTH];
  char slog_dir[OB_MAX_FILE_NAME_LENGTH];
  if (OB_FAIL(databuff_printf(file_dir, OB_MAX_FILE_NAME_LENGTH, "%s/sstable/", data_dir))) {
    STORAGE_LOG(WARN, "failed to databuff printf", K(ret));
  } else if (OB_FAIL(databuff_printf(clog_dir, OB_MAX_FILE_NAME_LENGTH, "%s/clog/", data_dir))) {
    STORAGE_LOG(WARN, "failed to gen clog dir", K(ret));
  } else if (OB_FAIL(databuff_printf(slog_dir, OB_MAX_FILE_NAME_LENGTH, "%s/slog/", data_dir))) {
    STORAGE_LOG(WARN, "failed to gen slog dir", K(ret));
  } else if (OB_FAIL(ObDeviceManager::get_instance().init_devices_env())) {
    STORAGE_LOG(WARN, "init device manager failed", KR(ret));
  }

  storage_env_.data_dir_ = data_dir;
  storage_env_.sstable_dir_ = file_dir;
  storage_env_.clog_dir_ = clog_dir;
  storage_env_.default_block_size_ = common::OB_DEFAULT_MACRO_BLOCK_SIZE;
  storage_env_.data_disk_size_ = macro_block_count * common::OB_DEFAULT_MACRO_BLOCK_SIZE;
  storage_env_.data_disk_percentage_ = 0;
  storage_env_.log_disk_size_ = 20 * 1024 * 1024 * 1024ll;
    common::ObString storage_type_prefix(OB_LOCAL_PREFIX);
    share::ObLocalDevice *local_device = static_cast<share::ObLocalDevice*>(get_device_inner(storage_type_prefix));
    // for unifying init/add_device_channel/destroy local_device and local_cache_device code below
    ObIODeviceWrapper::get_instance().set_local_device(local_device);
  iod_opt_array[0].set("data_dir", storage_env_.data_dir_);
  iod_opt_array[1].set("sstable_dir", storage_env_.sstable_dir_);
  iod_opt_array[2].set("block_size", storage_env_.default_block_size_);
  iod_opt_array[3].set("datafile_disk_percentage", storage_env_.data_disk_percentage_);
  iod_opt_array[4].set("datafile_size", storage_env_.data_disk_size_);
  iod_opts.opt_cnt_ = 5;
  ObTenantIOConfig io_config = ObTenantIOConfig::default_instance();
  const int64_t async_io_thread_count = 8;
  const int64_t sync_io_thread_count = 2;
  const int64_t max_io_depth = 256;
  const int64_t bucket_num = 1024L;
  const int64_t max_cache_size = 1024L * 1024L * 512;
  const int64_t block_size = common::OB_MALLOC_BIG_BLOCK_SIZE;
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(ObIOManager::get_instance().init())) {
    STORAGE_LOG(WARN, "fail to init io manager", K(ret));
  } else if (OB_FAIL(LOCAL_DEVICE_INSTANCE.init(iod_opts))) {
    STORAGE_LOG(WARN, "fail to init io device", K(ret), K_(storage_env));
  } else if (OB_FAIL(ObIOManager::get_instance().add_device_channel(&LOCAL_DEVICE_INSTANCE,
                                                                    async_io_thread_count,
                                                                    sync_io_thread_count,
                                                                    max_io_depth))) {
    STORAGE_LOG(WARN, "add device channel failed", K(ret));
  } else if (OB_FAIL(log_block_mgr_.init(storage_env_.clog_dir_))) {
    SERVER_LOG(ERROR, "init log pool fail", K(ret));
  } else if (OB_FAIL(ObIOManager::get_instance().start())) {
    STORAGE_LOG(WARN, "fail to start io manager", K(ret));
  } else if (OB_FAIL(ObKVGlobalCache::get_instance().init(&common::ObTenantMemLimitGetter::get_instance(),
      bucket_num,
      max_cache_size,
      block_size))) {
    STORAGE_LOG(WARN, "fail to init kv global cache ", K(ret));
  } else if (OB_FAIL(OB_STORE_CACHE.init(10))) {
    STORAGE_LOG(WARN, "fail to init OB_STORE_CACHE, ", K(ret));
  } else {
  }
  return ret;
}

void MockTenantModuleEnv::init_gctx_gconf()
{
  GCONF.rpc_port = rpc_port_;
  GCONF.mysql_port = mysql_port_;
  GCONF.self_addr_ = self_addr_;
  GCONF.__min_full_resource_pool_memory = 2 * 1024 * 1024 * 1024ul;
  GCONF.observer_id = 1;
  GCONF.cluster_id = 1;
  GCTX.self_addr_seq_.set_addr(self_addr_);
  GCTX.schema_service_ = &schema_service_;
  GCTX.net_frame_ = &net_frame_;
  GCTX.ob_service_ = &ob_service_;
  GCTX.omt_ = &multi_tenant_;
  GCTX.sql_engine_ = &sql_engine_;
  GCTX.session_mgr_ = &session_mgr_;
  GCTX.scramble_rand_ = &scramble_rand_;
  (void) GCTX.set_server_id(1);
  GCTX.config_ = &config_;
  GCTX.disk_reporter_ = &mock_disk_reporter_;
  GCTX.bandwidth_throttle_ = &bandwidth_throttle_;
  GCTX.log_block_mgr_ = &log_block_mgr_;
  GCTX.startup_accel_handler_ = &startup_accel_handler_;
  GCTX.meta_db_pool_ = &meta_db_pool_;
  GCTX.tablet_operator_ = &tablet_operator_;
  // Single-tenant seekdb: publish ObServer as the module provider. Module
  // instances live on OBSERVER (filled by obs_construct_modules during tenant
  // create); g_mp lets low-layer code and MTL(T) reach them.
  share::g_mp = &OBSERVER;
}

int MockTenantModuleEnv::init_before_start_mtl()
{
  int ret = OB_SUCCESS;
  const int64_t ts_ns = ObTimeUtility::current_time_ns();
  env_dir_ = "./env_" + std::to_string(rpc_port_) + "_" + std::to_string(ts_ns);
  run_dir_ = "./run_" + std::to_string(rpc_port_) + "_" + std::to_string(ts_ns);
  GCONF.cpu_count = 2;
  uint64_t start_time = 10000000;
  scramble_rand_.init(static_cast<uint64_t>(start_time), static_cast<uint64_t>(start_time / 2));
  if (OB_FAIL(init_dir())) {
    STORAGE_LOG(WARN, "fail to init env", K(ret));
  } else if (OB_FAIL(prepare_io())) {
    STORAGE_LOG(WARN, "fail to init env", K(ret));
  } else if (OB_FAIL(session_mgr_.init())) {
    STORAGE_LOG(WARN, "fail to init env", K(ret));
  } else if (OB_FAIL(TMA_MGR_INSTANCE.init())) {
    STORAGE_LOG(WARN, "fail to init env", K(ret));
  } else if (OB_FAIL(OB_FILE_SYSTEM_ROUTER.get_instance().init(env_dir_.c_str(), clog_dir_.c_str()))) {
    STORAGE_LOG(WARN, "fail to init env", K(ret));
  } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.init(2*1024*1024UL))) {
    STORAGE_LOG(WARN, "fail to init server object manager", K(ret));
  } else if (OB_FAIL(net_frame_.init())) {
    STORAGE_LOG(WARN, "net", "ss", _executeShellCommand("ss -antlp").c_str());
    STORAGE_LOG(WARN, "fail to init env", K(ret));
  } else if (OB_FAIL(net_frame_.start())) {
    STORAGE_LOG(WARN, "fail to init env", K(ret));
  } else if (OB_FAIL(startup_accel_handler_.init(observer::SERVER_ACCEL))) {
    STORAGE_LOG(WARN, "init server startup task handler failed", KR(ret));
  } else if (OB_FAIL(SERVER_STORAGE_META_SERVICE.init())) {
    STORAGE_LOG(ERROR, "init server checkpoint slog handler fail", K(ret));
  } else if (OB_FAIL(multi_tenant_.init(self_addr_, &sql_proxy_, false))) {
    STORAGE_LOG(WARN, "fail to init env", K(ret));
  } else if (OB_FAIL(tmp_file::ObTmpBlockCache::get_instance().init("tmp_block_cache"))) {
    STORAGE_LOG(WARN, "init tmp block cache failed", KR(ret));
  } else if (OB_FAIL(tmp_file::ObTmpPageCache::get_instance().init("tmp_page_cache"))) {
    STORAGE_LOG(WARN, "init sn tmp page cache failed", KR(ret));
  } else if (OB_SUCCESS != (ret = bandwidth_throttle_.init(1024 * 1024 * 60))) {
    STORAGE_LOG(ERROR, "failed to init bandwidth_throttle_", K(ret));
  } else if (OB_FAIL(ObMdsSchemaHelper::get_instance().init())) {
    STORAGE_LOG(ERROR, "fail to init mds schema helper", K(ret));
  } else if (OB_FAIL(LOG_IO_DEVICE_WRAPPER.init(clog_dir_.c_str(), 8, 128, &OB_IO_MANAGER, &ObDeviceManager::get_instance()))) {
    STORAGE_LOG(ERROR, "init log_io_device_wrapper fail", KR(ret));
  } else if (OB_FAIL(meta_db_pool_.init("./etc/meta.db"))) {
    STORAGE_LOG(ERROR, "init meta_db_pool_ failed", KR(ret));
  } else if (OB_FAIL(tablet_operator_.init(&meta_db_pool_))) {
    STORAGE_LOG(ERROR, "init tablet_operator_ failed", KR(ret));
  } else {
    GCTX.sql_proxy_ = &sql_proxy_;
    ObRunningModeConfig::instance().mini_mode_ = true; // make startup_accel_handler_ use only one thread
  }
  return ret;
}

int MockTenantModuleEnv::init()
{
    STORAGE_LOG(INFO, "mock env init begin", K(this));
    int ret = OB_SUCCESS;

    if (inited_) {
      ret = OB_INIT_TWICE;
      STORAGE_LOG(ERROR, "init twice", K(ret));
    } else if (OB_FAIL(ObClockGenerator::init())) {
      STORAGE_LOG(ERROR, "init ClockGenerator failed", K(ret));
    } else if (OB_FAIL(ObTabletHandleIndexMap::get_instance()->init())) {
      STORAGE_LOG(ERROR, "init ObTabletHandleIndexMap failed", K(ret));
    } else if (FALSE_IT(init_gctx_gconf())) {
    } else if (OB_FAIL(init_before_start_mtl())) {
      STORAGE_LOG(ERROR, "init_before_start_mtl failed", K(ret));
    } else {
      // Single-tenant seekdb: the module factory registration (MTL_BIND2) is gone;
      // OBSERVER.obs_construct_modules()/obs_init_modules()/obs_start_modules() build
      // the fixed single module set during ObTenant::create_tenant_module(), invoked
      // by convert_hidden_to_real_sys_tenant() in start_().
      oceanbase::ObClusterVersion::get_instance().update_data_version(DATA_CURRENT_VERSION);
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(GMEMCONF.reload_config(config_))) {
      STORAGE_LOG(ERROR, "reload memory config failed", K(ret));
    } else if (OB_FAIL(start_())) {
      STORAGE_LOG(ERROR, "mock env start failed", K(ret));
    } else {
      inited_ = true;
    }

    STORAGE_LOG(INFO, "mock env init finish", K(ret));

    return ret;
}

int MockTenantModuleEnv::start_()
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = OB_SERVER_TENANT_ID;
  omt::ObTenantMeta meta;
  omt::ObTenant *tenant = nullptr;
  int64_t succ_num = 0;


  if (OB_FAIL(log_block_mgr_.start(storage_env_.log_disk_size_))) {
    SERVER_LOG(ERROR, "log pool start failed", KR(ret));
  } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.start(0/*reserved_size*/))) {
    STORAGE_LOG(WARN, "fail to start object manager", K(ret));
  } else if (OB_FAIL(startup_accel_handler_.start())) {
    STORAGE_LOG(WARN, "fail to start server startup task handler", KR(ret));
  } else if (OB_FAIL(SERVER_STORAGE_META_SERVICE.start())) {
    STORAGE_LOG(ERROR, "server storage meta service fail", K(ret));
  } else if (OB_FAIL(multi_tenant_.create_hidden_sys_tenant())) {
    STORAGE_LOG(WARN, "fail to create hidden sys tenant", K(ret));
  } else if (OB_FAIL(construct_default_tenant_meta(tenant_id, meta))) {
    STORAGE_LOG(WARN, "fail to construct_default_tenant_meta", K(ret));
  } else if (OB_FAIL(multi_tenant_.convert_hidden_to_real_sys_tenant(meta.unit_))) {
    STORAGE_LOG(WARN, "fail to create_real_sys_tenant", K(ret));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(multi_tenant_.get_tenant(tenant))) {
    STORAGE_LOG(WARN, "fail to get tenant", K(ret), K(tenant_id));
  } else if (OB_FAIL(tenant->acquire_more_worker(TENANT_WORKER_COUNT, succ_num))) {
  } else if (OB_FAIL(guard_.switch_to(tenant))) { // make module set ready in this thread
    STORAGE_LOG(ERROR, "fail to switch to sys tenant", K(ret));
  }
  return ret;
}

void MockTenantModuleEnv::destroy()
{
  STORAGE_LOG(INFO, "destroy", K(destroyed_));

  if (server_fd_ > 0) {
    close(server_fd_);
  }
  if (destroyed_) {
    return;
  }
  // Release tenant module readiness
  guard_.release();

  startup_accel_handler_.destroy();

  multi_tenant_.stop();
  multi_tenant_.wait();
  multi_tenant_.destroy();
  ObKVGlobalCache::get_instance().destroy();
  SERVER_STORAGE_META_SERVICE.destroy();

  OB_STORAGE_OBJECT_MGR.stop();
  OB_STORAGE_OBJECT_MGR.wait();
  OB_STORAGE_OBJECT_MGR.destroy();

  net_frame_.sql_nio_stop();
  net_frame_.stop();
  net_frame_.wait();
  net_frame_.destroy();
  tmp_file::ObTmpBlockCache::get_instance().destroy();
  tmp_file::ObTmpPageCache::get_instance().destroy();
  share::g_mp = nullptr;

  destroyed_ = true;

  chdir(curr_dir_);
  system(("rm -rf " + run_dir_).c_str());
}

} // namespace storage

// just for override HOOK
namespace transaction
{
int ObGtiSource::get_trans_id(int64_t &trans_id)
{
  static int64_t trans_id_start = 1000;
  trans_id = ATOMIC_FAA(&trans_id_start, 1 );
  return OB_SUCCESS;
}

} // end transaction

} // namespace oceanbase
