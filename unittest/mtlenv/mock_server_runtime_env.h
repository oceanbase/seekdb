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
#include "share/rc/ob_server_runtime.h"
#include "lib/restore/ob_io_device.h"
#include "share/rc/ob_module_provider.h"
#include "lib/file/file_directory_utils.h"
#include "lib/random/ob_mysql_random.h"
#include "lib/objectpool/ob_server_object_pool.h"
#include "logservice/ob_log_service.h"
#include "logservice/palf/palf_options.h"
#include "logservice/ob_server_log_block_mgr.h"
#include "observer/ob_server.h"
#include "observer/ob_service.h"
#include "observer/ob_srv_network_frame.h"
#include "observer/omt/ob_worker_processor.h"
#include "observer/omt/ob_server_runtime.h"
#include "observer/omt/ob_server_runtime_controller.h"
#include "observer/omt/ob_srs_service.h"
#include "logservice/ob_log_allocator_mgr.h"
#include "share/ob_device_manager.h"
#include "share/ob_io_device_helper.h"
#include "share/ob_internal_table_change_notifier.h"
#include "share/resource/ob_server_runtime_config.h"
#include "observer/scheduler/ob_dag_scheduler.h"
#include "observer/scheduler/ob_dag_warning_history_mgr.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_runtime_service.h"
#include "sql/optimizer/stat/ob_opt_stat_monitor_manager.h"
#include "sql/ob_sql.h"
#include "storage/blocksstable/ob_log_file_spec.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/compaction/ob_compaction_tablet_diagnose.h"
#include "storage/slog_ckpt/ob_local_storage_checkpoint_slog_handler.h"
#include "storage/slog_ckpt/ob_server_checkpoint_slog_handler.h"
#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"
#include "storage/compaction/ob_tablet_scheduler.h"
#include "storage/compaction/ob_medium_checker.h"
#include "storage/slog/ob_storage_logger.h"
#include "storage/compaction/ob_sstable_merge_info_mgr.h"
#include "storage/compaction/ob_freeze_info_mgr.h"
#include "storage/compaction/ob_compaction_diagnose.h"
#include "storage/compaction/ob_compaction_suggestion.h"
#include "storage/tx_storage/ob_checkpoint_service.h"
#include "storage/tx_storage/ob_memstore_freezer.h"
#include "storage/tx_storage/ob_access_service.h"
#include "storage/lob/ob_lob_manager.h"
#include "storage/tablet/ob_mds_schema_helper.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/tx/ob_trans_service.h"
#include "storage/tx/ob_timestamp_service.h"
#include "storage/tx/ob_trans_id_service.h"
#include "storage/ob_tablet_stat_mgr.h"
#include "storage/tx/ob_tx_ctx.h"
#include "storage/ob_file_system_router.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "mtlenv/ob_mittest_utils.h"
#include "storage/mock_disk_usage_report.h"
#include "storage/deadlock/ob_deadlock_detector_mgr.h"
#include "storage/ob_relative_table.h"
#include "share/scn.h"
#include "storage/multi_data_source/runtime_utility/mds_service.h"
#include "storage/concurrency_control/ob_multi_version_garbage_collector.h"
#include "storage/tablelock/ob_table_lock_service.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"   // ObSharedMemAllocMgr
#include "storage/tx_storage/ob_server_mem_limit_getter.h"
#include "logservice/palf/log_define.h"
#include "storage/access/ob_empty_read_bucket.h"
#include "observer/ob_startup_accel_task_handler.h"
#include "storage/tmp_file/ob_tmp_file_manager.h" // ObTmpFileManager
#include "storage/memtable/ob_lock_wait_mgr.h"
#include "share/roaringbitmap/ob_rb_memory_mgr.h"
#include "observer/omt/ob_ai_service.h"
#include "share/storage/ob_sqlite_connection_pool.h"

