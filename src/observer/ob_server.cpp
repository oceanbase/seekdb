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

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#else
#include <windows.h>
#endif
#include <thread>
#include "observer/ob_server.h"
#include "storage/lob/ob_lob_manager.h"
#include "storage/compaction/ob_freeze_info_mgr.h"
#include "share/ob_freeze_info_proxy.h"
namespace oceanbase { namespace observer { common::ObILobReadService * ObServer::lob_read_service() { return mods_lob_manager_; }
int ObServer::get_lower_bound_freeze_info(const int64_t snapshot_version, share::ObFreezeInfo &freeze_info) { return OB_ISNULL(mods_freeze_info_mgr_) ? common::OB_NOT_INIT : mods_freeze_info_mgr_->get_lower_bound_freeze_info_before_snapshot_version(snapshot_version, freeze_info); } } }
#include "rootserver/ob_local_ddl_serial_call.h"
#include "lib/alloc/memory_dump.h"
#include "lib/oblog/ob_log_compressor.h"
#include "lib/ob_running_mode.h"
#include "lib/task/ob_timer_monitor.h"
#include "lib/task/ob_timer_service.h" // ObTimerService
#include "observer/ob_server_utils.h"
#include "observer/ob_server_options.h"
#include "share/ob_timezone_mgr.h"
#include "share/ob_standby_source_util.h"
#include "logservice/ob_log_allocator_mgr.h"
#include "observer/omt/ob_server_runtime.h"
#include "sql/engine/px/p2p_datahub/ob_p2p_dh_mgr.h"
#include "sql/ob_sql_init.h"
#include "sql/ob_sql_task.h"
#include "storage/tx_table/ob_tx_data_cache.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/ob_file_system_router.h"
#include "storage/ob_tablet_autoinc_seq_rpc_handler.h"
#include "sql/engine/px/ob_px_target_monitor.h"
#include "share/ob_device_manager.h"
#include "storage/ob_tablet_autoincrement_service.h"
#include "storage/tx_storage/ob_server_mem_limit_getter.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/tablet/ob_mds_schema_helper.h"
#include "observer/schema/ob_schema_service_sql_impl.h"
#include "storage/ob_file_system_router.h"
#include "sql/optimizer/stat/ob_opt_stat_manager.h" // for ObOptStatManager
#include "share/longops_mgr/ob_longops_mgr.h"
#include "share/ob_ddl_sim_point.h"
#include "storage/ddl/ob_ddl_redo_log_writer.h"
#include "observer/ob_server_utils.h"
#include "common/xml/ob_libxml2_sax_handler.h"
#include "common/ob_data_version_mgr.h"
#include "observer/vector_index/ob_plugin_vector_index_utils.h"
#include "share/roaringbitmap/ob_rb_memory_mgr.h"
#include "storage/fts/dict/ob_ft_cache.h"
#include "lib/utility/ob_target_specific.h"
#include "storage/fts/dict/ob_gen_dic_loader.h"
#include "storage/fts/ob_fts_parser_helper.h"
#include "rpc/ob_request.h"
#include "storage/blocksstable/ob_block_sstable_struct.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::transaction;
using namespace oceanbase::logservice;

extern "C" void ussl_stop();
extern "C" void ussl_wait();

namespace oceanbase
{
namespace common
{
uint64_t __attribute__((used)) lib_get_cpu_khz()
{
  return OBSERVER.get_cpu_frequency_khz();
}
} // namespace common

// Definition of the low-layer module-access facade global, declared in
// share/rc/ob_module_provider.h. Set to &OBSERVER early in ObServer::init(),
// before any module is constructed; read by low-layer code via g_mp->xxx().
namespace share { ObIModuleProvider *g_mp = nullptr; }

}