namespace oceanbase
{
using namespace common;

// Low-layer modules are owned by the global ObServer (OBSERVER) and reached
// through share::g_mp (ObIModuleProvider). This env brings up the real server
// runtime module set, then publishes share::g_mp = &OBSERVER so the test shim
// and production provider calls resolve to those modules.
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
MTLENV_DEFINE_GET(storage::ObStorageMetaMemMgr, storage_meta_mem_mgr)
MTLENV_DEFINE_GET(storage::ObTabletStatMgr, tablet_stat_mgr)
MTLENV_DEFINE_GET(storage::ObCompactionMemPool, compaction_mem_pool)
MTLENV_DEFINE_GET(storage::ObAccessService, access_service)
MTLENV_DEFINE_GET(storage::ObTabletMemtableMgrPool, tablet_memtable_mgr_pool)
MTLENV_DEFINE_GET(transaction::ObTransService, trans_service)
MTLENV_DEFINE_GET(tmp_file::ObTmpFileManager, tmp_file_manager)
MTLENV_DEFINE_GET(logservice::ObLogService, log_service)
MTLENV_DEFINE_GET(compaction::ObSSTableMergeInfoMgr, sstable_merge_info_mgr)
MTLENV_DEFINE_GET(share::ObDagScheduler, dag_scheduler)
MTLENV_DEFINE_GET(share::ObDagWarningHistoryManager, dag_warning_history_manager)
#undef MTLENV_DEFINE_GET
} // namespace mtlenv

namespace storage
{
using namespace transaction;
using namespace logservice;
using namespace concurrency_control;

int64_t ObStorageMetaMemMgr::cal_adaptive_bucket_num()
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

class MockServerRuntimeEnv
{
public:
  MockServerRuntimeEnv() : rpc_port_(unittest::get_rpc_port(server_fd_)),
                          mysql_port_(rpc_port_ + 1),
                          self_addr_(ObAddr::IPV4, "127.0.0.1", int32_t(rpc_port_)),
                          net_frame_(GCTX),
                          runtime_controller_(),
                          ob_service_(GCTX),
                          schema_service_(share::schema::ObMultiVersionSchemaService::get_instance()),
                          session_mgr_(),
                          config_(common::ObServerConfig::get_instance()),
                          mock_disk_reporter_(),
                          inited_(false),
                          destroyed_(false)
  {}
  ~MockServerRuntimeEnv()
  {
    destroy();
  }
  static MockServerRuntimeEnv &get_instance()
  {
    static MockServerRuntimeEnv env;
    return env;
  }

  void init_gctx_gconf();
  int init_before_start_runtime();
  int init();
  int start_();
  void destroy();
  bool is_inited() const { return inited_; }

public:
  static const int64_t RUNTIME_WORKER_COUNT = 5;

private:
  int init_dir();
  int prepare_io();

private:
  // env
  int64_t rpc_port_;
  int64_t mysql_port_;
  ObAddr self_addr_;
  observer::ObSrvNetworkFrame net_frame_;
  omt::ObServerRuntimeController runtime_controller_;
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

  observer::ObStartupAccelTaskHandler startup_accel_handler_;
  share::ObSQLiteConnectionPool meta_db_pool_;
  share::ObTabletTableOperator tablet_operator_;

  bool inited_;
  bool destroyed_;

  int server_fd_;
};

int MockServerRuntimeEnv::init_dir()
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

int MockServerRuntimeEnv::prepare_io()
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
  ObIOServiceConfig io_config = ObIOServiceConfig::default_instance();
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
  } else if (OB_FAIL(ObKVGlobalCache::get_instance().init(&common::ObServerMemLimitGetter::get_instance(),
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

void MockServerRuntimeEnv::init_gctx_gconf()
{
  GCONF.rpc_port = rpc_port_;
  GCONF.mysql_port = mysql_port_;
  GCONF.self_addr_ = self_addr_;
  GCTX.self_addr_seq_.set_addr(self_addr_);
  GCTX.schema_service_ = &schema_service_;
  GCTX.net_frame_ = &net_frame_;
  GCTX.ob_service_ = &ob_service_;
  GCTX.server_runtime_controller_ = &runtime_controller_;
  GCTX.sql_engine_ = &sql_engine_;
  GCTX.session_mgr_ = &session_mgr_;
  GCTX.scramble_rand_ = &scramble_rand_;
  GCTX.config_ = &config_;
  GCTX.disk_reporter_ = &mock_disk_reporter_;
  GCTX.bandwidth_throttle_ = &bandwidth_throttle_;
  GCTX.log_block_mgr_ = &log_block_mgr_;
  GCTX.startup_accel_handler_ = &startup_accel_handler_;
  GCTX.meta_db_pool_ = &meta_db_pool_;
  GCTX.tablet_operator_ = &tablet_operator_;
  // Publish ObServer as the module provider. Module instances live on OBSERVER
  // and are filled by obs_construct_modules during runtime creation.
  share::g_mp = &OBSERVER;
}

int MockServerRuntimeEnv::init_before_start_runtime()
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
  } else if (OB_FAIL(LOG_ALLOCATOR_MGR_INSTANCE.init())) {
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
  } else if (OB_FAIL(startup_accel_handler_.init())) {
    STORAGE_LOG(WARN, "init server startup task handler failed", KR(ret));
  } else if (OB_FAIL(
      share::ObInternalTableChangeNotifier::get_instance().init())) {
    STORAGE_LOG(ERROR, "init internal table change notifier failed", KR(ret));
  } else if (OB_FAIL(SERVER_STORAGE_META_SERVICE.init())) {
    STORAGE_LOG(ERROR, "init server checkpoint slog handler fail", K(ret));
  } else if (OB_FAIL(runtime_controller_.init())) {
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

int MockServerRuntimeEnv::init()
{
    STORAGE_LOG(INFO, "mock env init begin", K(this));
    int ret = OB_SUCCESS;

    if (inited_) {
      ret = OB_INIT_TWICE;
      STORAGE_LOG(ERROR, "init twice", K(ret));
    } else if (OB_FAIL(ObClockGenerator::init())) {
      STORAGE_LOG(ERROR, "init ClockGenerator failed", K(ret));
    } else if (FALSE_IT(init_gctx_gconf())) {
    } else if (OB_FAIL(init_before_start_runtime())) {
      STORAGE_LOG(ERROR, "init_before_start_runtime failed", K(ret));
    }
    // Runtime creation invokes OBSERVER.obs_construct_modules(),
    // obs_init_modules(), and obs_start_modules() for the fixed module set.
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

int MockServerRuntimeEnv::start_()
{
  int ret = OB_SUCCESS;
  omt::ObServerRuntime *runtime = nullptr;
  int64_t succ_num = 0;


  if (OB_FAIL(log_block_mgr_.start(storage_env_.log_disk_size_))) {
    SERVER_LOG(ERROR, "log pool start failed", KR(ret));
  } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.start(0/*reserved_size*/))) {
    STORAGE_LOG(WARN, "fail to start object manager", K(ret));
  } else if (OB_FAIL(startup_accel_handler_.start())) {
    STORAGE_LOG(WARN, "fail to start server startup task handler", KR(ret));
  } else if (OB_FAIL(SERVER_STORAGE_META_SERVICE.start())) {
    STORAGE_LOG(ERROR, "server storage meta service fail", K(ret));
  } else if (OB_FAIL(runtime_controller_.start())) {
    STORAGE_LOG(WARN, "fail to start runtime controller", K(ret));
  } else if (OB_FAIL(runtime_controller_.create_bootstrap_runtime())) {
    STORAGE_LOG(WARN, "fail to create bootstrap runtime", K(ret));
  } else if (OB_FAIL(runtime_controller_.get_runtime(runtime))) {
    STORAGE_LOG(WARN, "fail to get server runtime", K(ret));
  } else if (OB_ISNULL(runtime)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "server runtime is null", K(ret));
  } else if (FALSE_IT(lib::Threads::set_default_run_wrapper(runtime))) {
  } else if (OB_FAIL(runtime_controller_.bring_up_runtime())) {
    STORAGE_LOG(WARN, "fail to bring up server runtime", K(ret));
  } else if (OB_FAIL(share::check_server_runtime_ready())) {
    STORAGE_LOG(ERROR, "server runtime is not ready", K(ret));
  } else if (OB_FAIL(runtime->acquire_more_worker(RUNTIME_WORKER_COUNT, succ_num))) {
  }
  return ret;
}

void MockServerRuntimeEnv::destroy()
{
  STORAGE_LOG(INFO, "destroy", K(destroyed_));

  if (server_fd_ > 0) {
    close(server_fd_);
  }
  if (destroyed_) {
    return;
  }
  startup_accel_handler_.destroy();

  runtime_controller_.stop();
  // Timer workers retain the runtime run wrapper selected when their tasks
  // are scheduled. Join them before releasing the runtime object.
  ObTimerService::get_instance().stop();
  runtime_controller_.wait();
  ObTimerService::get_instance().wait();
  // IO workers can also retain the runtime run wrapper. Destroy their
  // channels after runtime modules finish, but before releasing the runtime.
  ObIOManager::get_instance().destroy();
  runtime_controller_.destroy();
  ObKVGlobalCache::get_instance().destroy();
  SERVER_STORAGE_META_SERVICE.destroy();
  share::ObInternalTableChangeNotifier::get_instance().destroy();

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