namespace oceanbase
{
namespace observer
{

static int check_need_initialize(const char *base_dir, const char *data_dir, const char *redo_dir, bool &need_initialize)
{
  int ret = OB_SUCCESS;
  need_initialize = false;
  bool data_file_exists = false;
  bool redo_empty = true;
  ObSqlString data_file_path;
  ObSqlString redo_file_path;
  if (OB_FAIL(data_file_path.assign_fmt("%s/%s/%s", data_dir, BLOCK_SSTBALE_DIR_NAME, BLOCK_SSTBALE_FILE_NAME))) {
    LOG_WARN("Failed to assign data file path.");
  }
  if (OB_FAIL(FileDirectoryUtils::is_exists(data_file_path.ptr(), data_file_exists))) {
    LOG_WARN("Failed to check data file exists.", K(data_file_path));
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(redo_dir))) {
    LOG_WARN("Failed to create redo path", KCSTRING(redo_dir), KCSTRING(strerror(errno)));
  } else if (OB_FAIL(ObServerLogBlockMgr::check_clog_directory_is_empty(redo_dir, redo_empty))) {
    LOG_WARN("Failed to check redo file exists.", KCSTRING(redo_dir), K(ret));
  } else if (!data_file_exists && redo_empty) {
    need_initialize = true;
  } else if (data_file_exists && !redo_empty) {
    need_initialize = false;
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("The status of deployment environment is not consistent. Please clear the directories and restart.");
    LOG_WARN("    base-dir", KCSTRING(base_dir));
    LOG_WARN("    data-dir", KCSTRING(data_dir));
    LOG_WARN("    redo-dir", KCSTRING(redo_dir));
  }
  return ret;
}

ObServer::ObServer()
  : need_ctas_cleanup_(true),
    gctx_(GCTX),
    prepare_stop_(true), stop_(true), has_stopped_(true), has_destroy_(false),
    net_frame_(gctx_), sql_conn_pool_(), ddl_conn_pool_(),
    sql_proxy_(),
    config_(ObServerConfig::get_instance()),
    reload_config_(config_, gctx_), config_mgr_(config_, reload_config_),
    timezone_mgr_(omt::ObTimezoneMgr::get_instance()),
    schema_service_(share::schema::ObMultiVersionSchemaService::get_instance()),
    tablet_operator_(),
    bandwidth_throttle_(),
    sys_bkgd_net_percentage_(0),
    ethernet_speed_(0),
    cpu_frequency_(DEFAULT_CPU_FREQUENCY),
    session_mgr_(),
    ob_service_(gctx_),
    server_runtime_controller_(), vt_data_service_(local_management_service_, self_addr_, &config_),
    start_time_(ObTimeUtility::current_time()),
    warm_up_start_time_(0),
    diag_(),
    scramble_rand_(),
    server_gtimer_(),
    sql_mem_timer_(),
    ctas_clean_up_timer_(),
    duty_task_(),
    sql_mem_task_(),
    ctas_clean_up_task_(),
    refresh_cpu_frequency_task_(),
    schema_status_proxy_(sql_proxy_),
    is_log_dir_empty_(false),
    conn_res_mgr_(),
    disk_usage_report_task_(),
    log_block_mgr_()
{
}

ObServer::~ObServer()
{
  destroy();
}

int ObServer::init(const ObServerOptions &opts, const ObPLogWriterCfg &log_cfg)
{
  gctx_.set_embedded_mode(opts.embedded_);
  FLOG_INFO("[OBSERVER_NOTICE] start to init observer");
  DBA_STEP_RESET(server_start);
  int ret = OB_SUCCESS;
  // Publish ObServer as the module provider before any module access (boot).
  // Accessors delegate to the live server module set, so g_mp is valid throughout
  // bring-up incl. re-entrant module init (e.g. ObLogService::server_module_init -> ls_service()).
  share::g_mp = this;
  init_arches();
  scramble_rand_.init(static_cast<uint64_t>(start_time_), static_cast<uint64_t>(start_time_ / 2));

  if (OB_SUCC(ret) && OB_FAIL(init_config(opts))) {
    LOG_ERROR("init config failed", KR(ret));
  }

#ifndef _WIN32
  if (OB_SUCC(ret) && gctx_.is_embedded_mode()) {
    clients_fd_ = ::open("./run/seekdb.clients", O_CREAT | O_RDWR, 0644);
    if (clients_fd_ < 0) {
      ret = OB_ERROR;
      LOG_ERROR("failed to open seekdb.clients at startup", K(errno));
    } else {
      FLOG_INFO("opened seekdb.clients fd at startup", K(clients_fd_));
    }
  }
#else
  if (OB_SUCC(ret) && gctx_.is_embedded_mode()) {
    clients_h_ = CreateFileA(
        "run\\seekdb.clients",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (clients_h_ == INVALID_HANDLE_VALUE) {
      ret = OB_ERROR;
      LOG_ERROR("failed to open seekdb.clients at startup",
                "last_error", (int)GetLastError());
    } else {
      FLOG_INFO("opened seekdb.clients HANDLE at startup");
    }
  }
#endif

  bool need_initialize = false;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(check_need_initialize(opts.base_dir_.ptr(),
                                           config_.data_dir.get_value(),
                                           config_.redo_dir.get_value(),
                                           need_initialize))) {
    LOG_ERROR("check need initialize failed", KR(ret));
  }
  if (OB_SUCC(ret) && need_initialize) {
    LOG_INFO("Need to initialize", K(need_initialize));
  }
  LOG_DBA_INFO_V2(OB_SERVER_INIT_BEGIN,
                  DBA_STEP_INC_INFO(server_start),
                  "observer init begin.");

  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObSimpleThreadPoolDynamicMgr::get_instance().init())) {
      LOG_ERROR("init queue_thread dynamic mgr failed", KR(ret));
    } else if (OB_FAIL(ObTimerService::get_instance().start())) {
      LOG_ERROR("start timer service failed", KR(ret));
    }
  }

    if (FAILEDx(OB_LOGGER.init(log_cfg))) {
      LOG_ERROR("async log init error.", KR(ret));
    } else if (OB_FAIL(OB_LOG_COMPRESSOR.init())) {
      LOG_ERROR("log compressor init error.", KR(ret));
    } else if (OB_FAIL(OB_LOGGER.set_log_compressor(&OB_LOG_COMPRESSOR))) {
      LOG_ERROR("set log compressor error.", KR(ret));
    } else if (OB_FAIL(init_tz_info_mgr())) {
      LOG_ERROR("init tz_info_mgr failed", KR(ret));
    } else if (OB_FAIL(ObSqlTaskFactory::get_instance().init())) {
      LOG_ERROR("init sql task factory failed", KR(ret));
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(sql::init_sql_factories())) {
        LOG_ERROR("init sql factories !", KR(ret));
      } else if (OB_FAIL(sql::init_sql_executor_singletons())) {
        LOG_ERROR("init sql executor singletons !", KR(ret));
      } else if (OB_FAIL(sql::init_sql_expr_static_var())) {
        LOG_ERROR("init sql expr static var !", KR(ret));
      } else if (OB_FAIL(ObPreProcessSysVars::init_sys_var(!need_initialize ? ObServerOptions::KeyValueArray() : opts.variables_))) {
        LOG_ERROR("init PreProcessing system variable failed !", KR(ret));
      } else if (OB_FAIL(ObBasicSessionInfo::init_sys_vars_cache_base_values())) {
        LOG_ERROR("init session base values failed", KR(ret));
      }
    }
    if (FAILEDx(ObQueryRetryCtrl::init())) {
      LOG_ERROR("init retry ctrl failed", KR(ret));
    } else if (OB_FAIL(ObMdsEventBuffer::init())) {
      LOG_WARN("init MDS event buffer failed", KR(ret));
    } else if (OB_FAIL(init_loaddata_global_stat())) {
      LOG_ERROR("init global load data stat map failed", KR(ret));
    } else if (OB_FAIL(init_pre_setting())) {
      LOG_ERROR("init pre setting failed", KR(ret));
    } else if (OB_FAIL(init_global_context())) {
      LOG_ERROR("init global context failed", KR(ret));
    } else if (OB_FAIL(parse_role(opts))) {
      LOG_ERROR("parse role failed", KR(ret));
    } else if (OB_FAIL(init_sql_proxy())) {
      LOG_ERROR("init sql connection pool failed", KR(ret));
    }
    if (OB_SUCC(ret)) {
    if (OB_FAIL(ObDeviceManager::get_instance().init_devices_env())) {
      LOG_ERROR("init device manager failed", KR(ret));
    }
    }
    if (OB_SUCC(ret)) {
    if (OB_FAIL(init_io())) {
      LOG_ERROR("init io failed", KR(ret));
    }
    }
    if (OB_SUCC(ret)) {
    if (OB_FAIL(ObMemoryDump::get_instance().init())) {
      LOG_ERROR("init memory dumper failed", KR(ret));
    }
    }
    if (OB_SUCC(ret)) {
    if (OB_FAIL(init_global_kvcache())) {
      LOG_ERROR("init global kvcache failed", KR(ret));
    }
    }
    if (OB_SUCC(ret)) {
    if (OB_FAIL(schema_status_proxy_.init())) {
      LOG_ERROR("fail to init schema status proxy", KR(ret));
    }
    }
    if (OB_SUCC(ret)) {
    if (OB_FAIL(init_schema())) {
      LOG_ERROR("init schema failed", KR(ret));
    }
    }
    if (OB_SUCC(ret)) {
    if (OB_FAIL(init_network())) {
      LOG_ERROR("init network failed", KR(ret));
    }
    }
    if (OB_SUCC(ret)) {
    if (OB_FAIL(init_interrupt())) {
      LOG_ERROR("init interrupt failed", KR(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(init_fts())) {
      LOG_ERROR("init fulltext parser data failed", KR(ret));
    } else if (OB_FAIL(init_ob_service(need_initialize))) {
      LOG_ERROR("init ob service failed", KR(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(init_local_management_service(need_initialize))) {
      LOG_ERROR("init local management service failed", KR(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(init_sql())) {
      LOG_ERROR("init sql failed", KR(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(init_sql_runner())) {
      LOG_ERROR("init sql runner failed", KR(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(init_pl())) {
      LOG_ERROR("init pl failed", K(ret));
    } else if (OB_FAIL(tablet_operator_.init(&meta_db_pool_))) {
      LOG_ERROR("tablet table operator init failed", KR(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(init_autoincrement_service())) {
      LOG_ERROR("init auto-increment service failed", KR(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(init_tablet_autoincrement_service())) {
      LOG_ERROR("init auto-increment service failed", KR(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(init_bandwidth_throttle())) {
      LOG_ERROR("init bandwidth_throttle failed", KR(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(ObClockGenerator::init())) {
      LOG_ERROR("init create clock generator failed", KR(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(init_storage())) {
      LOG_ERROR("init storage failed", KR(ret));
    }
    if (OB_SUCC(ret)) {
    if (OB_FAIL(init_tx_data_cache())) {
      LOG_ERROR("init tx data cache failed", KR(ret));
    } else if (OB_FAIL(tmp_file::ObTmpBlockCache::get_instance().init("tmp_block_cache"))) {
      LOG_ERROR("init tmp block cache failed", KR(ret));
    } else if (OB_FAIL(tmp_file::ObTmpPageCache::get_instance().init("tmp_page_cache"))) {
      LOG_ERROR("init tmp page cache failed", KR(ret));
    } else if (OB_FAIL(ObLogAllocatorMgr::get_instance().init())) {
      LOG_ERROR("init ObLogAllocatorMgr failed", KR(ret));
    } else if (OB_FAIL(startup_accel_handler_.init())) {
      LOG_ERROR("init server startup task handler failed", KR(ret));
    } else if (OB_FAIL(SERVER_STORAGE_META_SERVICE.init())) {
      LOG_ERROR("init server storage meta handler failed", KR(ret));
    } else if (OB_FAIL(init_server_runtime())) {
      LOG_ERROR("init server runtime failed", KR(ret));
    } else if (OB_FAIL(init_ctas_clean_up_task())) {
      LOG_ERROR("init ctas clean up task failed", KR(ret));
    } else if (OB_FAIL(init_ddl_heart_beat_task_container())) {
      LOG_ERROR("init ddl heart beat task container failed", KR(ret));
    } else if (OB_FAIL(init_redef_heart_beat_task())) {
      LOG_ERROR("init redef heart beat task failed", KR(ret));
    } else if (OB_FAIL(init_refresh_cpu_frequency())) {
      LOG_ERROR("init refresh cpu frequency failed", KR(ret));
    } else if (OB_FAIL(ObOptStatManager::get_instance().init(
                         &sql_proxy_, &config_))) {
      LOG_ERROR("init opt stat manager failed", KR(ret));
    } else if (OB_FAIL(ObSysTaskStatMgr::get_instance().set_self_addr(self_addr_))) {
      LOG_ERROR("set sys task status self addr failed", KR(ret));
    } else if (OB_FAIL(ObTimerMonitor::get_instance().init())) {
      LOG_ERROR("init timer monitor failed", KR(ret));
    } else if (OB_FAIL(PX_P2P_DH.init())) {
      LOG_ERROR("init px p2p datahub failed", KR(ret));
    } else if (OB_FAIL(init_px_target_mgr())) {
      LOG_ERROR("init px target mgr failed", KR(ret));
    } else if (OB_FAIL(ObDictCache::get_instance().init("dict_cache"))) {
      LOG_ERROR("init dict cache failed", KR(ret));
    } else if (OB_FAIL(ObGenDicLoader::get_instance().init())) {
      LOG_WARN("init dictionary loader failed", K(ret));
    } else if (OB_FAIL(ObDDLRedoLock::get_instance().init())) {
      LOG_WARN("init ddl redo lock failed", K(ret));
#ifdef ERRSIM
    } else if (OB_FAIL(ObDDLSimPointMgr::get_instance().init())) {
      LOG_WARN("init ddl sim point mgr fail", KR(ret));
#endif
    } else {
      // GDS direct dispatch through GCTX.local_management_service_
    }
  }
  }

  if (OB_FAIL(ret)) {
    LOG_ERROR("[OBSERVER_NOTICE] fail to init observer", KR(ret));
    LOG_DBA_FORCE_PRINT(DBA_ERROR, OB_SERVER_INIT_FAIL, ret,
                        DBA_STEP_INC_INFO(server_start),
                        "observer init fail. "
                        "you may find solutions in previous error logs or seek help from official technicians.");
#ifdef _WIN32
    Sleep(3000);
#endif
    set_stop();
    destroy();
  } else {
    FLOG_INFO("[OBSERVER_NOTICE] success to init observer",
        "lib::g_runtime_enabled", lib::g_runtime_enabled);
    LOG_DBA_INFO_V2(OB_SERVER_INIT_SUCCESS,
                    DBA_STEP_INC_INFO(server_start),
                    "observer init success.");
  }
  return ret;
}


void ObServer::destroy()
{
  // observer.destroy() be called under two scenarios:
  // 1. main() exit
  // 2. ObServer destruction.
  // ObServer itself is a static instance
  // during process exit, The destruction order of Observer and many other static instance are undefined.
  // this fact may cause double destruction.
  // If ObBackupInfo precedes ObServer destruction, ObServer destruction triggers the destruction of ObBackupInfo,
  // Cause ObBackupInfo to lock the mutex that has been destroyed by itself, and finally trigger the core
  // This is essentially an implementation problem of repeated destruction of ObBackupInfo (or one of its members). ObServer also adds a layer of defense here.
  FLOG_INFO("[OBSERVER_NOTICE] destroy observer begin");

  FLOG_INFO("begin to destroy config manager");
  config_mgr_.destroy();
  FLOG_INFO("destroy config manager success");

  if (!has_destroy_ && has_stopped_) {

    FLOG_INFO("begin to destroy OB_LOGGER");
    OB_LOGGER.destroy();
    FLOG_INFO("OB_LOGGER destroyed");

    FLOG_INFO("begin to destroy OB_LOG_COMPRESSOR");
    OB_LOG_COMPRESSOR.destroy();
    FLOG_INFO("OB_LOG_COMPRESSOR destroyed");

    FLOG_INFO("begin to destroy task controller");
    ObTaskController::get().destroy();
    FLOG_INFO("task controller destroyed");


    FLOG_INFO("begin destroy signal handle");
    signal_handle_.destroy();
    FLOG_INFO("signal handle destroyed");

    FLOG_INFO("begin to destroy timer monitor");
    ObTimerMonitor::get_instance().destroy();
    FLOG_INFO("timer monitor destroyed");

    FLOG_INFO("begin to destroy schema service");
    schema_service_.destroy();
    FLOG_INFO("schema service destroyed");

    FLOG_INFO("begin to destroy table auto increment service");
    ObTabletAutoincrementService::get_instance().destroy();
    FLOG_INFO("table auto increment service destroyed");

    FLOG_INFO("begin to destroy server timer");
    server_gtimer_.destroy();
    FLOG_INFO("server timer destroyed");

    FLOG_INFO("begin to destroy sql memory manager timer");
    sql_mem_timer_.destroy();
    FLOG_INFO("sql memory manager timer destroyed");

    FLOG_INFO("begin to destroy ctas clean up timer");
    ctas_clean_up_timer_.destroy();
    FLOG_INFO("ctas clean up timer destroyed");

    FLOG_INFO("begin to destroy local management service");
    local_management_service_.destroy();
    FLOG_INFO("local management service destroyed");

    FLOG_INFO("begin to destroy ob service");
    ob_service_.destroy();
    FLOG_INFO("ob service destroyed");

    FLOG_INFO("begin to destroy session manager");
    session_mgr_.destroy();
    FLOG_INFO("session manager destroyed");

    FLOG_INFO("begin to destroy sql engine");
    sql_engine_.destroy();
    FLOG_INFO("sql engine destroyed");

    FLOG_INFO("begin to destroy xml ctx");
    ObLibXml2SaxHandler::destroy();
    FLOG_INFO("xml ctx destroyed");

    FLOG_INFO("begin to destroy pl engine");
    pl_engine_.destory();
    FLOG_INFO("pl engine destroyed");

    FLOG_INFO("begin to destroy disk usage report task");
    disk_usage_report_task_.destroy();
    FLOG_INFO("disk usage report task destroyed");

    FLOG_INFO("begin to destroy store cache");
    OB_STORE_CACHE.destroy();
    FLOG_INFO("store cache destroyed");

    FLOG_INFO("begin to destroy tx data kv cache");
    OB_TX_DATA_KV_CACHE.destroy();
    FLOG_INFO("tx data kv cache destroyed");

    FLOG_INFO("begin to destroy tmp block cache");
    tmp_file::ObTmpBlockCache::get_instance().destroy();
    FLOG_INFO("tmp block cache destroyed");

    FLOG_INFO("begin to destroy tmp page cache");
    tmp_file::ObTmpPageCache::get_instance().destroy();
    FLOG_INFO("tmp page cache destroyed");

    FLOG_INFO("begin to destroy net frame");
    net_frame_.destroy();
    FLOG_INFO("net frame destroyed");

    FLOG_INFO("begin to destroy io manager");
    ObIOManager::get_instance().destroy();
    FLOG_INFO("io manager destroyed");

    FLOG_INFO("begin to destroy server storage meta service");
    SERVER_STORAGE_META_SERVICE.destroy();
    FLOG_INFO("server storage meta service destroyed");

    FLOG_INFO("begin to destroy memory dump");
    ObMemoryDump::get_instance().destroy();
    FLOG_INFO("memory dump destroyed");

    FLOG_INFO("begin to destroy time zone manager");
    timezone_mgr_.destroy();
    FLOG_INFO("time zone manager destroyed");

    FLOG_INFO("begin to destroy ObMdsEventBuffer");
    ObMdsEventBuffer::destroy();
    FLOG_INFO("ObMdsEventBuffer destroyed");

    FLOG_INFO("begin to destroy server runtime");
    server_runtime_controller_.destroy();
    FLOG_INFO("server runtime destroyed");

    FLOG_INFO("begin to destroy query retry ctrl");
    ObQueryRetryCtrl::destroy();
    FLOG_INFO("query retry ctrl destroy");

    FLOG_INFO("begin to destroy storage object mgr");
    OB_STORAGE_OBJECT_MGR.destroy();
    FLOG_INFO("storage object mgr destroyed");

    FLOG_INFO("begin to destroy server startup task handler");
    startup_accel_handler_.destroy();
    FLOG_INFO("server startup task handler destroyed");

    FLOG_INFO("begin to destroy dict cache");
    ObDictCache::get_instance().destroy();
    FLOG_INFO("dict cache destroyed");

    FLOG_INFO("begin to destroy log block mgr");
    log_block_mgr_.destroy();
    FLOG_INFO("log block mgr destroy");

    FLOG_INFO("begin to destroy kv global cache");
    ObKVGlobalCache::get_instance().destroy();
    FLOG_INFO("kv global cache destroyed");

    // for unittest, make sure threads can exit
    ObTimerService::get_instance().stop();
    ObTimerService::get_instance().wait();
    FLOG_INFO("begin to destroy timer service");
    ObTimerService::get_instance().destroy();
    FLOG_INFO("timer service destroyed");

    FLOG_INFO("begin to destroy clock generator");
    ObClockGenerator::destroy();
    FLOG_INFO("clock generator destroyed");

    deinit_fts();

    FLOG_INFO("begin to destroy io device");
    ObIODeviceWrapper::get_instance().destroy();
    FLOG_INFO("io device destroyed");

#ifndef _WIN32
    if (clients_fd_ >= 0) {
      ::close(clients_fd_);
      clients_fd_ = -1;
      FLOG_INFO("closed seekdb.clients fd");
    }
#else
    if (clients_h_ != INVALID_HANDLE_VALUE) {
      CloseHandle(clients_h_);
      clients_h_ = INVALID_HANDLE_VALUE;
      FLOG_INFO("closed seekdb.clients HANDLE");
    }
#endif

    has_destroy_ = true;
    FLOG_INFO("[OBSERVER_NOTICE] destroy observer end");
  }
}

int ObServer::start()
{
  int ret = OB_SUCCESS;
  gctx_.status_ = SS_STARTING;
  // begin to start a observer
  FLOG_INFO("[OBSERVER_NOTICE] start observer begin");
  LOG_DBA_INFO_V2(OB_SERVER_START_BEGIN,
                  DBA_STEP_INC_INFO(server_start),
                  "observer start begin.");

    LOG_DBA_INFO_V2(OB_SERVER_INSTANCE_START_BEGIN,
                    DBA_STEP_INC_INFO(server_start),
                    "observer instance start begin.");

    if (FAILEDx(signal_handle_.start())) {
      LOG_ERROR("fail to start signal handler", KR(ret));
    } else {
      FLOG_INFO("success to start signal handler");
    }
    if (FAILEDx(startup_accel_handler_.start())) {
      LOG_ERROR("fail to start server startup task handler", KR(ret));
    } else {
      FLOG_INFO("success to start server startup task handler");
    }
    // Services are registered once; start() is triggered by reload_config().

    if (FAILEDx(ObMdsSchemaHelper::get_instance().init())) {
      LOG_ERROR("fail to init mds schema helper", K(ret));
    } else {
      FLOG_INFO("success to init mds schema helper");
    }
    if (FAILEDx(ObIOManager::get_instance().start())) {
      LOG_ERROR("fail to start io manager", KR(ret));
    } else {
      FLOG_INFO("success to start io manager");
    }
    int64_t slog_reserved_size = 0;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(SERVER_STORAGE_META_SERVICE.get_reserved_size(slog_reserved_size))) {
      LOG_WARN("fail to get slog reserved size", KR(ret), K(slog_reserved_size));
    } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.start(slog_reserved_size))) {
      LOG_ERROR("start storage object mgr fail", KR(ret), K(slog_reserved_size));
    } else {
      FLOG_INFO("success to start storage object manager");
    }
    if (FAILEDx(server_runtime_controller_.start())) {
      LOG_ERROR("fail to start server runtime", KR(ret));
    } else {
      FLOG_INFO("success to start server runtime");
    }

    if (FAILEDx(SERVER_STORAGE_META_SERVICE.start())) {
      LOG_ERROR("fail to start server storage meta service", KR(ret));
    } else {
      FLOG_INFO("success to start server storage meta service");
    }
    // Validate local disk capacity after the storage runtime is ready.
    if (FAILEDx(OB_STORAGE_OBJECT_MGR.check_disk_space_available())) {
      LOG_ERROR("failed to check disk space available", K(ret));
    } else {
      LOG_INFO("success to check disk space available");
    }
    if (FAILEDx(log_block_mgr_.start(storage_env_.log_disk_size_))) {
      LOG_ERROR("fail to start log pool", KR(ret));
    } else {
      FLOG_INFO("success to start log pool");
    }
    if (FAILEDx(initialize_server_runtime())) {
      LOG_ERROR("fail to initialize server runtime", KR(ret));
    } else {
      FLOG_INFO("success to initialize server runtime");
    }
    if (FAILEDx(local_management_service_.start_service())) {
      LOG_ERROR("fail to start local management services", KR(ret));
    } else {
      FLOG_INFO("success to start local management services");
    }
    // Treat --embedded as the embed telemetry reporter; ObService reports bootstrap telemetry synchronously.
    if (FAILEDx(ob_service_.start())) {
      LOG_ERROR("fail to start oceanbase service", KR(ret));
    } else {
      FLOG_INFO("success to start oceanbase service");
    }

    if (FAILEDx(config_mgr_.reload_config())) {
      LOG_ERROR("fail to reload configuration", KR(ret));
    } else {
      FLOG_INFO("success to reload configuration");
    }

    if (FAILEDx(ObTimerMonitor::get_instance().start())) {
      LOG_ERROR("fail to start timer monitor", KR(ret));
    } else {
      FLOG_INFO("success to start timer monitor");
    }

    if (OB_SUCC(ret)) {
      FLOG_INFO("[OBSERVER_NOTICE] server instance start succeed");
      LOG_DBA_INFO_V2(OB_SERVER_INSTANCE_START_SUCCESS,
                      DBA_STEP_INC_INFO(server_start),
                      "observer instance start success.");
      prepare_stop_ = false;
      stop_ = false;
      has_stopped_ = false;
    } else {
      LOG_DBA_ERROR_V2(OB_SERVER_INSTANCE_START_FAIL, ret,
                       DBA_STEP_INC_INFO(server_start),
                       "observer instance start fail. "
                       "you may find solutions in previous error logs or seek help from official technicians.");
    }
    // this handler is only used to process tasks during startup. so it can be destroied here.
    startup_accel_handler_.destroy();

    // refresh server configure
    //
    if (FAILEDx(config_mgr_.got_version())) {
      FLOG_WARN("fail to refresh server configure", KR(ret));
    } else {
      FLOG_INFO("success to refresh server configure");
    }

    if (FAILEDx(wait_for_server_runtime())) {
      LOG_ERROR("server runtime did not become ready", KR(ret));
    } else {
      FLOG_INFO("server runtime is ready");
    }
    if (FAILEDx(local_management_service_.start_runtime_dependent_services())) {
      LOG_ERROR("fail to start runtime dependent local services", KR(ret));
    } else {
      FLOG_INFO("success to start runtime dependent local services");
    }

    // check if schema ready
    if (FAILEDx(check_if_schema_ready())) {
      LOG_ERROR("fail to check if schema ready", KR(ret));
    } else {
      FLOG_INFO("success to check if schema ready");
    }

    // check if timezone usable
    if (FAILEDx(check_if_timezone_usable())) {
      LOG_ERROR("fail to check if timezone usable", KR(ret));
    } else {
      FLOG_INFO("success to check if timezone usable");
    }

    if (FAILEDx(net_frame_.start())) {
      LOG_ERROR("fail to start net frame", KR(ret));
    } else {
      FLOG_INFO("success to start net frame");
    }

  int64_t start_service_time = ObTimeUtility::current_time();
  if (OB_FAIL(ret)) {
    LOG_ERROR("failure occurs, try to set stop and wait", KR(ret));
    LOG_DBA_FORCE_PRINT(DBA_ERROR, OB_SERVER_START_FAIL, ret,
                        DBA_STEP_INC_INFO(server_start),
                        "observer start fail, the stop status is ", stop_, ". "
                        "you may find solutions in previous error logs or seek help from official technicians.");

    set_stop();
    wait();
  } else if (!stop_) {
    GCTX.status_ = SS_SERVING;
    GCTX.start_service_time_ = start_service_time;
    FLOG_INFO("[OBSERVER_NOTICE] observer start service", "start_service_time", GCTX.start_service_time_);
    LOG_DBA_INFO_V2(OB_SERVER_START_SUCCESS,
                    DBA_STEP_INC_INFO(server_start),
                    "observer start success.");
  } else {
    FLOG_INFO("[OBSERVER_NOTICE] observer is set to stop", KR(ret), K_(stop));
    LOG_DBA_FORCE_PRINT(DBA_ERROR, OB_SERVER_START_FAIL, ret,
                        DBA_STEP_INC_INFO(server_start),
                        "observer start fail, the stop status is ", stop_, ". "
                        "you may find solutions in previous error logs or seek help from official technicians.");
  }

  return ret;
}


int ObServer::initialize_server_runtime()
{
  int ret = OB_SUCCESS;

  omt::ObServerRuntime *runtime = nullptr;
  if (OB_FAIL(server_runtime_controller_.get_runtime(runtime))) {
    if (OB_SERVER_RUNTIME_NOT_READY == ret) {
      ret = OB_SUCCESS;
      if (OB_FAIL(server_runtime_controller_.create_bootstrap_runtime())) {
        LOG_ERROR("fail to create bootstrap runtime", KR(ret));
      }
    } else {
      LOG_ERROR("fail to get server runtime", KR(ret));
    }
  } else if (OB_FAIL(server_runtime_controller_.refresh_runtime_resources())) {
    LOG_WARN("fail to refresh server runtime resources", KR(ret));
  }
  runtime = nullptr;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(server_runtime_controller_.get_runtime(runtime))) {
    LOG_WARN("failed to get default server runtime", KR(ret));
  } else if (OB_ISNULL(runtime)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("server runtime is null when setting default run wrapper");
  } else {
    lib::Threads::set_default_run_wrapper(runtime);
    LOG_INFO("set default server runtime", KP(runtime));
  }
  if (OB_SUCC(ret) && OB_FAIL(server_runtime_controller_.bring_up_runtime())) {
    LOG_ERROR("fail to bring up server runtime", KR(ret));
  }
  return ret;
}

int ObServer::wait_for_server_runtime()
{
  int ret = OB_SUCCESS;
  bool synced = false;
  bool timestamp_ready = false;
  LOG_DBA_INFO_V2(OB_SERVER_WAIT_RUNTIME_READY_BEGIN,
                  DBA_STEP_INC_INFO(server_start),
                  "wait for server runtime begin.");
  while (OB_SUCC(ret) && !stop_ && (!synced || !timestamp_ready)) {
    synced = server_runtime_controller_.has_synced();
    if (synced && !timestamp_ready) {
      SCN gts;
      if (OB_FAIL(OB_TS_MGR.get_gts(gts))) {
        if (OB_EAGAIN == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to check timestamp service readiness", KR(ret));
        }
      } else {
        timestamp_ready = true;
      }
    }
    if (!synced || !timestamp_ready) {
      ob_usleep(10 * 1000);
    }
  }
  FLOG_INFO("wait for server runtime", KR(ret), K(stop_), K(synced), K(timestamp_ready));
  if (!stop_ && synced && timestamp_ready) {
    LOG_DBA_INFO_V2(OB_SERVER_WAIT_RUNTIME_READY_SUCCESS,
                    DBA_STEP_INC_INFO(server_start),
                    "wait for server runtime success.");
  } else {
    LOG_DBA_ERROR_V2(OB_SERVER_WAIT_RUNTIME_READY_FAIL, ret,
                     DBA_STEP_INC_INFO(server_start),
                     "wait for server runtime failed, server stop status is ", stop_, ". "
                     "you may find solutions in previous error logs or seek help from official technicians.");
  }
  return ret;
}

int ObServer::check_if_schema_ready()
{
  int ret = OB_SUCCESS;
  bool schema_ready = false;
  int64_t baseline_schema_version = OB_INVALID_VERSION;
  int64_t current_schema_version = OB_INVALID_VERSION;
  const int64_t SLEEP_INTERVAL_US = 10 * 1000; // 10ms
  LOG_DBA_INFO_V2(OB_SERVER_WAIT_SCHEMA_READY_BEGIN,
                  DBA_STEP_INC_INFO(server_start),
                  "wait schema ready begin.");
  while (!stop_ && !schema_ready) {
    ret = OB_SUCCESS;
    if (OB_FAIL(schema_service_.get_baseline_schema_version(true/*auto_update*/, baseline_schema_version))) {
      LOG_WARN("fail to get baseline schema version", KR(ret));
    } else if (OB_INVALID_VERSION == baseline_schema_version || baseline_schema_version < 0) {
      LOG_WARN("invalid baseline schema version", K(baseline_schema_version));
    } else if (OB_FAIL(schema_service_.get_runtime_refreshed_schema_version(current_schema_version))) {
      LOG_WARN("fail to get runtime refreshed schema version", KR(ret));
    } else {
      schema_ready = (current_schema_version >= baseline_schema_version);
    }
    if (!schema_ready) {
      LOG_INFO("schema not ready yet", K(current_schema_version), K(baseline_schema_version));
      ob_usleep(SLEEP_INTERVAL_US);
    }
  }
  FLOG_INFO("check if schema ready", KR(ret), K(stop_), K(schema_ready),
            K(current_schema_version), K(baseline_schema_version));
  if (!stop_ && schema_ready) {
    LOG_DBA_INFO_V2(OB_SERVER_WAIT_SCHEMA_READY_SUCCESS,
                    DBA_STEP_INC_INFO(server_start),
                    "wait schema ready success.");
  } else {
    LOG_DBA_ERROR_V2(OB_SERVER_WAIT_SCHEMA_READY_FAIL, ret,
                     DBA_STEP_INC_INFO(server_start),
                     "wait schema ready fail, server stop status is ", stop_, ". "
                     "you may find solutions in previous error logs or seek help from official technicians.");
  }
  return ret;
}

int ObServer::check_if_timezone_usable()
{
  int ret = OB_SUCCESS;
  bool timezone_usable = false;
  while (OB_SUCC(ret) && !stop_ && !timezone_usable) {
    timezone_usable = timezone_mgr_.is_usable();
    if (!timezone_usable) {
      (void) (timezone_mgr_.refresh_timezone_info());
      ob_usleep(10 * 1000);
    }
  }
  if (FAILEDx(timezone_mgr_.start())) {
    LOG_ERROR("fail to start time zone manager", KR(ret));
  } else {
    FLOG_INFO("success to start time zone manager");
  }
  FLOG_INFO("check if timezone usable", KR(ret), K(stop_), K(timezone_usable));
  return ret;
}
void ObServer::prepare_stop()
{
  prepare_stop_ = true;
  // reserve some time to switch leader
  ob_usleep(5 * 1000 * 1000);
}

bool ObServer::is_prepare_stopped()
{
  return prepare_stop_;
}

bool ObServer::is_stopped()
{
  return stop_;
}

void ObServer::embed_shutdown()
{
  // Do not call stop(): it runs multi_tenant_/net_frame teardown that can block
  // indefinitely in embed CI. Signal modules to exit instead.
  if (!gctx_.is_inited() || !gctx_.is_embedded_mode() || stop_) {
    return;
  }
  set_stop();
  obs_stop_modules();
  obs_wait_modules();
}

void ObServer::set_stop()
{
  stop_ = true;
  ob_service_.set_stop();
  gctx_.status_ = SS_STOPPING;
  FLOG_INFO("[OBSERVER_NOTICE] observer is setted to stop");
}

int ObServer::stop()
{
  int ret = OB_SUCCESS;
  int fail_ret = OB_SUCCESS;
  FLOG_INFO("[OBSERVER_NOTICE] stop observer begin");
  LOG_DBA_INFO_V2(OB_SERVER_STOP_BEGIN, "observer stop begin.");

  FLOG_INFO("begin to stop OB_LOGGER");
  OB_LOGGER.stop();
  FLOG_INFO("stop OB_LOGGER success");

  FLOG_INFO("begin to stop OB_LOG_COMPRESSOR");
  OB_LOG_COMPRESSOR.stop();
  FLOG_INFO("stop OB_LOG_COMPRESSOR success");

  FLOG_INFO("begin to stop task controller");
  ObTaskController::get().stop();
  FLOG_INFO("stop task controller success");

  FLOG_INFO("begin to stop config manager");
  config_mgr_.stop();
  FLOG_INFO("stop config manager success");

    FLOG_INFO("begin stop signal handle");
    signal_handle_.stop();
    FLOG_INFO("stop signal handle success");

    FLOG_INFO("begin to stop GDS");
    GDS.stop();
    FLOG_INFO("GDS stopped");

    FLOG_INFO("begin to sql nio stop");
    net_frame_.sql_nio_stop();
    FLOG_INFO("sql nio stopped");


    FLOG_INFO("begin to stop schema service");
    schema_service_.stop();
    FLOG_INFO("schema service stopped");


    FLOG_INFO("begin to stop storage object mgr");
    OB_STORAGE_OBJECT_MGR.stop();
    FLOG_INFO("storage object mgr stopped");

    FLOG_INFO("begin to stop timer monitor");
    ObTimerMonitor::get_instance().stop();
    FLOG_INFO("timer monitor stopped");

    FLOG_INFO("begin to stop timer");
    server_gtimer_.stop();
    FLOG_INFO("timer stopped");

    FLOG_INFO("begin to stop sql memory manager timer");
    sql_mem_timer_.stop();
    FLOG_INFO("sql memory manager timer stopped");

    FLOG_INFO("begin to stop ctas clean up timer");
    ctas_clean_up_timer_.stop();
    FLOG_INFO("ctas clean up timer stopped");

    FLOG_INFO("begin to stop sql conn pool");
    sql_conn_pool_.stop();
    FLOG_INFO("sql connection pool stopped");

    FLOG_INFO("begin to stop ddl connection pool");
    ddl_conn_pool_.stop();
    FLOG_INFO("ddl connection pool stopped");

    FLOG_INFO("begin to stop local management service");
    if (OB_FAIL(local_management_service_.stop())) {
      FLOG_WARN("fail to stop local management service", KR(ret));
      fail_ret = OB_SUCCESS == fail_ret ? ret : fail_ret;
    } else {
      FLOG_INFO("local management service stopped");
    }

    FLOG_INFO("begin to stop memory dump");
    ObMemoryDump::get_instance().stop();
    FLOG_INFO("memory dump stopped");

    FLOG_INFO("begin to stop time zone manager");
    timezone_mgr_.stop();
    FLOG_INFO("time zone manager stopped");
    //FLOG_INFO("begin stop partition scheduler");
    //ObPartitionScheduler::get_instance().stop_merge();
    //FLOG_INFO("partition scheduler stopped", KR(ret));

    FLOG_INFO("begin to stop server storage meta service");
    SERVER_STORAGE_META_SERVICE.stop();
    FLOG_INFO("server storage meta service stopped");

    FLOG_INFO("begin to stop server startup task handler");
    startup_accel_handler_.stop();
    FLOG_INFO("server startup task handler stopped");

    // It will wait for all requests done.
    FLOG_INFO("begin to stop server runtime");
    server_runtime_controller_.stop();
    FLOG_INFO("server runtime stopped");
    FLOG_INFO("begin to stop ob_service");
    ob_service_.stop();
    FLOG_INFO("ob_service stopped");

    FLOG_INFO("begin to stop io manager");
    ObIOManager::get_instance().stop();
    FLOG_INFO("io manager stopped");


    // net frame, ensure net_frame should stop after server_runtime_controller_
    // stopping.
    FLOG_INFO("begin to stop net frame");
    if (OB_FAIL(net_frame_.stop())) {
      FLOG_WARN("fail to stop net frame", KR(ret));
      fail_ret = OB_SUCCESS == fail_ret ? ret : fail_ret;
    } else {
      FLOG_INFO("net frame stopped");
    }

    FLOG_INFO("begin to stop kv global cache");
    ObKVGlobalCache::get_instance().stop();
    FLOG_INFO("kv global cache stopped");

    FLOG_INFO("begin to stop timer service");
    ObTimerService::get_instance().stop();
    FLOG_INFO("timer service stopped");

    FLOG_INFO("begin to stop thread dynamic mgr");
    ObSimpleThreadPoolDynamicMgr::get_instance().stop();
    FLOG_INFO("thread dynamic mgr stopped");

    FLOG_INFO("begin to stop clock generator");
    ObClockGenerator::get_instance().stop();
    FLOG_INFO("clock generator stopped");

  has_stopped_ = true;
  FLOG_INFO("[OBSERVER_NOTICE] stop observer end", KR(ret));
  if (OB_SUCCESS != fail_ret) {
    LOG_DBA_ERROR_V2(OB_SERVER_STOP_FAIL, fail_ret, "observer stop fail. "
                     "you may find solutions in previous error logs or seek help from official technicians.");
  } else {
    LOG_DBA_INFO_V2(OB_SERVER_STOP_SUCCESS, "observer stop success.");
  }

  return ret;
}

int ObServer::wait_no_client()
{
  int ret = OB_SUCCESS;
#ifdef _WIN32
  OVERLAPPED ov = {};
  if (LockFileEx(clients_h_,
                 LOCKFILE_EXCLUSIVE_LOCK,
                 0, MAXDWORD, MAXDWORD, &ov)) {
    FLOG_INFO("no clients remaining, exiting");
    _Exit(0);
  } else {
    ret = OB_ERROR;
    LOG_ERROR("LockFileEx failed", "last_error", (int)GetLastError());
  }
#else
  if (flock(clients_fd_, LOCK_EX) == 0) {
    FLOG_INFO("no clients remaining, exiting");
    _Exit(0);
  } else {
    ret = OB_ERROR;
    LOG_ERROR("flock failed", K(errno));
  }
#endif
  return ret;
}

int ObServer::wait()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[OBSERVER_NOTICE] wait observer begin");
  LOG_DBA_INFO_V2(OB_SERVER_WAIT_BEGIN, "observer process wait begin.");
  // wait for stop flag

  if (gctx_.is_embedded_mode()) {
    std::thread([this]() { wait_no_client(); }).detach();
  }

  FLOG_INFO("begin to wait observer setted to stop");
  while (OB_SUCC(ret) && !stop_) {
    SLEEP(3);
  }
  _Exit(0);
  return ret;
}

int ObServer::init_tz_info_mgr()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(timezone_mgr_.init(sql_proxy_))) {
    LOG_ERROR("timezone_mgr_ init failed", K_(self_addr), KR(ret));
  }
  return ret;
}

int ObServer::init_config(const ObServerOptions &opts)
{
  int ret = OB_SUCCESS;

  int64_t base_version = -1;
  // Initialize shared meta database connection pool first
  // Create directory before opening database (handles both normal dir and symlink)
  const char *meta_db_dir = "./store/sstable";
  const char *meta_db_path = "./store/sstable/meta.db";

  // Convert relative path to absolute path to ensure stability when working directory changes
  char cwd[OB_MAX_FILE_NAME_LENGTH] = {0};
  char abs_meta_db_path[OB_MAX_FILE_NAME_LENGTH] = {0};
  if (nullptr == getcwd(cwd, sizeof(cwd))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("failed to get current working directory", K(ret));
  } else {
    snprintf(abs_meta_db_path, sizeof(abs_meta_db_path), "%s/%s", cwd, meta_db_path);
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(meta_db_dir))) {
    LOG_ERROR("failed to create meta db directory", K(ret), K(meta_db_dir));
  } else if (OB_FAIL(meta_db_pool_.init(abs_meta_db_path))) {
    LOG_ERROR("meta_db_pool_ init failed", K_(self_addr), KR(ret), K(abs_meta_db_path));
  } else if (OB_FAIL(config_mgr_.init(&meta_db_pool_))) {
    LOG_ERROR("config_mgr_ init failed", K_(self_addr), KR(ret));
  } else if (OB_FAIL(config_mgr_.got_version())) {
    LOG_WARN("failed to got version", KR(ret));
  } else if (FALSE_IT(base_version = config_mgr_.get_current_version())) {
  } else if (OB_FAIL(DATA_VERSION_MGR.init())) {
    LOG_ERROR("fail to init data_version_mgr", KR(ret));
  } else if (OB_FAIL(DATA_VERSION_MGR.load_from_file())) {
    LOG_ERROR("failed to load data_version_mgr file", KR(ret));
  } else if (OB_FAIL(DATA_VERSION_MGR.validate_or_init_current_version())) {
    LOG_ERROR("persisted data version is incompatible with this binary", KR(ret));
  }

  ObSqlString optstr;
  const char *server_create_time_str = opts.parameters_.count() == 0 ? "server_create_time=%ld" : ",server_create_time=%ld";
  for (int64_t i = 0; OB_SUCC(ret) &&i < opts.parameters_.count(); ++i) {
    const char *format = i == 0 ? "%.*s=%.*s" : ",%.*s=%.*s";
    if (OB_FAIL(optstr.append_fmt(format,
        opts.parameters_.at(i).first.length(), opts.parameters_.at(i).first.ptr(),
        opts.parameters_.at(i).second.length(), opts.parameters_.at(i).second.ptr()))) {
      LOG_ERROR("append optstr fmt failed", KR(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (0 == config_.server_create_time
             && OB_FAIL(optstr.append_fmt(server_create_time_str, ObTimeUtility::current_time()))) {
    LOG_WARN("fail to append server_create_time", KR(ret));
  } else if (OB_FAIL(init_opts_config(opts, optstr.ptr()))) {
    LOG_ERROR("init opts config failed", KR(ret));
  } else {
    config_.print();
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(GMEMCONF.reload_config(config_))) {
    LOG_ERROR("reload memory config failed", KR(ret));
  } else if (OB_FAIL(set_running_mode())) {
    LOG_ERROR("set running mode failed", KR(ret));
  } else if (OB_FAIL(init_self_addr())) {
    LOG_ERROR("init self_addr failed", KR(ret));
  } else if (OB_FAIL(init_config_module(optstr.ptr()))) {
    LOG_ERROR("init config module failed", KR(ret));
  } else if (OB_FAIL(config_mgr_.save_configs(base_version))) {
    LOG_WARN("failed to save startup configs", KR(ret));
  } else if (OB_FAIL(config_mgr_.got_version())) {
    LOG_WARN("failed to got version", KR(ret));
  } else {
    config_mgr_.enable_static_effect();
    lib::g_runtime_enabled = true;
  }

  return ret;
}

int ObServer::init_opts_config(const ObServerOptions &opts, const char *optstr)
{
  int ret = OB_SUCCESS;

  if (opts.port_ != 0) {
    config_.mysql_port = opts.port_;
  }

  config_.syslog_level.set_value(OB_LOGGER.get_level_str());

  if (nullptr != optstr) {
    if (FAILEDx(config_.add_extra_config(optstr, start_time_))) {
      LOG_ERROR("invalid config from cmdline options", KCSTRING(optstr), KR(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(init_data_dir_and_redo_dir(opts))) {
    LOG_ERROR("init data dir and redo dir failed", KR(ret));
  }

  // The command line is specified, subject to the command line
  if (opts.use_ipv6_) {
    config_.use_ipv6 = opts.use_ipv6_;
  }

  return ret;
}

int ObServer::init_data_dir_and_redo_dir(const ObServerOptions &opts)
{
  int ret = OB_SUCCESS;

  // remove current_directory prefix of data_dir and redo_dir if exists
  char current_dir[PATH_MAX] = {0};
  if (nullptr == getcwd(current_dir, sizeof(current_dir))) {
    LOG_ERROR("failed to get current directory", K(ret), KCSTRING(strerror(errno)));
  } else if (current_dir[strlen(current_dir) - 1] == '/') {
  } else if (strlen(current_dir) + 2 >= PATH_MAX) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_ERROR("current directory is too long", K(ret), KCSTRING(current_dir));
  } else {
    current_dir[strlen(current_dir)] = '/';
    current_dir[strlen(current_dir) + 1] = '\0';
  }

  ObSqlString data_dir;
  ObSqlString redo_dir;
  if (!opts.data_dir_.empty()) {
    if (OB_FAIL(data_dir.assign(opts.data_dir_))) {
      LOG_ERROR("failed to assign data dir", K(ret));
    }
  } else if (nullptr == config_.data_dir.get_value() || 0 == strlen(config_.data_dir.get_value())) {
    if (OB_FAIL(data_dir.assign("store"))) {
      LOG_ERROR("failed to append data dir", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (!data_dir.empty()) {
    if (OB_FAIL(FileDirectoryUtils::create_full_path(data_dir.ptr()))) {
      LOG_ERROR("failed to create data dir", K(ret));
    } else if (OB_FAIL(FileDirectoryUtils::to_absolute_path(data_dir))) {
      LOG_ERROR("failed to convert data dir to absolute path", K(ret));
    } else {
      ObString tmp_data_dir(data_dir.length(), data_dir.ptr());
      if (tmp_data_dir.prefix_match(current_dir)) {
        tmp_data_dir.assign(tmp_data_dir.ptr() + strlen(current_dir), tmp_data_dir.length() - (int64_t)strlen(current_dir));
        while (tmp_data_dir.prefix_match("/")) {
          tmp_data_dir.assign(tmp_data_dir.ptr() + 1, tmp_data_dir.length() - 1);
        }
      }
      config_.data_dir.set_value(tmp_data_dir.ptr());
      LOG_INFO("set data dir", K(config_.data_dir));
    }
  }

  if (!opts.redo_dir_.empty()) {
    if (OB_FAIL(redo_dir.assign(opts.redo_dir_))) {
      LOG_ERROR("failed to assign redo dir", K(ret));
    }
  } else if (nullptr == config_.redo_dir.get_value() || 0 == strlen(config_.redo_dir.get_value())) {
    ObString tmp_data_dir(data_dir.length(), data_dir.ptr());
    if (tmp_data_dir.empty()) {
      tmp_data_dir.assign_ptr(config_.data_dir.get_value(), static_cast<ObString::obstr_size_t>(strlen(config_.data_dir.get_value())));
    }
    if (OB_FAIL(redo_dir.assign_fmt("%.*s/redo", tmp_data_dir.length(), tmp_data_dir.ptr()))) {
      LOG_ERROR("failed to append redo dir", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (!redo_dir.empty()) {
    if (OB_FAIL(FileDirectoryUtils::create_full_path(redo_dir.ptr()))) {
      LOG_ERROR("failed to create redo dir", K(ret));
    } else if (OB_FAIL(FileDirectoryUtils::to_absolute_path(redo_dir))) {
      LOG_ERROR("failed to convert redo dir to absolute path", K(ret));
    } else {
      ObString tmp_redo_dir(redo_dir.length(), redo_dir.ptr());
      if (tmp_redo_dir.prefix_match(current_dir)) {
        tmp_redo_dir.assign_ptr(tmp_redo_dir.ptr() + strlen(current_dir), static_cast<ObString::obstr_size_t>(tmp_redo_dir.length() - strlen(current_dir)));
        while (tmp_redo_dir.prefix_match("/")) {
          tmp_redo_dir.assign_ptr(tmp_redo_dir.ptr() + 1, tmp_redo_dir.length() - 1);
        }
      }
      config_.redo_dir.set_value(tmp_redo_dir.ptr());
      LOG_INFO("set redo dir", K(config_.redo_dir));
    }
  }
  return ret;
}

int ObServer::init_self_addr()
{
  int ret = OB_SUCCESS;

  const char *ip = nullptr;
  int32_t local_port = static_cast<int32_t>(config_.rpc_port);
  if (config_.use_ipv6) {
    ip = "::1";
  } else {
    ip = "127.0.0.1";
  }
  self_addr_.set_ip_addr(ip, local_port);

  if (OB_SUCC(ret)) {
    const char *syslog_file_info = ObServerUtils::build_syslog_file_info();
    OB_LOGGER.set_new_file_info(syslog_file_info);
    LOG_INFO("Build basic information for each syslog file", "info", syslog_file_info);

    // initialize self address
    rpc::g_server_self_addr = self_addr_;
    LOG_INFO("my addr", K_(self_addr));
    config_.self_addr_ = self_addr_;
  }

  return ret;
}

int ObServer::init_config_module(const char *optstr)
{
  int ret = OB_SUCCESS;

  // initialize configure module
  if (!self_addr_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("local address isn't valid", K(self_addr_), KR(ret));
  } else if (OB_FAIL(server_gtimer_.init("ServerGTimer", ObMemAttr("ServerGTimer")))) {
    LOG_ERROR("init timer fail", KR(ret));
  } else if (OB_FAIL(sql_mem_timer_.init("SqlMemTimer", ObMemAttr("SqlMemTimer")))) {
    LOG_ERROR("init sql memory manger timer fail", KR(ret));
  } else if (OB_FAIL(ctas_clean_up_timer_.init("CTASCleanUp", ObMemAttr("CTASCleanUp")))) {
    LOG_ERROR("fail to init ctas clean up timer", KR(ret));
  }

  return ret;
}

int ObServer::set_running_mode()
{
  int ret = OB_SUCCESS;
  const int64_t memory_limit = GMEMCONF.get_server_memory_limit();
  const int64_t cnt = GCONF.cpu_count;
  const int64_t cpu_cnt = cnt > 0 ? cnt : common::get_cpu_num();
  if (memory_limit < lib::ObRunningModeConfig::MINI_MEM_UPPER) {
    ObTaskController::get().allow_next_syslog();
    LOG_INFO("observer start with mini_mode", K(memory_limit));
    lib::update_mini_mode(memory_limit, cpu_cnt);
  } else {
    lib::update_mini_mode(memory_limit, cpu_cnt);
  }
  _OB_LOG(INFO, "mini mode: %s", lib::is_mini_mode() ? "true" : "false");
  return ret;
}

int ObServer::init_pre_setting()
{
  int ret = OB_SUCCESS;

  ObMallocSampleLimiter::set_interval(GCONF._max_malloc_sample_interval,
                                      GCONF._min_malloc_sample_interval);
  enable_memleak_light_backtrace(GCONF._enable_memleak_light_backtrace);

  // oblog configuration
  if (OB_SUCC(ret)) {
    const int max_log_cnt = static_cast<int32_t>(config_.max_syslog_file_count);
    const bool enable_async_syslog = config_.enable_async_syslog;
    const int64_t max_disk_size = config_.syslog_disk_size;
    const int64_t min_uncompressed_count = config_.syslog_file_uncompressed_count;
    const char *compress_func_ptr = config_.syslog_compress_func.str();
    OB_LOGGER.set_max_file_index(max_log_cnt);
    OB_LOGGER.set_record_old_log_file();
    OB_LOGGER.set_enable_async_log(enable_async_syslog);
    OB_LOG_COMPRESSOR.set_max_disk_size(max_disk_size);
    LOG_INFO("Whether compress syslog file", K(compress_func_ptr));
    OB_LOG_COMPRESSOR.set_compress_func(compress_func_ptr);
    OB_LOG_COMPRESSOR.set_min_uncompressed_count(min_uncompressed_count);
    LOG_INFO("init log config", K(enable_async_syslog),
             K(max_disk_size), K(compress_func_ptr), K(min_uncompressed_count));
    if (0 == max_log_cnt) {
      LOG_INFO("won't recycle log file");
    } else {
      LOG_INFO("recycle log file", "count", max_log_cnt);
    }
  }

  // task controller(log rate limiter)
  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObTaskController::get().init())) {
      LOG_ERROR("init task controller fail", KR(ret));
    } else {
      ObTaskController::get().set_log_rate_limit(config_.syslog_io_bandwidth_limit);
      ObTaskController::get().set_diag_per_error_limit(config_.diag_syslog_per_error_limit);
      ObTaskController::get().switch_task(share::ObTaskType::GENERIC);
    }
  }

  // total memory limit
  if (OB_SUCC(ret)) {
    const int64_t limit_memory = GMEMCONF.get_server_memory_limit();
    const int64_t hard_limit_memory = GMEMCONF.get_server_hard_memory_limit();
    const int64_t reserved_memory = std::min(config_.cache_wash_threshold.get_value(),
        static_cast<int64_t>(static_cast<double>(limit_memory) * KVCACHE_FACTOR));
    LOG_INFO("set memory config", K(hard_limit_memory), K(limit_memory), K(reserved_memory));
    set_hard_memory_limit(hard_limit_memory);
    set_memory_limit(limit_memory);
    ob_set_reserved_memory(reserved_memory);
  }
  if (OB_SUCC(ret)) {
    const int64_t default_stack_size = 1L << 18; // 256KB
    const int64_t stack_size = std::max(static_cast<int64_t>(default_stack_size), static_cast<int64_t>(GCONF.stack_size));
    LOG_INFO("set stack_size", K(stack_size));
    global_thread_stack_size = stack_size - THREAD_STACK_RESERVED_SIZE - ACHUNK_PRESERVE_SIZE;
#ifdef __APPLE__
    const int ps = getpagesize();
    global_thread_stack_size = (global_thread_stack_size + ps - 1) & ~(ps - 1);
#endif
  }
  if (OB_SUCC(ret) && GCONF.use_ipv6) {
    enable_use_ipv6();
  }
  return ret;
}

int ObServer::init_sql_proxy()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sql_conn_pool_.init(&schema_service_,
                                  &sql_engine_,
                                  &vt_data_service_.get_vt_iter_factory().get_vt_iter_creator(),
                                  &config_))) {
    LOG_ERROR("init sql connection pool failed", KR(ret));
  } else if (OB_FAIL(ddl_conn_pool_.init(&schema_service_,
                                  &sql_engine_,
                                  &vt_data_service_.get_vt_iter_factory().get_vt_iter_creator(),
                                  &config_,
                                  true/*use static type engine*/))) {
    LOG_ERROR("init sql connection pool failed", KR(ret));
  } else if (OB_FAIL(sql_proxy_.init(&sql_conn_pool_))) {
    LOG_ERROR("init sql proxy failed", KR(ret));
  } else if (OB_FAIL(ddl_sql_proxy_.init(&ddl_conn_pool_))) {
    LOG_ERROR("init ddl sql proxy failed", KR(ret));
  }
  return ret;
}

int ObServer::init_io()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(OB_FILE_SYSTEM_ROUTER.init(GCONF.data_dir, GCONF.redo_dir))) {
    LOG_ERROR("init OB_FILE_SYSTEM_ROUTER fail", KR(ret));
  }

  if (OB_SUCC(ret)) {
    static const double IO_MEMORY_RATIO = 0.2;
    if (OB_FAIL(ObIOManager::get_instance().init(GMEMCONF.get_reserved_server_memory() * IO_MEMORY_RATIO))) {
      LOG_ERROR("init io manager fail, ", KR(ret));
    } else {
      ObIOConfig io_config;
      int64_t cpu_cnt = GCONF.cpu_count;
      if (cpu_cnt <= 0) {
        cpu_cnt = common::get_cpu_num();
      }
      io_config.disk_io_thread_count_ = GCONF.disk_io_thread_count;
      io_config.sync_io_thread_count_ = GCONF.sync_io_thread_count;
      const int64_t max_io_depth = 256;
      if (OB_FAIL(ObIOManager::get_instance().set_io_config(io_config))) {
        LOG_ERROR("config io manager fail, ", KR(ret));
      } else {
        //allow load benchmark fail, please ignore return code.
        if (OB_FAIL(ObIOCalibration::get_instance().init())) {
          LOG_WARN("init io benchmark fail, ", KR(ret));
        }

        storage_env_.data_dir_ = OB_FILE_SYSTEM_ROUTER.get_data_dir();
        storage_env_.sstable_dir_ = OB_FILE_SYSTEM_ROUTER.get_sstable_dir();
        storage_env_.default_block_size_ = OB_DEFAULT_MACRO_BLOCK_SIZE;  // 2MB

        // log
        storage_env_.log_spec_.log_dir_ = OB_FILE_SYSTEM_ROUTER.get_slog_dir();
        storage_env_.log_spec_.max_log_file_size_ = ObLogConstants::MAX_LOG_FILE_SIZE;
        storage_env_.clog_dir_ = OB_FILE_SYSTEM_ROUTER.get_clog_dir();

        // cache
        storage_env_.bf_cache_miss_count_threshold_ = config_.bf_cache_miss_count_threshold;

        // policy
        storage_env_.clog_file_spec_ = OB_FILE_SYSTEM_ROUTER.get_clog_file_spec();
        storage_env_.slog_file_spec_ = OB_FILE_SYSTEM_ROUTER.get_slog_file_spec();

        int64_t data_disk_size = 0;
        int64_t log_disk_size = 0;
        int64_t data_disk_percentage = 0;
        int64_t log_disk_percentage = 0;

        if (OB_SUCC(ret) && OB_FAIL(log_block_mgr_.init(storage_env_.clog_dir_))) {
          LOG_ERROR("log block mgr init failed", KR(ret));
        } else if (OB_FAIL(ObServerUtils::cal_all_part_disk_size(config_.datafile_size,
                                                  config_.log_disk_size,
                                                  config_.datafile_disk_percentage,
                                                  config_.log_disk_percentage,
                                                  data_disk_size,
                                                  log_disk_size,
                                                  data_disk_percentage,
                                                  log_disk_percentage))) {
          LOG_ERROR("cal_all_part_disk_size failed", KR(ret));
        }
        if (OB_SUCC(ret)) {
          storage_env_.data_disk_size_ = data_disk_size;
          storage_env_.data_disk_percentage_ = data_disk_percentage;
          storage_env_.log_disk_size_ = log_disk_size;
          storage_env_.log_disk_percentage_ = log_disk_percentage;
        }

        if (OB_SUCC(ret)) {
          if (OB_FAIL(ObIODeviceWrapper::get_instance().init(
                storage_env_.data_dir_,
                storage_env_.sstable_dir_,
                storage_env_.default_block_size_,
                storage_env_.data_disk_percentage_,
                storage_env_.data_disk_size_))) {
            LOG_ERROR("fail to init io device wrapper", KR(ret), K_(storage_env));
          } else if (OB_FAIL(ObIOManager::get_instance().add_device_channel(&LOCAL_DEVICE_INSTANCE,
                                                                            io_config.disk_io_thread_count_,
                                                                            io_config.sync_io_thread_count_,
                                                                            max_io_depth))) {
            LOG_ERROR("add device channel failed", KR(ret));
          }
        }
      }
    }
  }
  return ret;
}

int ObServer::init_interrupt()
{
  int ret = OB_SUCCESS;
  ObGlobalInterruptManager *mgr = ObGlobalInterruptManager::getInstance();
  if (OB_ISNULL(mgr)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail get interrupt mgr instance", KR(ret));
  } else if (OB_FAIL(mgr->init(get_self()))) {
    LOG_ERROR("fail init interrupt mgr", KR(ret));
  }
  return ret;
}

int ObServer::init_fts()
{
  int ret = ObFTParseData::init_global();
  if (OB_FAIL(ret)) {
    LOG_ERROR("failed to initialize fulltext parser data", KR(ret));
  } else {
    LOG_INFO("fulltext parser data initialized");
  }
  return ret;
}

void ObServer::deinit_fts()
{
  ObFTParseData::deinit_global();
  LOG_INFO("fulltext parser data deinitialized");
}

int ObServer::init_loaddata_global_stat()
{
  int ret = OB_SUCCESS;
  ObGlobalLoadDataStatMap *map = ObGlobalLoadDataStatMap::getInstance();
  if (OB_ISNULL(map)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail allocate load data map for status", KR(ret));
  } else if (OB_FAIL(map->init())) {
    LOG_ERROR("fail init load data map", KR(ret));
  }
  return ret;
}

int ObServer::init_network()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(net_frame_.init())) {
    LOG_ERROR("init server network fail");
  }

  return ret;
}

int ObServer::init_server_runtime()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(server_runtime_controller_.init())) {
    LOG_ERROR("init server runtime fail", KR(ret));

  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(duty_task_.schedule(server_gtimer_))) {
      LOG_ERROR("schedule server duty task fail", KR(ret));
    } else if (OB_FAIL(sql_mem_task_.schedule(sql_mem_timer_))) {
      LOG_ERROR("schedule SQL memory manager task fail", KR(ret));
    }
  }

  return ret;
}

namespace
{
// ObSchemaService factory injection: implementation ObSchemaServiceSQLImpl lives in the observer layer(can legally call sql)。
share::schema::ObSchemaService *create_schema_service_sql_impl()
{
  return OB_NEW(share::schema::ObSchemaServiceSQLImpl, ObModIds::OB_SCHEMA_SERVICE);
}
void destroy_schema_service_sql_impl(share::schema::ObSchemaService *schema_service)
{
  share::schema::ObSchemaServiceSQLImpl *tmp =
      static_cast<share::schema::ObSchemaServiceSQLImpl *>(schema_service);
  OB_DELETE(ObSchemaServiceSQLImpl, ObModIds::OB_SCHEMA_SERVICE, tmp);
}
} // anonymous namespace

int ObServer::init_schema()
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaServiceFactory::register_creator(
      create_schema_service_sql_impl, destroy_schema_service_sql_impl);
  if (OB_FAIL(schema_service_.init(&sql_proxy_, &config_,
                                   OB_MAX_VERSION_COUNT))) {
    LOG_WARN("init schema_service_ fail", KR(ret));
  }

  return ret;
}

int ObServer::init_autoincrement_service()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObAutoincrementService::get_instance().init(&sql_proxy_))) {
    LOG_ERROR("init autoincrement_service_ fail", KR(ret));
  }
  return ret;
}

int ObServer::init_tablet_autoincrement_service()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTabletAutoincrementService::get_instance().init())) {
    LOG_WARN("init tablet_autoincrement_service_ fail", KR(ret));
  } else if (OB_FAIL(ObTabletAutoincSeqRpcHandler::get_instance().init())) {
    LOG_WARN("init tablet autoinc seq rpc handler fail", K(ret));
  }
  return ret;
}

int ObServer::init_global_kvcache()
{
  int ret = OB_SUCCESS;
  int64_t bucket_num;
  const int64_t max_cache_size = MIN(GMEMCONF.get_server_memory_limit(), ObKVGlobalCache::DEFAULT_MAX_CACHE_SIZE);
  if (OB_FAIL(ObKVGlobalCache::get_instance().get_suitable_bucket_num(bucket_num))) {
    LOG_WARN("Failed to get suitable bucket num");
  } else if (OB_FAIL(ObKVGlobalCache::get_instance().init(&ObServerMemLimitGetter::get_instance(),
                                                   bucket_num,
                                                   max_cache_size))) {
    LOG_WARN("Fail to init ObKVGlobalCache, ", KR(ret));
  } else if (OB_FAIL(ObResourceMgr::get_instance().set_cache_washer(
      ObKVGlobalCache::get_instance()))) {
    LOG_ERROR("Fail to set_cache_washer", KR(ret));
  }

  return ret;
}

int ObServer::init_ob_service(bool need_bootstrap)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ob_service_.init(sql_proxy_, need_bootstrap))) {
    LOG_ERROR("oceanbase service init failed", KR(ret));
  }
  return ret;
}

int ObServer::init_local_management_service(const bool need_bootstrap)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(local_management_service_.init(
                 config_, config_mgr_,
                 self_addr_, sql_proxy_,
                 &schema_service_, need_bootstrap))) {
    LOG_ERROR("init local management service failed", K(ret));
  }

  return ret;
}

int ObServer::init_sql()
{
  int ret = OB_SUCCESS;

  LOG_INFO("init sql");
  if (OB_FAIL(session_mgr_.init())) {
    LOG_ERROR("init sql session mgr fail");
  } else if (OB_FAIL(conn_res_mgr_.init(schema_service_, server_gtimer_))) {
    LOG_ERROR("init user resource mgr failed", KR(ret));
  } else if (OB_FAIL(server_gtimer_.schedule(session_mgr_,
                                             ObSQLSessionMgr::SCHEDULE_PERIOD, true))) {
    LOG_ERROR("tier schedule fail");
  } else {
    LOG_INFO("init sql session mgr done");
    LOG_INFO("init sql location cache done");
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(sql_engine_.init(
                    &ObOptStatManager::get_instance(),
                    &vt_data_service_,
                    self_addr_))) {
      LOG_ERROR("init sql engine failed", KR(ret));
    } else {
      LOG_INFO("init sql engine done");
    }
  }

  if (OB_SUCC(ret)) {
    if (nullptr == dtl::ObDtl::instance()) {
      ret = OB_INIT_FAIL;
      LOG_ERROR("allocate DTL service fail", KR(ret));
    } else if (OB_FAIL(DTL.init())) {
      LOG_ERROR("fail initialize DTL instance", KR(ret));
    }
  }

  if (OB_SUCC(ret)) {
    ObLibXml2SaxHandler::init();
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObRbMemMgr::init_memory_hook())) {
      LOG_ERROR("fail initialize roaring memory hook", KR(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObPluginVectorIndexUtils::set_vsag_logger())) {
      LOG_ERROR("failed to initialize VSAG LOGGER.", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("init sql done");
  } else {
    LOG_ERROR("init sql fail", KR(ret));
  }
  return ret;
}

int ObServer::init_sql_runner()
{
  int ret = OB_SUCCESS;
  LOG_INFO("init sql runner done");
  return ret;
}


int ObServer::init_pl()
{
  int ret = OB_SUCCESS;
  LOG_INFO("init pl");
  if (OB_FAIL(pl_engine_.init(sql_proxy_))) {
    LOG_ERROR("init pl engine failed", KR(ret));
  } else {
    LOG_INFO("init pl engine done");
  }
  return ret;
}

int ObServer::init_global_context()
{
  int ret = OB_SUCCESS;

  gctx_.local_management_service_ = &local_management_service_;
  gctx_.ob_service_ = &ob_service_;
  gctx_.schema_service_ = &schema_service_;
  gctx_.config_ = &config_;
  gctx_.config_mgr_ = &config_mgr_;
  gctx_.tablet_operator_ = &tablet_operator_;
  gctx_.meta_db_pool_ = &meta_db_pool_;
  gctx_.sql_proxy_ = &sql_proxy_;
  gctx_.ddl_sql_proxy_ = &ddl_sql_proxy_;
  gctx_.self_addr_seq_.set_addr(self_addr_);
  gctx_.bandwidth_throttle_ = &bandwidth_throttle_;
  gctx_.vt_par_ser_ = &vt_data_service_;
  gctx_.session_mgr_ = &session_mgr_;
  gctx_.sql_engine_ = &sql_engine_;
  gctx_.pl_engine_ = &pl_engine_;
  gctx_.conn_res_mgr_ = &conn_res_mgr_;
  gctx_.server_runtime_controller_ = &server_runtime_controller_;
  gctx_.vt_iter_creator_ = &vt_data_service_.get_vt_iter_factory().get_vt_iter_creator();
  gctx_.start_time_ = start_time_;
  gctx_.warm_up_start_time_ = &warm_up_start_time_;
  gctx_.status_ = SS_INIT;
  gctx_.rs_server_status_ = RSS_INVALID;
  gctx_.start_service_time_ = 0;
  gctx_.ssl_key_expired_time_ = 0;
  gctx_.diag_ = &diag_;
  gctx_.scramble_rand_ = &scramble_rand_;
  gctx_.init();
  gctx_.schema_status_proxy_ = &schema_status_proxy_;
  gctx_.net_frame_ = &net_frame_;

  gctx_.disk_reporter_ = &disk_usage_report_task_;
  gctx_.log_block_mgr_ = &log_block_mgr_;
  gctx_.startup_accel_handler_ = &startup_accel_handler_;

  gctx_.in_bootstrap_ = false;
  gctx_.inited_ = true;

  return ret;
}

int ObServer::parse_role(const ObServerOptions &opts)
{
  int ret = OB_SUCCESS;

  // Parse role
  if (opts.role_.empty()) {
    // Default to PRIMARY
    gctx_.server_role_ = share::ObServerRole::PRIMARY_ROLE;
    LOG_INFO("role not specified, default to PRIMARY");
  } else {
    common::ObString role_str(opts.role_.length(), opts.role_.ptr());
    if (0 == role_str.case_compare("PRIMARY")) {
      gctx_.server_role_ = share::ObServerRole::PRIMARY_ROLE;
    } else if (0 == role_str.case_compare("STANDBY")) {
      gctx_.server_role_ = share::ObServerRole::STANDBY_ROLE;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("invalid role", K(opts.role_));
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("role parsed",
        "role", gctx_.server_role_ == share::ObServerRole::PRIMARY_ROLE
            ? "PRIMARY" : "STANDBY");
  }

  return ret;
}

int ObServer::init_px_target_mgr()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(OB_PX_TARGET_MONITOR.init(self_addr_))) {
    LOG_ERROR("px target mgr init failed", K(self_addr_), KR(ret));
  } else {
    LOG_INFO("px target mgr init success");
  }
  return ret;
}

int ObServer::init_storage()
{
  int ret = OB_SUCCESS;

  bool clogdir_is_empty = false;

  if (OB_SUCC(ret)) {
    int64_t log_disk_in_use = 0;
    // Check if the clog directory is empty
    if (OB_FAIL(log_block_mgr_.get_disk_usage(log_disk_in_use))) {
      LOG_ERROR("ObServerLogBlockMgr get_disk_usage failed", K(ret));
    } else if (0 == log_disk_in_use
        && OB_FAIL(logservice::ObServerLogBlockMgr::check_clog_directory_is_empty(
            OB_FILE_SYSTEM_ROUTER.get_clog_dir(), clogdir_is_empty))) {
      LOG_ERROR("is_dir_empty fail", K(ret));
    } else if (clogdir_is_empty) {
      LOG_INFO("clog dir is empty");
    } else {
      clogdir_is_empty = log_disk_in_use == 0;
    }
  }

  if (OB_SUCC(ret)) {
    is_log_dir_empty_ = clogdir_is_empty;
  }

  if (OB_SUCC(ret)) {
    storage_env_.ethernet_speed_ = ethernet_speed_;
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(OB_STORE_CACHE.init(storage_env_.bf_cache_miss_count_threshold_))) {
      LOG_WARN("Fail to init OB_STORE_CACHE, ", KR(ret), K(storage_env_.data_dir_));
    } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.init(
        storage_env_.default_block_size_))) {
      LOG_ERROR("init storage object mgr fail", KR(ret));
    } else if (OB_FAIL(disk_usage_report_task_.init(sql_proxy_))) {
      LOG_WARN("fail to init disk usage report task", KR(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObDDLCtrlSpeedHandle::get_instance().init(server_gtimer_))) {
      LOG_WARN("fail to init ObDDLCtrlSpeedHandle", KR(ret));
    }
  }
  return ret;
}

int ObServer::init_tx_data_cache()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(OB_TX_DATA_KV_CACHE.init("tx_data_kv_cache"))) {
    LOG_WARN("init OB_TX_DATA_KV_CACHE failed", KR(ret));
  }
  return ret;
}

char* strtrim(char* str)
{
  char* ptr;

  if (str == NULL) {
    return NULL;
  }

  ptr = str + strlen(str) - 1;
  while (isspace(*str)) {
    str++;
  }

  while ((ptr > str) && isspace(*ptr)) {
    *ptr-- = '\0';
  }

  return str;
}

static int64_t nic_rate_parse(const char *str, bool &valid)
{
  char *p_unit = nullptr;
  int64_t value = 0;

  if (OB_ISNULL(str) || '\0' == str[0]) {
    valid = false;
  } else {
    valid = true;
    value = strtoll(str, &p_unit, 0);
    p_unit = strtrim(p_unit);

    if (OB_ISNULL(p_unit)) {
      valid = false;
    } else if (value <= 0) {
      valid = false;
    } else if (0 == STRCASECMP("bit", p_unit)
               || 0 == STRCASECMP("b", p_unit)) {
      // do nothing
    } else if (0 == STRCASECMP("kbit", p_unit)
               || 0 == STRCASECMP("kb", p_unit)
               || 0 == STRCASECMP("k", p_unit)) {
      value <<= 10;
    } else if ('\0' == *p_unit
               || 0 == STRCASECMP("mbit", p_unit)
               || 0 == STRCASECMP("mb", p_unit)
               || 0 == STRCASECMP("m", p_unit)) {
      // default is meta bit
      value <<= 20;
    } else if (0 == STRCASECMP("gbit", p_unit)
               || 0 == STRCASECMP("gb", p_unit)
               || 0 == STRCASECMP("g", p_unit)) {
      value <<= 30;
    } else {
      valid = false;
      LOG_ERROR_RET(OB_ERR_UNEXPECTED, "parse nic rate error", K(str), K(p_unit));
    }
  }
  return value;
}

int ObServer::get_network_speed_from_config_file(int64_t &network_speed)
{
  int ret = OB_SUCCESS;
  const char *nic_rate_path = "etc/nic.rate.config";
  const int64_t MAX_NIC_CONFIG_FILE_SIZE = 1 << 10; // 1KB
  FILE *fp = nullptr;
  char *buf = nullptr;
  static int nic_rate_file_exist = 1;

  if (OB_ISNULL(buf = static_cast<char *>(ob_malloc(MAX_NIC_CONFIG_FILE_SIZE + 1,
                                                           ObModIds::OB_BUFFER)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("alloc buffer failed", LITERAL_K(MAX_NIC_CONFIG_FILE_SIZE), KR(ret));
  } else if (OB_ISNULL(fp = fopen(nic_rate_path, "r"))) {
    if (ENOENT == errno) {
      ret = OB_FILE_NOT_EXIST;
      if (nic_rate_file_exist) {
        LOG_WARN("NIC Config file doesn't exist, auto detecting", K(nic_rate_path), KR(ret));
        nic_rate_file_exist = 0;
      }
    } else {
      ret = OB_IO_ERROR;
      if (EAGAIN == errno) {
        LOG_WARN("Can't open NIC Config file", K(nic_rate_path), K(errno), KR(ret));
      } else {
        LOG_ERROR("Can't open NIC Config file", K(nic_rate_path), K(errno), KR(ret));
      }
    }
  } else {
    if (!nic_rate_file_exist) {
      LOG_INFO("Reading NIC Config file", K(nic_rate_path));
      nic_rate_file_exist = 1;
    }
    memset(buf, 0, MAX_NIC_CONFIG_FILE_SIZE + 1);
    // ignore return value of fread, because ferror can get fread state
    IGNORE_RETURN fread(buf, 1, MAX_NIC_CONFIG_FILE_SIZE, fp);
    char *prate = nullptr;

    if (OB_UNLIKELY(0 != ferror(fp))) {
      ret = OB_IO_ERROR;
      LOG_ERROR("Read NIC Config file error", K(nic_rate_path), KR(ret));
    } else if (OB_UNLIKELY(0 == feof(fp))) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_ERROR("NIC Config file is too long", K(nic_rate_path), KR(ret));
    } else {
      prate = strchr(buf, '=');
      if (nullptr != prate) {
        prate++;
        bool valid = false;
        int64_t nic_rate = nic_rate_parse(prate, valid);
        if (valid) {
          network_speed = nic_rate / 8;
        } else {
          ret = OB_INVALID_ARGUMENT;
          LOG_ERROR("invalid NIC Rate Config", KR(ret));
        }
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_ERROR("invalid NIC Config file", KR(ret));
      }
    } // else

    if (OB_UNLIKELY(0 != fclose(fp))) {
      ret = OB_IO_ERROR;
      LOG_ERROR("Close NIC Config file failed", KR(ret));
    }
  } // else
  if (OB_LIKELY(nullptr != buf)) {
    ob_free(buf);
    buf = nullptr;
  }
  return ret;
}

int ObServer::init_bandwidth_throttle()
{
  int ret = OB_SUCCESS;
  int64_t network_speed = DEFAULT_ETHERNET_SPEED;

  sys_bkgd_net_percentage_ = config_.sys_bkgd_net_percentage;
  if (network_speed > 0) {
    int64_t rate = network_speed * sys_bkgd_net_percentage_ / 100;
    if (OB_FAIL(bandwidth_throttle_.init(rate))) {
      LOG_ERROR("failed to init bandwidth throttle", KR(ret), K(rate), K(network_speed));
    } else {
      LOG_INFO("succeed to init_bandwidth_throttle",
          K(sys_bkgd_net_percentage_),
          K(network_speed),
          K(rate));
      ethernet_speed_ = network_speed;
    }
  }
  return ret;
}

int ObServer::reload_config()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(OB_STORE_CACHE.set_bf_cache_miss_count_threshold(GCONF.bf_cache_miss_count_threshold))) {
    LOG_WARN("set bf_cache_miss_count_threshold fail", KR(ret));
  }

  return ret;
}

ObServer::ObCTASCleanUpTask::ObCTASCleanUpTask()
: obs_(nullptr), is_inited_(false)
{}

int ObServer::ObCTASCleanUpTask::init(ObServer *obs, common::ObTimer &timer)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("ObCTASCleanUpTask has already been inited", KR(ret));
  } else if (OB_ISNULL(obs)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ObCTASCleanUpTask init with null ptr", KR(ret), K(obs));
  } else {
    obs_ = obs;
    is_inited_ = true;
    disable_timeout_check();
    if (OB_FAIL(timer.schedule(*this, CLEANUP_INTERVAL, true /*schedule repeatly*/))) {
      LOG_ERROR("fail to schedule task ObCTASCleanUpTask", KR(ret));
    }
  }
  return ret;
}


void ObServer::ObCTASCleanUpTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  bool need_ctas_cleanup = ATOMIC_BCAS(&obs_->need_ctas_cleanup_, true, false);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObCTASCleanUpTask has not been inited", KR(ret));
  } else if (false == need_ctas_cleanup) {
    LOG_DEBUG("CTAS cleanup task skipped this time");
  } else if (OB_ISNULL(obs_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("CTAS cleanup task got null ptr", KR(ret));
  } else if (OB_FAIL(obs_->clean_up_invalid_tables())) {
    LOG_WARN("CTAS clean up task failed", KR(ret));
    ATOMIC_STORE(&obs_->need_ctas_cleanup_, true);
  } else {
    LOG_DEBUG("CTAS clean up task succeed");
  }
}

//Traverse the current session and determine whether the given table schema needs to be deleted according to the session id and last active time
bool ObServer::ObCTASCleanUp::operator()(sql::ObSQLSessionMgr::Key key,
                                         sql::ObSQLSessionInfo *sess_info)
{
  int ret = OB_SUCCESS;
  if (false == get_drop_flag()) {
    //do nothing
  } else if (OB_ISNULL(sess_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is NULL", KR(ret));
  } else if (static_cast<uint64_t>(key.sessid_) == get_session_id()) {
    if (OB_FAIL(sess_info->try_lock_query())) {
      if (OB_UNLIKELY(OB_EAGAIN != ret)) {
        LOG_WARN("fail to try lock query", KR(ret));
      } else {
        ret = OB_SUCCESS;
        ATOMIC_STORE(&obs_->need_ctas_cleanup_, true); //1, The current session is in use, there is suspicion, need to continue to check in the next scheduling
        LOG_DEBUG("try lock query fail with code OB_EGAIN",
            K(sess_info->get_server_sid()), K(sess_info->get_sessid_for_table()));
      }
      set_drop_flag(false);
    } else if (ObCTASCleanUp::CTAS_RULE == get_cleanup_type()) { //2, Query build table cleanup
      if (sess_info->get_last_active_time() < get_schema_version() + 100) { //The reason for +100 is to allow a certain error in the time stamp comparison
        (void)sess_info->unlock_query();
        set_drop_flag(false);
        ATOMIC_STORE(&obs_->need_ctas_cleanup_, true); //The current session is creating a table and needs to continue to check in the next schedule
        LOG_INFO("current table is in status of creating", K(sess_info->get_last_active_time()));
      } else {
        (void)sess_info->unlock_query();
        LOG_INFO("current table was in status of creating", K(sess_info->get_last_active_time()));
      }
    } else if (ObCTASCleanUp::TEMP_TAB_RULE == get_cleanup_type()) { //3, Directly connected temporary table cleanup
      if (sess_info->get_sess_create_time() < get_schema_version() + 100) {
        (void)sess_info->unlock_query();
        set_drop_flag(false);
        ATOMIC_STORE(&obs_->need_ctas_cleanup_, true); //The session that created the temporary table is still alive and needs to be checked in the next schedule
        LOG_DEBUG("session that creates temporary table is still alive");
      } else {
        (void)sess_info->unlock_query();
        LOG_DEBUG("current session reusing session id that created temporary table", K(sess_info->get_sess_create_time()));
      }
    }
  }
  return OB_SUCCESS == ret;
}

ObServer::ObRefreshCpuFreqTimeTask::ObRefreshCpuFreqTimeTask()
: obs_(nullptr), is_inited_(false)
{}

int ObServer::ObRefreshCpuFreqTimeTask::init(ObServer *obs, common::ObTimer &timer)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("ObRefreshCpuFreqTimeTask has already been inited", KR(ret));
  } else if (OB_ISNULL(obs)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ObRefreshCpuFreqTimeTask init with null ptr", KR(ret), K(obs));
  } else {
    obs_ = obs;
    is_inited_ = true;
    if (OB_FAIL(timer.schedule(*this, REFRESH_INTERVAL, true /*schedule repeatly*/))) {
      LOG_ERROR("fail to schedule task ObRefreshCpuFreqTimeTask", KR(ret));
    }
  }
  return ret;
}


void ObServer::ObRefreshCpuFreqTimeTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObRefreshCpuFreqTimeTask has not been inited", KR(ret));
  } else if (OB_ISNULL(obs_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ObRefreshCpuFreqTimeTask task got null ptr", KR(ret));
  } else if (OB_FAIL(obs_->refresh_cpu_frequency())) {
    LOG_ERROR("ObRefreshCpuFreqTimeTask task failed", KR(ret));
  }
}

int ObServer::refresh_cpu_frequency()
{
  int ret = OB_SUCCESS;
  uint64_t cpu_frequency = get_cpufreq_khz();

  if (0 == cpu_frequency) {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "get cpu frequency failed");
    cpu_frequency = ObServer::DEFAULT_CPU_FREQUENCY;
  }
  if (cpu_frequency != cpu_frequency_) {
    LOG_INFO("Cpu frequency changed", "from", cpu_frequency_, "to", cpu_frequency);
    cpu_frequency_ = cpu_frequency;
  }

  return ret;
}

int ObServer::init_ctas_clean_up_task()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ctas_clean_up_task_.init(this, ctas_clean_up_timer_))) {
    LOG_ERROR("fail to init ctas clean up task", KR(ret));
  }
  return ret;
}

int ObServer::init_redef_heart_beat_task()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(redef_table_heart_beat_task_.init(server_gtimer_))) {
    LOG_ERROR("fail to init redef heart beat task", KR(ret));
  }
  return ret;
}

int ObServer::init_ddl_heart_beat_task_container()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(OB_DDL_HEART_BEAT_TASK_CONTAINER.init())) {
    LOG_ERROR("fail to init ddl heart beat task container", K(ret));
  }
  return ret;
}

int ObServer::init_refresh_cpu_frequency()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(refresh_cpu_frequency_task_.init(this, server_gtimer_))) {
    LOG_ERROR("fail to init refresh cpu frequency task", KR(ret));
  }
  return ret;
}

// @@Query cleanup rules for built tables and temporary tables:
//1, Traverse all table_schema, if the session_id of table T <> 0 means that the table is being created or the previous creation failed or the temporary table is to be cleared, then enter 2#;
//2, Create a table for the query: traverse the session, and determine whether T should be DROP according to the session_id and time of the session and table T;
//2.1, there is session->id = T->session_id,
//     a), the last active time of the session <the creation time of T, the table T is in the process of being created and cannot be DROP;
//     b), the last active time of the session >= the creation time of T, sess_id is reused, the ession of the original table T has been disconnected, and you can DROP;
//2.2, there is no session, its id = T->session_id, T can be DROP;
//3. For temporary tables, the judgment deletion condition is the same as 2#,
//   except that the session creation time is used instead of the last active time.
//It has been optimized before calling this interface, only need_ctas_cleanup_=true will be here
// Temporary table cleanup is performed in the DML resolve phase for the first
// temporary table access after session creation to avoid frequent background deletes.
int ObServer::clean_up_invalid_tables()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObDatabaseSchema *database_schema = NULL;
  ObArray<uint64_t> table_ids;
  obcall::ObDropTableArg drop_table_arg;
  obcall::ObTableItem table_item;
  if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("fail to get schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_ids_in_runtime(table_ids))) {
    LOG_WARN("fail to get table schema", K(ret));
  } else {
    ObCTASCleanUp ctas_cleanup(this, true);
    drop_table_arg.if_exist_ = true;
    drop_table_arg.to_recyclebin_ = false;
    // only OB_ISNULL(GCTX.session_mgr_) will exit the loop
    for (int64_t i = 0; i < table_ids.count() && OB_SUCC(tmp_ret); i++) {
      const ObSimpleTableSchemaV2 *table_schema = NULL;
      const uint64_t table_id = table_ids.at(i);
      // schema guard cannot be used repeatedly in iterative logic,
      // otherwise it will cause a memory hike in schema cache
      if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
        LOG_WARN("get schema guard failed", K(ret));
      } else if (OB_FAIL(schema_guard.get_simple_table_schema( table_id, table_schema))) {
        LOG_WARN("get simple table schema failed", K(ret), KT(table_id));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("got invalid schema", KR(ret), K(i));
      } else if (0 == table_schema->get_session_id()) {
        //do nothing
      } else {
        LOG_DEBUG("table is creating or encountered error or is temporary one", K(*table_schema));
        ctas_cleanup.set_drop_flag(false);
        if (table_schema->is_tmp_table()) {
          ctas_cleanup.set_cleanup_type(ObCTASCleanUp::TEMP_TAB_RULE);
        } else {
          ctas_cleanup.set_cleanup_type(ObCTASCleanUp::CTAS_RULE);
        }
        if (false == ctas_cleanup.get_drop_flag()) {
          ctas_cleanup.set_session_id(table_schema->get_session_id());
          ctas_cleanup.set_schema_version(table_schema->get_schema_version());
          ctas_cleanup.set_drop_flag(true);
          if (OB_ISNULL(GCTX.session_mgr_)) {
            tmp_ret = OB_ERR_UNEXPECTED;
            LOG_ERROR("session mgr is null", KR(ret));
          } else if (OB_FAIL(GCTX.session_mgr_->for_each_session(ctas_cleanup))) {
            LOG_WARN("failed to traverse each session to check table need be dropped", KR(ret), K(*table_schema));
          }
        }
        if (ctas_cleanup.get_drop_flag()) {
          LOG_INFO("a table will be dropped!", K(*table_schema));
          obcall::ObDDLRes res;
          database_schema = NULL;
          drop_table_arg.tables_.reset();
          drop_table_arg.if_exist_ = true;
          
          
          
          drop_table_arg.table_type_ = table_schema->get_table_type();
          drop_table_arg.session_id_ = table_schema->get_session_id();
          drop_table_arg.to_recyclebin_ = false;
          table_item.table_name_ = table_schema->get_table_name_str();
          table_item.mode_ = table_schema->get_name_case_mode();
          if (OB_FAIL(schema_guard.get_database_schema( table_schema->get_database_id(), database_schema))) {
            LOG_WARN("failed to get database schema", K(ret));
          } else if (OB_ISNULL(database_schema)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("database schema is null", KR(ret));
          } else if (database_schema->is_in_recyclebin() || table_schema->is_in_recyclebin()) {
            LOG_DEBUG("skip table schema in recyclebin", K(*table_schema));
          } else if (FALSE_IT(table_item.database_name_ = database_schema->get_database_name_str())) {
            //impossible
          } else if (OB_FAIL(drop_table_arg.tables_.push_back(table_item))) {
            LOG_WARN("failed to add table item!", K(table_item), K(ret));
          } else if (OB_FAIL(rootserver::local_ddl_serial_call([&]{ return GCTX.local_management_service_->drop_table(drop_table_arg, res); }))) {
            LOG_WARN("failed to drop table", K(drop_table_arg), K(table_item), KR(ret));
          } else {
            LOG_INFO("a table is dropped due to previous error or is a temporary one", K(i), "table_name", table_item.table_name_);
          }
        } else {
          LOG_DEBUG("no need to drop table", K(i));
        }
      }
    }
  }
  return ret;
}

void set_server_stop()
{
  ObServer::get_instance().set_stop();
}

} // end of namespace observer
} // end of namespace oceanbase
