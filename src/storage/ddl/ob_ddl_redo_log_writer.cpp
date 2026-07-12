#include "lib/stat/ob_diagnostic_info_guard.h"
#include "share/ob_ex_rpc.h"
#include "share/rc/ob_module_provider.h"
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

#define USING_LOG_PREFIX STORAGE

#include "ob_ddl_redo_log_writer.h"
#include "storage/ob_storage_rpc.h"
#include "storage/ob_storage_rpc_arg.h"
#include "logservice/ob_log_service.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/ddl/ob_ddl_merge_task.h"
#include "storage/ddl/ob_direct_insert_sstable_ctx_new.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "share/ob_ddl_sim_point.h"
#include "storage/blocksstable/index_block/ob_macro_meta_temp_store.h"
#include "storage/ddl/ob_ddl_merge_schedule.h"

using namespace oceanbase::common;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::logservice;
using namespace oceanbase::share;
using namespace oceanbase::blocksstable;
using namespace oceanbase::transaction;

bool ObDDLFullNeedStopWriteChecker::check_need_stop_write()
{
  return ddl_kv_mgr_handle_.get_obj()->get_count() >= ObTabletDDLKvMgr::MAX_DDL_KV_CNT_IN_STORAGE - 1;
}

bool ObDDLIncNeedStopWriteChecker::check_need_stop_write()
{
  int ret = OB_SUCCESS;
  bool ret_value = false;
  ObProtectedMemtableMgrHandle *memtable_mgr_handle = nullptr;
  if (OB_FAIL(tablet_.get_protected_memtable_mgr_handle(memtable_mgr_handle))) {
    LOG_WARN("failed to get protected memtable mgr handle", K(ret));
  } else if (memtable_mgr_handle->get_memtable_count() >= common::MAX_MEMSTORE_CNT - 1) {
    ret_value = true;
  }
  return ret_value;
}

int ObDDLCtrlSpeedItem::init(const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("inited twice", K(ret));
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ls id is invalid", K(ret), K(ls_id));
  } else {
    ls_id_ = ls_id;
    next_available_write_ts_ = ObTimeUtility::current_time();
    if (OB_FAIL(refresh())) {
      LOG_WARN("fail to init write speed and clog disk used threshold", K(ret));
    } else {
      is_inited_ = true;
      LOG_INFO("succeed to init ObDDLCtrlSpeedItem", K(ret), K(is_inited_), K(ls_id_),
        K(next_available_write_ts_), K(write_speed_), K(disk_used_stop_write_threshold_));
    }
  }
  return ret;
}

// refrese ddl clog write speed and disk used threshold on tenant level.
int ObDDLCtrlSpeedItem::refresh()
{
  int ret = OB_SUCCESS;
  int64_t archive_speed = 0;
  int64_t refresh_speed = 0;
  bool ignore = false;
  bool force_wait = false;
  int64_t total_used_space = 0; // for current tenant, used bytes.
  int64_t total_disk_space = 0; // for current tenant, limit used bytes.
  ObLSHandle ls_handle;
  palf::PalfOptions palf_opt;
  logservice::ObLogService *log_service = share::g_mp->log_service();
  if (OB_ISNULL(log_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, nullptr found", K(ret), KP(log_service));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls_id_, ls_handle, ObLSGetMod::DDL_MOD))) {
    if (OB_LS_NOT_EXIST == ret) {
      // log stream may be removed during timer refresh task.
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to get ls", K(ret), K(ls_id_));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(log_service->get_palf_options(palf_opt))) {
    LOG_WARN("fail to get palf_options", K(ret));
  } else if (OB_FAIL(log_service->get_palf_disk_usage(total_used_space, total_disk_space))) {
    STORAGE_LOG(WARN, "failed to get the disk space that clog used", K(ret));
  } else if (OB_ISNULL(GCTX.bandwidth_throttle_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, bandwidth throttle is null", K(ret), KP(GCTX.bandwidth_throttle_));
  } else if (OB_FAIL(GCTX.bandwidth_throttle_->get_rate(refresh_speed))) {
    LOG_WARN("fail to get rate", K(ret), K(refresh_speed));
  } else {
    // archive is not on if ignore = true.
    write_speed_ = ignore ? std::max(refresh_speed, 1 * MIN_WRITE_SPEED) : std::max(archive_speed, 1 * MIN_WRITE_SPEED);
    disk_used_stop_write_threshold_ = min(0 == palf_opt.disk_options_.log_disk_utilization_threshold_ ?
                                          palf::DEFAULT_LOG_UTL_THRESHOLD : palf_opt.disk_options_.log_disk_utilization_threshold_,
                                          palf_opt.disk_options_.log_disk_utilization_limit_threshold_);
    need_stop_write_ = 100.0 * total_used_space / total_disk_space >= disk_used_stop_write_threshold_ ? true : false;
  }
  LOG_DEBUG("current ddl clog write speed", K(ret), K(need_stop_write_), K(ls_id_), K(archive_speed), K(write_speed_),
    K(total_used_space), K(total_disk_space), K(disk_used_stop_write_threshold_), K(refresh_speed));
  return ret;
}

// calculate the sleep time for the input bytes, and return next available write timestamp.
int ObDDLCtrlSpeedItem::cal_limit(const int64_t bytes, int64_t &next_available_ts)
{
  int ret = OB_SUCCESS;
  next_available_ts = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (bytes < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid input bytes.", K(ret), K(bytes));
  } else if (write_speed_ < MIN_WRITE_SPEED) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected write speed", K(ret), K(write_speed_));
  }
  if (OB_SUCC(ret)) {
    const int64_t need_sleep_us = static_cast<int64_t>(1.0 * bytes / (write_speed_ * 1024 * 1024) * 1000 * 1000);
    int64_t tmp_us = 0;
    do {
      tmp_us = next_available_write_ts_;
      next_available_ts = std::max(ObTimeUtility::current_time(), next_available_write_ts_ + need_sleep_us);
    } while (!ATOMIC_BCAS(&next_available_write_ts_, tmp_us, next_available_ts));
  }
  return ret;
}

int ObDDLCtrlSpeedItem::check_cur_node_is_leader(bool &is_leader)
{
  int ret = OB_SUCCESS;
  is_leader = true;
  ObRole role = INVALID_ROLE;
  ObLS *ls = nullptr;
  ObLSHandle handle;
  ObLSService *ls_svr = share::g_mp->ls_service();
  if (OB_ISNULL(ls_svr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls_svr is nullptr", K(ret));
  } else if (OB_FAIL(ls_svr->get_ls(ls_id_, handle, ObLSGetMod::DDL_MOD))) {
    LOG_WARN("fail to get ls handle", K(ret), K_(ls_id));
  } else if (OB_ISNULL(ls = handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls is nullptr", K(ret));
  } else if (OB_FAIL(ls->get_ls_role(role))) {
    LOG_WARN("get ls role failed", K(ret));
  } else if (role != ObRole::LEADER) {
    is_leader = false;
  }
  return ret;
}

int ObDDLCtrlSpeedItem::do_sleep(
  const int64_t next_available_ts,
  const int64_t task_id,
  ObDDLNeedStopWriteChecker &checker,
  int64_t &real_sleep_us)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  real_sleep_us = 0;  

  bool is_need_stop_write = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (next_available_ts <= 0 || false || task_id == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument.", K(ret), K(next_available_ts), K(task_id));
  } else if (OB_FAIL(DDL_SIM(task_id, DDL_REDO_WRITER_SPEED_CONTROL_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(task_id));
  } else if (OB_TMP_FAIL(check_need_stop_write(checker, is_need_stop_write))) {
    LOG_WARN("fail to check need stop write", K(tmp_ret));
  }
  if (OB_FAIL(ret)) {
  } else if (is_need_stop_write) /*clog disk used exceeds threshold*/ {
    int64_t loop_cnt = 0;
    while (OB_SUCC(ret) && is_need_stop_write) {
      ob_usleep(SLEEP_INTERVAL);
      if (0 == loop_cnt % 100 && dynamic_cast<ObDDLFullNeedStopWriteChecker *>(&checker) != nullptr) {
        uint64_t unused_data_format_version = 0;
        int64_t unused_snapshot_version = 0;
        share::ObDDLTaskStatus task_status = share::ObDDLTaskStatus::PREPARE;
        if (OB_TMP_FAIL(ObDDLUtil::get_data_information(task_id, unused_data_format_version,
            unused_snapshot_version, task_status))) {
          if (OB_ITER_END == tmp_ret) {
            is_need_stop_write = false;
            LOG_INFO("exit due to ddl task exit", K(task_id));
          } else if (loop_cnt >= 100 * 1000) { // wait_time = 100 * 1000 * SLEEP_INTERVAL = 100s.
            is_need_stop_write = false;
            LOG_INFO("exit due to sql exceeds time limit", K(tmp_ret), K(task_id));
          } else {
            if (REACH_COUNT_INTERVAL(1000L)) {
              LOG_WARN("get ddl task info failed", K(tmp_ret), K(task_id));
            }
          }
        } else if (!is_replica_build_ddl_task_status(task_status)) {
          is_need_stop_write = false;
          LOG_INFO("exit due to mismatched status", K(task_id));
        }
      }
      if (REACH_TIME_INTERVAL(10 * 1000 * 1000)) {
        ObTaskController::get().allow_next_syslog();
        FLOG_INFO("stop write ddl clog", K(ret), K(ls_id_),
          K(write_speed_), K(need_stop_write_), K(ref_cnt_),
          K(disk_used_stop_write_threshold_));
      }
      if (is_need_stop_write && OB_TMP_FAIL(check_need_stop_write(checker, is_need_stop_write))) {
        LOG_WARN("fail to check need stop write", K(tmp_ret));
      }
      loop_cnt++;
    }
  }

  if (OB_SUCC(ret)) {
    real_sleep_us = std::max(static_cast<int64_t>(0), next_available_ts - ObTimeUtility::current_time());
    ob_usleep(real_sleep_us);
  }
  return ret;
}

int ObDDLCtrlSpeedItem::check_need_stop_write(ObDDLNeedStopWriteChecker &checker,
                                              bool &is_need_stop_write)
{
  int ret = OB_SUCCESS;
  is_need_stop_write = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    bool is_leader = true;
    if (OB_FAIL(check_cur_node_is_leader(is_leader))) {
      LOG_WARN("check cur node is leader failed", K(ret));
    } else {
      if (is_leader) {
        is_need_stop_write = (checker.check_need_stop_write() || need_stop_write_);
      } else {
        is_need_stop_write = false;
      }
    }
  }
  return ret;
}

// calculate the sleep time for the input bytes, sleep.
int ObDDLCtrlSpeedItem::limit_and_sleep(
  const int64_t bytes,
  const int64_t task_id,
  ObDDLNeedStopWriteChecker &checker,
  int64_t &real_sleep_us)
{
  int ret = OB_SUCCESS;
  real_sleep_us = 0;
  int64_t next_available_ts = 0;
  int64_t transmit_sleep_us = 0; // network related.
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if ((disk_used_stop_write_threshold_ <= 0
      || disk_used_stop_write_threshold_ > 100) || bytes < 0 || false || 0 == task_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument.", K(ret), K(disk_used_stop_write_threshold_), K(bytes), K(task_id));
  } else if (OB_FAIL(cal_limit(bytes, next_available_ts))) {
    LOG_WARN("fail to calculate sleep time", K(ret), K(bytes), K(next_available_ts));
  } else if (OB_ISNULL(GCTX.bandwidth_throttle_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, bandwidth throttle is null", K(ret), KP(GCTX.bandwidth_throttle_));
  } else if (OB_FAIL(GCTX.bandwidth_throttle_->limit_out_and_sleep(bytes,
                                                                   ObTimeUtility::current_time(),
                                                                   INT64_MAX,
                                                                   &transmit_sleep_us))) {
    LOG_WARN("fail to limit out and sleep", K(ret), K(bytes), K(transmit_sleep_us));
  } else if (OB_FAIL(do_sleep(next_available_ts, task_id, checker, real_sleep_us))) {
    LOG_WARN("fail to sleep", K(ret), K(next_available_ts), K(real_sleep_us));
  } else {/* do nothing. */}
  return ret;
}

ObDDLCtrlSpeedHandle::ObDDLCtrlSpeedHandle()
  : is_inited_(false), refreshTimerTask_()
{
}

ObDDLCtrlSpeedHandle::~ObDDLCtrlSpeedHandle()
{
}

ObDDLCtrlSpeedHandle &ObDDLCtrlSpeedHandle::get_instance()
{
  static ObDDLCtrlSpeedHandle instance;
  return instance;
}

int ObDDLCtrlSpeedHandle::init(common::ObTimer &timer)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("inited twice", K(ret));
  } else if (OB_FAIL(refreshTimerTask_.init(timer))) {
    LOG_WARN("fail to init refreshTimerTask", K(ret));
  } else {
    is_inited_ = true;
    LOG_INFO("succeed to init ObDDLCtrlSpeedHandle", K(ret));
  }
  return ret;
}

int ObDDLCtrlSpeedHandle::limit_and_sleep(const share::ObLSID &ls_id,
                                          const int64_t bytes,
                                          const int64_t task_id,
                                          ObDDLNeedStopWriteChecker &checker,
                                          int64_t &real_sleep_us)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if(OB_UNLIKELY(false || !ls_id.is_valid() || bytes < 0 || 0 == task_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(task_id), K(ls_id), K(bytes));
  } else if (OB_FAIL(DDL_SIM(task_id, WRITE_DUPLICATED_DDL_REDO_LOG))) {
    LOG_WARN("ddl sim remote write", K(ret), K(task_id));
  } else if (false) {
  }
  if (OB_SUCC(ret) && OB_FAIL(speed_handle_item_.init(ls_id))) {
    if (OB_INIT_TWICE != ret) {
      LOG_WARN("fail to init speed handle item", K(ret), K(ls_id));
    } else {
      ret = OB_SUCCESS; // already inited, treat as success
    }
  }
  if (OB_SUCC(ret)) {
    ret = speed_handle_item_.limit_and_sleep(bytes, task_id, checker, real_sleep_us);
    if (OB_FAIL(ret)) {
      LOG_WARN("fail to limit and sleep", K(ret), K(bytes), K(task_id), K(real_sleep_us));
    }
  }
  return ret;
}

int ObDDLCtrlSpeedHandle::refresh()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(false)) {
    // not initialized yet, skip refresh
  } else {
    MOD_SCOPE {
      if (OB_FAIL(speed_handle_item_.refresh())) {
        LOG_WARN("refresh speed and disk config failed", K(ret));
      }
    } else if (OB_TENANT_NOT_IN_SERVER == ret || OB_IN_STOP_STATE == ret) {
      speed_handle_item_.reset_need_stop_write();
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("switch tenant id failed", K(ret));
    }
  }
  return ret;
}

// RefreshSpeedHandle Timer Task
ObDDLCtrlSpeedHandle::RefreshSpeedHandleTask::RefreshSpeedHandleTask()
  : is_inited_(false) {}

ObDDLCtrlSpeedHandle::RefreshSpeedHandleTask::~RefreshSpeedHandleTask()
{
  is_inited_ = false;
}

int ObDDLCtrlSpeedHandle::RefreshSpeedHandleTask::init(common::ObTimer &timer)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    is_inited_ = true;
    if (OB_FAIL(timer.schedule(*this, REFRESH_INTERVAL, true /* schedule repeatedly */))) {
      LOG_WARN("fail to schedule RefreshSpeedHandle Timer Task", K(ret));
    }
  }
  return ret;
}

void ObDDLCtrlSpeedHandle::RefreshSpeedHandleTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("RefreshSpeedHandleTask not init", K(ret));
  } else if (OB_FAIL(ObDDLCtrlSpeedHandle::get_instance().refresh())) {
    LOG_WARN("fail to refresh SpeedHandleMap", K(ret));
  }
}

ObDDLRedoLock::ObDDLRedoLock() : is_inited_(false), bucket_lock_()
{
}

ObDDLRedoLock::~ObDDLRedoLock()
{
}

ObDDLRedoLock &ObDDLRedoLock::get_instance()
{
  static ObDDLRedoLock instance;
  return instance;
}

int ObDDLRedoLock::init()
{
  int ret = OB_SUCCESS;
  const int64_t bucket_num = 10243L;
  if (is_inited_) {
  } else if (OB_FAIL(bucket_lock_.init(bucket_num))) {
    LOG_WARN("init bucket lock failed", K(ret), K(bucket_num));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObDDLRedoLogWriter::local_write_ddl_macro_redo(
    const ObDDLMacroBlockRedoInfo &redo_info,
    const share::ObLSID &ls_id,
    const int64_t task_id,
    logservice::ObLogHandler *log_handler,
    const blocksstable::MacroBlockId &macro_block_id,
    char *buffer,
    ObDDLRedoLogHandle &handle)
{
  int ret = OB_SUCCESS;
  
  ObDDLRedoLog log;
  const enum ObReplayBarrierType replay_barrier_type = ObReplayBarrierType::NO_NEED_BARRIER;
  logservice::ObLogBaseHeader base_header(logservice::ObLogBaseType::DDL_LOG_BASE_TYPE,
                                          replay_barrier_type);
  ObDDLClogHeader ddl_header(ObDDLClogType::DDL_REDO_LOG);
  int64_t buffer_size = 0;
  int64_t pos = 0;
  ObDDLMacroBlockClogCb *cb = nullptr;
  ObDDLRedoLog tmp_log;
  int64_t log_start_pos = 0;

  palf::LSN lsn;
  const bool need_nonblock= false;
  const bool allow_compression = false;
  SCN base_scn = SCN::min_scn();
  SCN scn;
  int64_t real_sleep_us = 0;
  int tmp_ret = OB_SUCCESS;

  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  ddl_kv_mgr_handle.reset();
  if (OB_UNLIKELY(!redo_info.is_valid()
                  || nullptr == log_handler
                  || false
                  || nullptr == buffer
                  || 0 == task_id
                  || !ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(redo_info), KP(log_handler), KP(buffer), K(task_id), K(ls_id));
  } else if (OB_FAIL(log.init(redo_info))) {
    LOG_WARN("fail to init DDLRedoLog", K(ret), K(redo_info));
  } else if (FALSE_IT(buffer_size = base_header.get_serialize_size()
                                    + ddl_header.get_serialize_size()
                                    + log.get_serialize_size())) {
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls_id, ls_handle, ObLSGetMod::DDL_MOD))) {
    LOG_WARN("get ls failed", K(ret), K(ls_id));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls should not be null", K(ret));
  } else if (OB_FAIL(ls->get_tablet(log.get_redo_info().table_key_.tablet_id_, tablet_handle, ObTabletCommon::DEFAULT_GET_TABLET_NO_WAIT, ObMDSGetTabletMode::READ_ALL_COMMITED))) {
    LOG_WARN("get tablet handle failed", K(ret), K(ls_id), K(log.get_redo_info()));
  } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle, 
                                                             ObDDLUtil::use_idempotent_mode()))) {
    LOG_WARN("create ddl kv mgr failed", K(ret));
  } else {
    ObDDLFullNeedStopWriteChecker checker(ddl_kv_mgr_handle);
    if (OB_TMP_FAIL(ObDDLCtrlSpeedHandle::get_instance().limit_and_sleep(ls_id, buffer_size, task_id, checker, real_sleep_us))) {
      LOG_WARN("fail to limit and sleep", K(tmp_ret), K(task_id), K(ls_id), K(buffer_size), K(real_sleep_us));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(cb = op_alloc(ObDDLMacroBlockClogCb))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(base_header.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("failed to serialize log base header", K(ret));
  } else if (OB_FAIL(ddl_header.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("fail to seriaize ddl redo log", K(ret));
  } else if (FALSE_IT(log_start_pos = pos)) {
  } else if (OB_FAIL(log.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("fail to seriaize ddl redo log", K(ret));
  } else if (OB_FAIL(tmp_log.deserialize(buffer, buffer_size, log_start_pos))) {
    LOG_WARN("fail to deserialize ddl redo log", K(ret));
  /* use the ObString data_buffer_ in tmp_log.redo_info_, do not rely on the macro_block_buf in original log*/
  } else if (OB_FAIL(cb->init(ls_id, tmp_log.get_redo_info(), macro_block_id, tablet_handle, tmp_log.get_redo_info().type_))) {
    LOG_WARN("init ddl clog callback failed", K(ret), K(redo_info), K(tmp_log.get_redo_info()), K(macro_block_id));
  } else if (OB_FAIL(DDL_SIM(task_id, DDL_REDO_WRITER_WRITE_MACRO_LOG_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(task_id));
  } else if (OB_FAIL(log_handler->append(buffer,
                                         buffer_size,
                                         base_scn,
                                         need_nonblock,
                                         allow_compression,
                                         cb,
                                         lsn,
                                         scn))) {
    LOG_WARN("fail to submit ddl redo log", K(ret), K(buffer), K(buffer_size));
  } else {
    handle.cb_ = cb;
    cb = nullptr;
    handle.scn_ = scn;
  }
  if (OB_FAIL(ret)) {
    if (nullptr != cb) {
      op_free(cb);
      cb = nullptr;
    }
  }
  return ret;
}

int ObDDLRedoLogWriter::local_write_ddl_start_log(
    const ObDDLStartLog &log,
    ObLSHandle &ls_handle,
    ObLogHandler *log_handler,
    ObDDLKvMgrHandle &ddl_kv_mgr_handle,
    ObDDLKvMgrHandle &lob_kv_mgr_handle,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle,
    uint32_t &lock_tid,
    SCN &start_scn)
{
  int ret = OB_SUCCESS;
  start_scn.set_min();
  const enum ObReplayBarrierType replay_barrier_type = ObReplayBarrierType::STRICT_BARRIER;
  logservice::ObLogBaseHeader base_header(logservice::ObLogBaseType::DDL_LOG_BASE_TYPE,
                                          replay_barrier_type);
  ObDDLClogHeader ddl_header(ObDDLClogType::DDL_START_LOG);
  const int64_t buffer_size = base_header.get_serialize_size()
                              + ddl_header.get_serialize_size()
                              + log.get_serialize_size();
  char buffer[buffer_size];
  int64_t pos = 0;
  ObDDLStartClogCb *cb = nullptr;

  palf::LSN lsn;
  const bool need_nonblock= false;
  const bool allow_compression = false;
  SCN scn = SCN::min_scn();
  bool is_external_consistent = false;
  ObDDLRedoLockGuard guard(log.get_table_key().get_tablet_id().hash());
  if (OB_ISNULL(cb = op_alloc(ObDDLStartClogCb))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(cb->init(log.get_table_key(), log.get_data_format_version(), log.get_execution_id(),
    ddl_kv_mgr_handle, lob_kv_mgr_handle, direct_load_mgr_handle, lock_tid))) {
    LOG_WARN("failed to init cb", K(ret));
  } else if (OB_FAIL(base_header.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("failed to serialize log base header", K(ret));
  } else if (OB_FAIL(ddl_header.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("fail to seriaize ddl start log", K(ret));
  } else if (OB_FAIL(log.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("fail to seriaize ddl start log", K(ret));
  } else if (OB_FAIL(ls_handle.get_ls()->get_ddl_log_handler()->add_tablet(log.get_table_key().get_tablet_id()))) {
    LOG_WARN("add tablet failed", K(ret), "tablet_id", log.get_table_key().get_tablet_id());
  } else if (lob_kv_mgr_handle.is_valid() && OB_FAIL(ls_handle.get_ls()->get_ddl_log_handler()->add_tablet(lob_kv_mgr_handle.get_obj()->get_tablet_id()))) {
    LOG_WARN("add lob tablet failed", K(ret), "lob_tablet_id", lob_kv_mgr_handle.get_obj()->get_tablet_id());
  } else if (OB_FAIL(log_handler->append(buffer,
                                         buffer_size,
                                         SCN::min_scn(),
                                         need_nonblock,
                                         allow_compression,
                                         cb,
                                         lsn,
                                         scn))) {
    LOG_ERROR("fail to submit ddl start log", K(ret), K(buffer_size));
    if (ObDDLUtil::need_remote_write(ret)) {
      ret = OB_NOT_MASTER;
      LOG_INFO("overwrite return to OB_NOT_MASTER");
    }
  } else {
    ObDDLStartClogCb *tmp_cb = cb;
    cb = nullptr;
    lock_tid = 0;
    bool finish = false;
    const int64_t start_time = ObTimeUtility::current_time();
    start_scn = scn;
    while (OB_SUCC(ret) && !finish) {
      if (OB_FAIL(THIS_WORKER.check_status())) {
        LOG_WARN("check status failed", K(ret));
      } else if (tmp_cb->is_success()) {
        finish = true;
      } else if (tmp_cb->is_failed()) {
        ret = OB_NOT_MASTER;
      }
      if (OB_SUCC(ret) && !finish) {
        const int64_t current_time = ObTimeUtility::current_time();
        if (current_time - start_time > ObDDLRedoLogHandle::DDL_REDO_LOG_TIMEOUT) {
          ret = OB_TIMEOUT;
          LOG_WARN("write ddl start log timeout", K(ret), K(current_time), K(start_time));
        } else {
          if (REACH_TIME_INTERVAL(10L * 1000L * 1000L)) { //10s
            LOG_INFO("wait ddl start log callback", K(ret), K(finish), K(current_time), K(start_time));
          }
          ob_usleep(ObDDLRedoLogHandle::CHECK_DDL_REDO_LOG_FINISH_INTERVAL);
        }
      }
    }
    tmp_cb->try_release(); // release the memory no matter succ or not
  }
  if (OB_FAIL(ret)) {
    if (nullptr != cb) {
      op_free(cb);
      cb = nullptr;
    }
  }
  return ret;
}

int ObDDLRedoLogWriter::local_write_ddl_commit_log(
    const ObDDLCommitLog &log,
    const ObDDLClogType clog_type,
    const share::ObLSID &ls_id,
    ObLogHandler *log_handler,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle,
    ObTabletDirectLoadMgrHandle &lob_direct_load_mgr_handle,
    ObDDLCommitLogHandle &handle,
    uint32_t &lock_tid)
{
  int ret = OB_SUCCESS;
  const enum ObReplayBarrierType replay_barrier_type = ObReplayBarrierType::PRE_BARRIER;
  DEBUG_SYNC(BEFORE_WRITE_DDL_PREPARE_LOG);
  logservice::ObLogBaseHeader base_header(logservice::ObLogBaseType::DDL_LOG_BASE_TYPE,
                                          replay_barrier_type);
  ObDDLClogHeader ddl_header(clog_type);
  char *buffer = nullptr;
  const int64_t buffer_size = base_header.get_serialize_size()
                              + ddl_header.get_serialize_size()
                              + log.get_serialize_size();
  int64_t pos = 0;
  ObDDLCommitClogCb *cb = nullptr;

  palf::LSN lsn;
  const bool need_nonblock= false;
  const bool allow_compression = false;
  SCN base_scn = SCN::min_scn();
  SCN scn = SCN::min_scn();
  bool is_external_consistent = false;
if (OB_ISNULL(buffer = static_cast<char *>(ob_malloc(buffer_size, ObMemAttr("DDL_COMMIT_LOG"))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_ISNULL(cb = op_alloc(ObDDLCommitClogCb))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(cb->init(ls_id, log.get_table_key().tablet_id_, log.get_start_scn(), lock_tid, direct_load_mgr_handle, lob_direct_load_mgr_handle))) {
    LOG_WARN("init ddl commit log callback failed", K(ret), K(ls_id), K(log));
  } else if (OB_FAIL(base_header.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("failed to serialize log base header", K(ret));
  } else if (OB_FAIL(ddl_header.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("fail to seriaize ddl commit log", K(ret));
  } else if (OB_FAIL(log.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("fail to seriaize ddl commit log", K(ret));
  } else if (OB_FAIL(OB_TS_MGR.get_ts_sync(ObDDLRedoLogHandle::DDL_REDO_LOG_TIMEOUT, base_scn, is_external_consistent))) {
    LOG_WARN("fail to get gts sync", K(ret), K(log));
  } else if (OB_FAIL(log_handler->append(buffer,
                                         buffer_size,
                                         base_scn,
                                         need_nonblock,
                                         allow_compression,
                                         cb,
                                         lsn,
                                         scn))) {
    LOG_WARN("fail to submit ddl commit log", K(ret), K(buffer), K(buffer_size));
  } else {
    ObDDLCommitClogCb *tmp_cb = cb;
    cb = nullptr;
    lock_tid = 0;
    bool need_retry = true;
    while (need_retry) {
      if (OB_FAIL(OB_TS_MGR.wait_gts_elapse(scn))) {
        if (OB_EAGAIN != ret) {
          LOG_WARN("fail to wait gts elapse", K(ret), K(log));
        } else {
          ob_usleep(1000);
        }
      } else {
        need_retry = false;
      }
    }
    if (OB_SUCC(ret)) {
      handle.cb_ = tmp_cb;
      handle.commit_scn_ = scn;
    } else {
      tmp_cb->try_release(); // release the memory
    }
  }
  if (nullptr != buffer) {
    ob_free(buffer);
    buffer = nullptr;
  }
  if (OB_FAIL(ret)) {
    if (nullptr != cb) {
      op_free(cb);
      cb = nullptr;
    }
  }
  return ret;
}

template <typename T>
int ObDDLRedoLogWriter::write_auto_split_log(
    const share::ObLSID &ls_id,
    const ObDDLClogType &clog_type,
    const ObReplayBarrierType &replay_barrier_type,
    const T &log,
    SCN &scn)
{
  int ret = OB_SUCCESS;
  scn = SCN::min_scn();
  ObArenaAllocator tmp_arena("SplitLogBuf", OB_MALLOC_NORMAL_BLOCK_SIZE);
  logservice::ObLogBaseHeader base_header(logservice::ObLogBaseType::DDL_LOG_BASE_TYPE,
                                          replay_barrier_type);
  ObDDLClogHeader ddl_header(clog_type);
  const int64_t buffer_size = base_header.get_serialize_size()
                              + ddl_header.get_serialize_size()
                              + log.get_serialize_size();
  char *buffer = nullptr; // stack space avoided, to avoid too muck stack size.
  int64_t pos = 0;
  ObDDLClogCb *cb = nullptr;

  palf::LSN lsn;
  const bool need_nonblock= false;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  logservice::ObLogHandler *log_handler = nullptr;
  if (OB_UNLIKELY(!ls_id.is_valid()) ||
      OB_UNLIKELY(ObDDLClogType::DDL_TABLET_SPLIT_START_LOG != clog_type &&
                  ObDDLClogType::DDL_TABLET_SPLIT_FINISH_LOG != clog_type &&
                  ObDDLClogType::DDL_TABLET_FREEZE_LOG != clog_type) ||
      OB_UNLIKELY(!log.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(ls_id), K(clog_type), K(log));
  } else if (OB_ISNULL(buffer = static_cast<char *>(tmp_arena.alloc(buffer_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc failed", K(ret), K(buffer_size));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls_id, ls_handle, ObLSGetMod::DDL_MOD))) {
    LOG_WARN("get ls failed", K(ret), K(log));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls should not be null", K(ret));
  } else if (OB_ISNULL(log_handler = ls->get_log_handler())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get log handler failed", K(ret), K(log));
  } else if (OB_ISNULL(cb = op_alloc(ObDDLClogCb))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(base_header.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("failed to serialize log base header", K(ret));
  } else if (OB_FAIL(ddl_header.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("fail to seriaize ddl start log", K(ret));
  } else if (OB_FAIL(log.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("fail to seriaize ddl start log", K(ret));
  } else if (OB_FAIL(log_handler->append(buffer,
                                         buffer_size,
                                         SCN::min_scn(),
                                         need_nonblock,
                                         false/*allow_compression*/,
                                         cb,
                                         lsn,
                                         scn))) {
    LOG_ERROR("fail to submit ddl start log", K(ret), K(buffer_size));
    if (ObDDLUtil::need_remote_write(ret)) {
      ret = OB_NOT_MASTER;
      LOG_INFO("overwrite return to OB_NOT_MASTER");
    }
  } else {
    ObDDLClogCb *tmp_cb = cb;
    cb = nullptr;
    bool finish = false;
    const int64_t start_time = ObTimeUtility::current_time();
    while (OB_SUCC(ret) && !finish) {
      if (tmp_cb->is_success()) {
        finish = true;
      } else if (tmp_cb->is_failed()) {
        ret = OB_NOT_MASTER;
      }
      if (OB_SUCC(ret) && !finish) {
        const int64_t current_time = ObTimeUtility::current_time();
        if (current_time - start_time > ObDDLRedoLogHandle::DDL_REDO_LOG_TIMEOUT) {
          ret = OB_TIMEOUT;
          LOG_WARN("write auto split log timeout", K(ret), K(log));
        } else {
          ob_usleep(ObDDLRedoLogHandle::CHECK_DDL_REDO_LOG_FINISH_INTERVAL);
        }
      }
    }
    tmp_cb->try_release(); // release the memory no matter succ or not
  }
  if (OB_FAIL(ret)) {
    if (nullptr != cb) {
      op_free(cb);
      cb = nullptr;
    }
  }
  tmp_arena.reset();
  buffer = nullptr;
  SERVER_EVENT_ADD("ddl", "write_split_log",
      "ret", ret,
      "src_tablet_id", log.get_source_tablet_id().id(),
      "clog_type", clog_type,
      "scn", scn,
      "trace_id", *ObCurTraceId::get_trace_id());
  LOG_INFO("write split log finished", K(ret), K(ls_id), K(clog_type), K(scn));
  return ret;
}

template int ObDDLRedoLogWriter::write_auto_split_log(const share::ObLSID &ls_id,
                                  const ObDDLClogType &clog_type,
                                  const ObReplayBarrierType &replay_barrier_type,
                                  const ObTabletSplitStartLog &log,
                                  SCN &scn);
template int ObDDLRedoLogWriter::write_auto_split_log(const share::ObLSID &ls_id,
                                  const ObDDLClogType &clog_type,
                                  const ObReplayBarrierType &replay_barrier_type,
                                  const ObTabletSplitFinishLog &log,
                                  SCN &scn);
template int ObDDLRedoLogWriter::write_auto_split_log(const share::ObLSID &ls_id,
                                  const ObDDLClogType &clog_type,
                                  const ObReplayBarrierType &replay_barrier_type,
                                  const ObTabletFreezeLog &log,
                                  SCN &scn);

template <typename T>
int ObDDLRedoLogWriter::write_auto_fork_log(
    const share::ObLSID &ls_id,
    const ObDDLClogType &clog_type,
    const logservice::ObReplayBarrierType &replay_barrier_type,
    const T &log,
    SCN &scn)
{
  int ret = OB_SUCCESS;
  scn = SCN::min_scn();
  ObArenaAllocator tmp_arena("ForkLogBuf", OB_MALLOC_NORMAL_BLOCK_SIZE);
  logservice::ObLogBaseHeader base_header(logservice::ObLogBaseType::DDL_LOG_BASE_TYPE,
                                          replay_barrier_type);
  ObDDLClogHeader ddl_header(clog_type);
  const int64_t buffer_size = base_header.get_serialize_size()
                              + ddl_header.get_serialize_size()
                              + log.get_serialize_size();
  char *buffer = nullptr; // stack space avoided, to avoid too muck stack size.
  int64_t pos = 0;
  ObDDLClogCb *cb = nullptr;

  palf::LSN lsn;
  const bool need_nonblock = false;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  logservice::ObLogHandler *log_handler = nullptr;
  if (OB_UNLIKELY(!ls_id.is_valid()) ||
              OB_UNLIKELY(ObDDLClogType::DDL_TABLE_FORK_FREEZE_LOG != clog_type &&
                          ObDDLClogType::DDL_TABLE_FORK_START_LOG != clog_type &&
                          ObDDLClogType::DDL_TABLE_FORK_FINISH_LOG != clog_type) ||
      OB_UNLIKELY(!log.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(ls_id), K(clog_type), K(log));
  } else if (OB_ISNULL(buffer = static_cast<char *>(tmp_arena.alloc(buffer_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc failed", K(ret), K(buffer_size));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls_id, ls_handle, ObLSGetMod::DDL_MOD))) {
    LOG_WARN("get ls failed", K(ret), K(log));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls should not be null", K(ret));
  } else if (OB_ISNULL(log_handler = ls->get_log_handler())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get log handler failed", K(ret), K(log));
  } else if (OB_ISNULL(cb = op_alloc(ObDDLClogCb))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(base_header.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("failed to serialize log base header", K(ret));
  } else if (OB_FAIL(ddl_header.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("fail to serialize ddl header", K(ret));
  } else if (OB_FAIL(log.serialize(buffer, buffer_size, pos))) {
    LOG_WARN("fail to serialize fork log", K(ret));
  } else if (OB_FAIL(log_handler->append(buffer,
                                         buffer_size,
                                         SCN::min_scn(),
                                         need_nonblock,
                                         false/*allow_compression*/,
                                         cb,
                                         lsn,
                                         scn))) {
    LOG_ERROR("fail to submit ddl fork log", K(ret), K(buffer_size));
    if (ObDDLUtil::need_remote_write(ret)) {
      ret = OB_NOT_MASTER;
      LOG_INFO("overwrite return to OB_NOT_MASTER");
    }
  } else {
    ObDDLClogCb *tmp_cb = cb;
    cb = nullptr;
    bool finish = false;
    const int64_t start_time = ObTimeUtility::current_time();
    while (OB_SUCC(ret) && !finish) {
      if (tmp_cb->is_success()) {
        finish = true;
      } else if (tmp_cb->is_failed()) {
        ret = OB_NOT_MASTER;
      }
      if (OB_SUCC(ret) && !finish) {
        const int64_t current_time = ObTimeUtility::current_time();
        if (current_time - start_time > ObDDLRedoLogHandle::DDL_REDO_LOG_TIMEOUT) {
          ret = OB_TIMEOUT;
          LOG_WARN("write auto fork log timeout", K(ret), K(log));
        } else {
          ob_usleep(ObDDLRedoLogHandle::CHECK_DDL_REDO_LOG_FINISH_INTERVAL);
        }
      }
    }
    tmp_cb->try_release(); // release the memory no matter succ or not
  }
  if (OB_FAIL(ret)) {
    if (nullptr != cb) {
      op_free(cb);
      cb = nullptr;
    }
  }
  tmp_arena.reset();
  buffer = nullptr;
  const auto &source_tablet_ids = log.get_source_tablet_ids();
  SERVER_EVENT_ADD("ddl", "write_fork_log",
      "ret", ret,
      "clog_type", clog_type,
      "replay_barrier", replay_barrier_type,
      "scn", scn,
      "trace_id", *ObCurTraceId::get_trace_id());
  LOG_INFO("write fork log finished", K(ret), K(ls_id), K(source_tablet_ids), K(clog_type), K(replay_barrier_type), K(scn));
  return ret;
}

template int ObDDLRedoLogWriter::write_auto_fork_log(const share::ObLSID &ls_id,
                                              const ObDDLClogType &clog_type,
                                              const ObReplayBarrierType &replay_barrier_type,
                                              const ObTableForkFreezeLog &log,
                                              SCN &scn);
template int ObDDLRedoLogWriter::write_auto_fork_log(const share::ObLSID &ls_id,
                                              const ObDDLClogType &clog_type,
                                              const ObReplayBarrierType &replay_barrier_type,
                                              const ObTableForkStartLog &log,
                                              SCN &scn);
template int ObDDLRedoLogWriter::write_auto_fork_log(const share::ObLSID &ls_id,
                                              const ObDDLClogType &clog_type,
                                              const ObReplayBarrierType &replay_barrier_type,
                                              const ObTableForkFinishLog &log,
                                              SCN &scn);

bool ObDDLRedoLogWriter::need_retry(int ret_code)
{
  return OB_NOT_MASTER == ret_code;
}

ObDDLRedoLogHandle::ObDDLRedoLogHandle()
  : cb_(nullptr), scn_(SCN::min_scn())
{
}

ObDDLRedoLogHandle::~ObDDLRedoLogHandle()
{
  reset();
}

void ObDDLRedoLogHandle::reset()
{
  if (nullptr != cb_) {
    cb_->try_release();
    cb_ = nullptr;
  }
}

int ObDDLRedoLogHandle::wait(const int64_t timeout)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cb_)) {
  } else {
    bool finish = false;
    const int64_t start_time = ObTimeUtility::current_time();
    while (OB_SUCC(ret) && !finish) {
      if (OB_FAIL(THIS_WORKER.check_status())) {
        LOG_WARN("check status failed", K(ret));
      } else if (cb_->is_success()) {
        finish = true;
      } else if (cb_->is_failed()) {
        ret = OB_NOT_MASTER;
      }
      if (OB_SUCC(ret) && !finish) {
        const int64_t current_time = ObTimeUtility::current_time();
        if (current_time - start_time > timeout) {
          ret = OB_TIMEOUT;
          LOG_WARN("write ddl redo log timeout", K(ret), K(current_time), K(start_time));
        } else {
          if (REACH_TIME_INTERVAL(10L * 1000L * 1000L)) { //10s
            LOG_INFO("wait ddl redo log callback", K(ret), K(finish), K(current_time), K(start_time));
          }
          ob_usleep(ObDDLRedoLogHandle::CHECK_DDL_REDO_LOG_FINISH_INTERVAL);
        }
      }
    }
  }
  return ret;
}

ObDDLCommitLogHandle::ObDDLCommitLogHandle()
  : cb_(nullptr), commit_scn_(SCN::min_scn())
{
}

ObDDLCommitLogHandle::~ObDDLCommitLogHandle()
{
  reset();
}

int ObDDLCommitLogHandle::wait(const int64_t timeout)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cb_)) {
  } else {
    bool finish = false;
    const int64_t start_time = ObTimeUtility::current_time();
    while (OB_SUCC(ret) && !finish) {
      if (OB_FAIL(THIS_WORKER.check_status())) {
        LOG_WARN("check status failed", K(ret));
      } else if (cb_->is_success()) {
        finish = true;
        ret = cb_->get_ret_code();
        if (OB_FAIL(ret)) {
          LOG_WARN("ddl commit log callback execute failed", K(ret), KPC(cb_));
        }
      } else if (cb_->is_failed()) {
        ret = OB_NOT_MASTER;
      }
      if (OB_SUCC(ret) && !finish) {
        const int64_t current_time = ObTimeUtility::current_time();
        if (current_time - start_time > timeout) {
          ret = OB_TIMEOUT;
          LOG_WARN("write ddl commit log timeout", K(ret), K(current_time), K(start_time));
        } else {
          if (REACH_TIME_INTERVAL(10L * 1000L * 1000L)) { //10s
            LOG_INFO("wait ddl commit log callback", K(ret), K(finish), K(current_time), K(start_time));
          }
          ob_usleep(ObDDLRedoLogHandle::CHECK_DDL_REDO_LOG_FINISH_INTERVAL);
        }
      }
    }
  }
  return ret;
}

void ObDDLCommitLogHandle::reset()
{
  int tmp_ret = OB_SUCCESS;
  if (nullptr != cb_) {
    cb_->try_release();
    cb_ = nullptr;
  }
}

int ObDDLRedoLogWriter::remote_write_ddl_macro_redo(
    const int64_t task_id,
    const ObDDLMacroBlockRedoInfo &redo_info)
{
  int ret = OB_SUCCESS;
  const int64_t wait_timeout_us = MAX(ObDDLRedoLogHandle::DDL_REDO_LOG_TIMEOUT, GCONF.rpc_timeout);
  if (OB_UNLIKELY(!redo_info.is_valid() || 0 == task_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(redo_info));
  } else {
    obcall::ObCallRemoteWriteDDLRedoLogArg arg;
    if (OB_FAIL(arg.init(leader_ls_id_, redo_info, task_id))) {
      LOG_WARN("fail to init arg", K(ret));
    } else if (OB_FAIL(ex_rpc::sync_call([&]() -> int {
  int ret = OB_SUCCESS;
  
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(arg));
  } else {
    MOD_SCOPE {
      ObRole role = INVALID_ROLE;
      ObDDLRedoLogWriter sstable_redo_writer;
      MacroBlockId macro_block_id;
      ObLSService *ls_service = share::g_mp->ls_service();
      blocksstable::ObMacroBlockHandle macro_handle;
      ObLSHandle ls_handle;
      ObLS *ls = nullptr;
      if (OB_FAIL(ls_service->get_ls(arg.ls_id_, ls_handle, ObLSGetMod::OBSERVER_MOD))) {
        LOG_WARN("get ls failed", K(ret), K(arg));
      } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected error", K(ret), K(arg.ls_id_));
      } else if (OB_FAIL(ls->get_ls_role(role))) {
        LOG_WARN("get role failed", K(ret), K(arg.ls_id_));
      } else if (ObRole::LEADER != role) {
        ret = OB_NOT_MASTER;
        LOG_INFO("not leader", K(ret), K(arg.ls_id_));
      } else if (OB_FAIL(ObDDLRedoLogWriter::write_block_to_disk(arg.redo_info_, arg.ls_id_, macro_handle, macro_block_id))) {
        LOG_WARN("failed to write block to disk", K(ret));
      } else if (OB_FAIL(sstable_redo_writer.init(arg.ls_id_, arg.redo_info_.table_key_.tablet_id_))) {
        LOG_WARN("init sstable redo writer", K(ret), K(arg));
      } else if (OB_FAIL(sstable_redo_writer.write_macro_block_log(arg.redo_info_, macro_block_id, false, arg.task_id_))) {
        LOG_WARN("fail to write macro redo", K(ret), K(arg), K(macro_block_id));
      } else if (OB_FAIL(sstable_redo_writer.wait_macro_block_log_finish(arg.redo_info_, macro_block_id))) {
        LOG_WARN("fail to wait macro redo finish", K(ret), K(arg));
      }
    }
  }
  return ret;}))) {
      LOG_WARN("fail to write ddl redo log", K(ret), K_(leader_addr), K(arg));
    }
  }
  return ret;
}

ObDDLRedoLogWriter::ObDDLRedoLogWriter()
  : is_inited_(false), remote_write_(false),
    ls_id_(), tablet_id_(), ddl_redo_handle_array_(), leader_addr_(), leader_ls_id_(), buffer_(nullptr), allocator_(ObMemAttr("DldTabletMeta")), shared_tablet_()
{
  ddl_redo_handle_array_.set_attr(lib::ObMemAttr("DdlWriteHdl"));
} 

int ObDDLRedoLogWriter::init(const ObLSID &ls_id, const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ddl redo log writer has been inited twice", K(ret));
  } else if (OB_UNLIKELY(!ls_id.is_valid() || !tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(ls_id), K(tablet_id));
  } else {
    ls_id_ = ls_id;
    tablet_id_ = tablet_id;
    is_inited_ = true;
  }
  return ret;
}

void ObDDLRedoLogWriter::reset()
{
  is_inited_ = false;
  remote_write_ = false;
  ls_id_.reset();
  tablet_id_.reset();
  ddl_redo_handle_array_.reuse();
  leader_addr_.reset();
  leader_ls_id_.reset();
}

int ObDDLRedoLogWriter::write_start_log(
    const ObITable::TableKey &table_key,
    const int64_t execution_id,
    const uint64_t data_format_version,
    const ObDirectLoadType direct_load_type,
    ObDDLKvMgrHandle &ddl_kv_mgr_handle,
    ObDDLKvMgrHandle &lob_kv_mgr_handle,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle,
    uint32_t &lock_tid,
    SCN &start_scn)
{
  int ret = OB_SUCCESS;
  ObDDLStartLog log;
  ObLS *ls = nullptr;
  ObLSHandle ls_handle;
  ObTabletHandle tablet_handle;
  start_scn.set_min();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ddl redo log writer has not been inited", K(ret));
  } else if (OB_UNLIKELY(!table_key.is_valid() || execution_id < 0 || data_format_version <= 0 || !is_valid_direct_load(direct_load_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(table_key), K(execution_id), K(data_format_version), K(direct_load_type));
  } else if (OB_FAIL(log.init(table_key, data_format_version, execution_id, direct_load_type,
          lob_kv_mgr_handle.is_valid() ? lob_kv_mgr_handle.get_obj()->get_tablet_id() : ObTabletID()))) {
    LOG_WARN("fail to init DDLStartLog", K(ret), K(table_key), K(execution_id), K(data_format_version));
  }  else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls_id_, ls_handle, ObLSGetMod::DDL_MOD))) {
    LOG_WARN("get ls failed", K(ret), K(ls_id_));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls should not be null", K(ret), K(table_key));
  /*} else if (OB_FAIL(DDL_SIM(ddl_task_id, DDL_REDO_WRITER_WRITE_START_LOG_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(ddl_task_id));*/
  } else if (OB_FAIL(local_write_ddl_start_log(log, ls_handle, ls->get_log_handler(),
      ddl_kv_mgr_handle, lob_kv_mgr_handle, direct_load_mgr_handle, lock_tid, start_scn))) {
    LOG_WARN("fail to write ddl start log", K(ret), K(table_key));
  } else {
  /*SERVER_EVENT_ADD("ddl", "ddl write start log",
    "ret", ret,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "task_id", ddl_task_id,
    "tablet_id", tablet_id_,
    "start_scn", start_scn);
    LOG_INFO("write ddl start log", K(ret), K(table_key), K(start_scn));*/
  }
  return ret;
}

int ObDDLRedoLogWriter::write_macro_block_log(
    const ObDDLMacroBlockRedoInfo &redo_info,
    const blocksstable::MacroBlockId &macro_block_id,
    const bool allow_remote_write,
    const int64_t task_id)
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  const int64_t BUF_SIZE = 2 * 1024 * 1024 + 16 * 1024;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ddl redo log writer has not been inited", K(ret));
  } else if (OB_UNLIKELY(!redo_info.is_valid() || 0 == task_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(redo_info), K(task_id));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls_id_, ls_handle, ObLSGetMod::DDL_MOD))) {
    LOG_WARN("get ls failed", K(ret), K(ls_id_));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls should not be null", K(ret));
  } else if (nullptr == buffer_ && OB_ISNULL(buffer_ = static_cast<char *>(ob_malloc(BUF_SIZE, ObMemAttr("DDL_REDO_LOG"))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret), K(BUF_SIZE));
  } else if (!remote_write_) {
    if (OB_FAIL(ddl_redo_handle_array_.push_back(ObDDLRedoLogHandle()))) {
      LOG_WARN("failed to push back new redo log handle", K(ret));
    } else if (OB_FAIL(local_write_ddl_macro_redo(redo_info, ls->get_ls_id(), task_id, ls->get_log_handler(), macro_block_id, buffer_, 
                                                  ddl_redo_handle_array_.at(ddl_redo_handle_array_.count() - 1)))) {
      if (ObDDLUtil::need_remote_write(ret) && allow_remote_write) {
        if (OB_FAIL(switch_to_remote_write())) {
          LOG_WARN("fail to switch to remote write", K(ret));
        }
      } else {
        LOG_ERROR("fail to write ddl redo clog", K(ret), K(MTL_GET_TENANT_ROLE_CACHE()));
      }
    } else {
      LOG_INFO("local write redo log of macro block", K(redo_info), K(macro_block_id));
    }
  }

  if (OB_SUCC(ret) && remote_write_) {
    if (OB_FAIL(retry_remote_write_macro_redo(task_id, redo_info))) {
      LOG_WARN("remote write redo failed", K(ret), K(task_id));
    } else {
      LOG_INFO("remote write redo log of macro block", K(redo_info), K(macro_block_id));
    } 
  }
  return ret;
}

int ObDDLRedoLogWriter::wait_macro_block_log_finish(
    const ObDDLMacroBlockRedoInfo &unused_redo_info,
    const blocksstable::MacroBlockId &macro_block_id)
{
  int ret = OB_SUCCESS;
  int64_t wait_timeout_us = MAX(ObDDLRedoLogHandle::DDL_REDO_LOG_TIMEOUT, GCONF._data_storage_io_timeout * 1);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ddl redo log writer has not been inited", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_redo_handle_array_.count(); i++) {
      if (OB_ISNULL(ddl_redo_handle_array_.at(i).cb_)) { /* cb be null in remote write */
      } else if (!ddl_redo_handle_array_.at(i).is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid handle", K(ret), K(ddl_redo_handle_array_.at(i)));
      } else if (OB_FAIL(ddl_redo_handle_array_.at(i).wait())) {
        LOG_WARN("failed to wait", K(ret));
      } else if (OB_FAIL(ddl_redo_handle_array_.at(i).cb_->get_ret_code())) {
        LOG_WARN("ddl redo callback executed failed", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      DEBUG_SYNC(AFTER_MACRO_BLOCK_WRITER_DDL_CALLBACK_WAIT);
    }
  }
  ddl_redo_handle_array_.reuse();
  return ret;
}

int ObDDLRedoLogWriter::write_commit_log_with_retry(
    const bool allow_remote_write,
    const ObITable::TableKey &table_key,
    const share::SCN &start_scn,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle,
    ObTabletHandle &tablet_handle,
    SCN &commit_scn,
    bool &is_remote_write,
    uint32_t &lock_tid)
{
  int ret = OB_SUCCESS;
  int64_t start_ts = ObTimeUtility::fast_current_time();
  const int64_t timeout_us = ObDDLRedoLogWriter::DEFAULT_RETRY_TIMEOUT_US;
  int64_t retry_count = 0;
  do {
    if (OB_FAIL(THIS_WORKER.check_status())) {
      LOG_WARN("check status failed", K(ret));
    } else if (OB_FAIL(write_commit_log(allow_remote_write, table_key, start_scn, direct_load_mgr_handle, tablet_handle, commit_scn, is_remote_write, lock_tid))) {
      LOG_WARN("write ddl commit log failed", K(ret));
    }
    if (ObDDLRedoLogWriter::need_retry(ret)) {
      ob_usleep(1000L * 1000L); // 1s
      ++retry_count;
      LOG_INFO("retry write ddl commit log", K(ret), K(table_key), K(retry_count));
    } else {
      break;
    }
  } while (ObTimeUtility::fast_current_time() - start_ts < timeout_us);
  return ret;
}

int ObDDLRedoLogWriter::write_commit_log(
    const bool allow_remote_write,
    const ObITable::TableKey &table_key,
    const share::SCN &start_scn,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle,
    ObTabletHandle &tablet_handle,
    SCN &commit_scn,
    bool &is_remote_write,
    uint32_t &lock_tid)
{
  int ret = OB_SUCCESS;
#ifdef ERRSIM
  SERVER_EVENT_SYNC_ADD("storage_ddl", "before_write_prepare_log",
                        "table_key", table_key);
  DEBUG_SYNC(BEFORE_DDL_WRITE_PREPARE_LOG);
#endif
  commit_scn.set_min();
  is_remote_write = false;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  ObDDLCommitLog log;
  ObDDLCommitLogHandle handle;
  ObTabletBindingMdsUserData ddl_data;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ddl redo log writer has not been inited", K(ret));
  } else if (OB_UNLIKELY(!table_key.is_valid() || !start_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(table_key), K(start_scn));
  } else if (OB_FAIL(tablet_handle.get_obj()->ObITabletMdsInterface::get_ddl_data(share::SCN::max_scn(), ddl_data))) {
    LOG_WARN("failed to get ddl data from tablet", K(ret), K(tablet_handle));
  } else if (OB_FAIL(log.init(table_key, start_scn, ddl_data.lob_meta_tablet_id_))) {
    LOG_WARN("fail to init DDLCommitLog", K(ret), K(table_key), K(start_scn), K(ddl_data.lob_meta_tablet_id_));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls_id_, ls_handle, ObLSGetMod::DDL_MOD))) {
    LOG_WARN("get ls failed", K(ret), K(ls_id_));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls should not be null", K(ret), K(table_key));
  } else if (start_scn != direct_load_mgr_handle.get_obj()->get_start_scn()) {
    ret = OB_TASK_EXPIRED;
    LOG_WARN("current task is restarted", K(ret), K(start_scn), "current_start_scn", direct_load_mgr_handle.get_obj()->get_start_scn());
  } else if (direct_load_mgr_handle.get_obj()->get_commit_scn(tablet_handle.get_obj()->get_tablet_meta()).is_valid_and_not_min()) {
    commit_scn = direct_load_mgr_handle.get_obj()->get_commit_scn(tablet_handle.get_obj()->get_tablet_meta());
    LOG_WARN("already committed", K(ret), K(start_scn), K(commit_scn), K(direct_load_mgr_handle.get_obj()->get_start_scn()), K(log));
  } else if (!remote_write_) {
    // direct load mgr handle of lob meta tablet may not bind to data tablet handle, get it manually here
    ObTabletBindingMdsUserData ddl_data;
    ObTabletDirectLoadMgrHandle lob_direct_load_mgr_handle;
    if (OB_FAIL(tablet_handle.get_obj()->ObITabletMdsInterface::get_ddl_data(share::SCN::max_scn(), ddl_data))) {
      LOG_WARN("failed to get ddl data from tablet", K(ret), K(tablet_handle));
    } else if (ddl_data.lob_meta_tablet_id_.is_valid()) {
      bool is_lob_major_sstable_exist = false;
      if (OB_FAIL(share::g_mp->tenant_direct_load_mgr()->get_tablet_mgr_and_check_major(ls_id_, ddl_data.lob_meta_tablet_id_,
              true/* is_full_direct_load */, lob_direct_load_mgr_handle, is_lob_major_sstable_exist))) {
        if (OB_ENTRY_NOT_EXIST == ret && is_lob_major_sstable_exist) {
          ret = OB_SUCCESS;
          LOG_INFO("lob meta tablet exist major sstable, skip", K(ret), K(ddl_data.lob_meta_tablet_id_));
        } else {
          LOG_WARN("get tablet mgr failed", K(ret), K(ddl_data.lob_meta_tablet_id_));
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(local_write_ddl_commit_log(
      log, ObDDLClogType::DDL_COMMIT_LOG, ls_id_, ls->get_log_handler(), direct_load_mgr_handle, lob_direct_load_mgr_handle, handle, lock_tid))) {
      if (ObDDLUtil::need_remote_write(ret) && allow_remote_write) {
        if (OB_FAIL(switch_to_remote_write())) {
          LOG_WARN("fail to switch to remote write", K(ret), K(table_key));
        }
      } else {
        LOG_ERROR("fail to write ddl commit log", K(ret), K(table_key));
      }
    } else if (OB_FAIL(handle.wait())) {
      LOG_WARN("wait ddl commit log finish failed", K(ret), K(table_key));
    } else {
      commit_scn = handle.get_commit_scn();
      LOG_INFO("local write ddl commit log", K(ret), K(table_key), K(commit_scn));
    }
  }
  if (OB_SUCC(ret) && remote_write_) {
    obcall::ObCallRemoteWriteDDLCommitLogArg arg;
    if (OB_FAIL(arg.init(leader_ls_id_, table_key, start_scn))) {
      LOG_WARN("fail to init ObCallRemoteWriteDDLCommitLogArg", K(ret));
    } else if (OB_FAIL(retry_remote_write_commit_clog(arg, commit_scn))) {
      LOG_WARN("remote write ddl commit log failed", K(ret), K(arg));
    } else {
      is_remote_write = !(leader_addr_ == GCTX.self_addr());
      LOG_INFO("remote write ddl commit log", K(ret), K(table_key), K(commit_scn), K(is_remote_write));
    }
  }
  SERVER_EVENT_ADD("ddl", "ddl write commit log",
    "ret", ret,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "start_scn", direct_load_mgr_handle.get_obj()->get_start_scn(),
    "tablet_id", tablet_id_,
    "commit_scn", commit_scn,
    "info", is_remote_write);
  LOG_INFO("ddl write commit log", K(ret), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObDDLRedoLogWriter::switch_to_remote_write()
{
  int ret = OB_SUCCESS;
  
  share::ObLocationService *location_service = nullptr;
  bool is_cache_hit = false;
  if (OB_ISNULL(location_service = GCTX.location_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("location service is null", K(ret), KP(location_service));
  } else if (OB_FAIL(location_service->get(tablet_id_,
                                           INT64_MAX/*expire_renew_time*/,
                                           is_cache_hit,
                                           leader_ls_id_))) {
    LOG_WARN("fail to get log stream id", K(ret), K_(tablet_id));
  } else if (OB_FAIL(location_service->get_leader(GCONF.cluster_id,
                                                  leader_ls_id_,
                                                  true, /*force_renew*/
                                                  leader_addr_))) {
      LOG_WARN("get leader failed", K(ret), K(leader_ls_id_));
  } else if (GCTX.self_addr() == leader_addr_) {
    ret = OB_NOT_MASTER; // switch to local is unexpected, use retry ret code
    remote_write_ = false; 
    LOG_WARN("leader is local", K(ret), K_(tablet_id), K_(leader_ls_id));
  } else {
    remote_write_ = true;
    LOG_INFO("switch to remote write", K(ret), K_(tablet_id), K_(leader_ls_id), K_(leader_addr));
  }
  return ret;
}

int ObDDLRedoLogWriter::retry_remote_write_macro_redo(
    const int64_t task_id,
    const storage::ObDDLMacroBlockRedoInfo &redo_info)
{
  int ret = OB_SUCCESS;
  int retry_cnt = 0;
  const int64_t MAX_REMOTE_WRITE_RETRY_CNT = 800;
  if (OB_UNLIKELY(!redo_info.is_valid() || 0 == task_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(redo_info));
  } else {
    while (OB_SUCC(ret)) {
      if (OB_FAIL(switch_to_remote_write())) {
        LOG_WARN("flush ls leader location failed", K(ret));
      } else if (OB_FAIL(remote_write_ddl_macro_redo(task_id, redo_info))) {
        if (OB_NOT_MASTER == ret && retry_cnt++ < MAX_REMOTE_WRITE_RETRY_CNT) {
          ob_usleep(10 * 1000); // 10 ms.
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("remote write macro redo failed", K(ret), K_(leader_ls_id), K_(leader_addr));
        }
      } else {
        break; // remote write ddl clog successfully.
      }
    }
  }
  return ret;
}

int ObDDLRedoLogWriter::retry_remote_write_commit_clog(
    const obcall::ObCallRemoteWriteDDLCommitLogArg &arg,
    share::SCN &commit_scn)
{
  int ret = OB_SUCCESS;
  int retry_cnt = 0;
  const int64_t MAX_REMOTE_WRITE_RETRY_CNT = 800;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else {
    while (OB_SUCC(ret)) {
      if (OB_FAIL(switch_to_remote_write())) {
        LOG_WARN("flush ls leader location failed", K(ret));
      } else if (OB_FAIL(remote_write_ddl_commit_redo(arg, commit_scn))) {
        if (OB_NOT_MASTER == ret && retry_cnt++ < MAX_REMOTE_WRITE_RETRY_CNT) {
          ob_usleep(10 * 1000); // 10 ms.
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("remote write macro redo failed", K(ret), K_(leader_ls_id), K_(leader_addr));
        }
      } else {
        break; // remote write ddl clog successfully.
      }
    }
  }
  return ret;
}

int ObDDLRedoLogWriter::remote_write_ddl_commit_redo(const obcall::ObCallRemoteWriteDDLCommitLogArg &arg, SCN &commit_scn)
{
  int ret = OB_SUCCESS;
  obcall::Int64 log_ns;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(ex_rpc::sync_call([&]() -> int {
    int ret = OB_SUCCESS;
    MOD_SCOPE {
      ObRole role = INVALID_ROLE;
      const ObITable::TableKey &table_key = arg.table_key_;
      ObDDLRedoLogWriter writer;
      ObLSService *ls_svc = share::g_mp->ls_service();
      ObLSHandle ls_hdl; ObLS *ls = nullptr;
      auto *tlm = share::g_mp->tenant_direct_load_mgr();
      ObTabletFullDirectLoadMgr *dtm = nullptr; ObTabletDirectLoadMgrHandle dmh;
      dmh.reset(); bool major_exist = false;
      if (OB_FAIL(ls_svc->get_ls(arg.ls_id_, ls_hdl, ObLSGetMod::OBSERVER_MOD))) { LOG_WARN("get ls failed", K(ret)); }
      else if (OB_ISNULL(ls = ls_hdl.get_ls())) { ret = OB_ERR_UNEXPECTED; }
      else if (OB_FAIL(ls->get_ls_role(role))) { LOG_WARN("get role failed", K(ret)); }
      else if (ObRole::LEADER != role) { ret = OB_NOT_MASTER; }
      else if (OB_ISNULL(tlm)) { ret = OB_ERR_UNEXPECTED; }
      else if (OB_FAIL(tlm->get_tablet_mgr_and_check_major(arg.ls_id_, table_key.tablet_id_, true, dmh, major_exist))) {
        if (OB_ENTRY_NOT_EXIST == ret && major_exist) { ret = OB_TASK_EXPIRED; }
      } else if (OB_ISNULL(dtm = dmh.get_full_obj())) {
        ret = OB_ERR_UNEXPECTED;
      }
      else if (OB_FAIL(writer.init(arg.ls_id_, table_key.tablet_id_))) { LOG_WARN("init failed", K(ret)); }
      else {
        uint32_t lock_tid = 0; SCN scn_val; bool remote = false; ObTabletHandle th;
        if (OB_FAIL(dtm->wrlock(ObTabletDirectLoadMgr::TRY_LOCK_TIMEOUT, lock_tid))) { LOG_WARN("wrlock failed", K(ret)); }
        else if (OB_FAIL(ls->get_tablet(table_key.tablet_id_, th, ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US, ObMDSGetTabletMode::READ_WITHOUT_CHECK))) { LOG_WARN("get tablet failed", K(ret)); }
        else if (OB_FAIL(writer.write_commit_log(false, table_key, arg.start_scn_, dmh, th, scn_val, remote, lock_tid))) { LOG_WARN("write commit log failed", K(ret)); }
        else if (!dtm->get_lob_mgr_handle().is_valid()) {
          ObTabletBindingMdsUserData ddl_data; ObTabletDirectLoadMgrHandle lob_hdl;
          if (OB_FAIL(th.get_obj()->ObITabletMdsInterface::get_ddl_data(share::SCN::max_scn(), ddl_data))) { LOG_WARN("get ddl data failed", K(ret)); }
          else if (ddl_data.lob_meta_tablet_id_.is_valid()) {
            bool lob_exist = false;
            if (OB_FAIL(share::g_mp->tenant_direct_load_mgr()->get_tablet_mgr_and_check_major(arg.ls_id_, ddl_data.lob_meta_tablet_id_, true, lob_hdl, lob_exist))) {
              if (OB_ENTRY_NOT_EXIST != ret || !lob_exist) { LOG_WARN("get lob mgr failed", K(ret)); } else { ret = OB_SUCCESS; }
            } else if (OB_FAIL(lob_hdl.get_full_obj()->commit(*th.get_obj(), arg.start_scn_, scn_val, arg.table_id_, arg.ddl_task_id_, false))) { LOG_WARN("lob commit failed", K(ret)); }
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(dtm->commit(*th.get_obj(), arg.start_scn_, scn_val, arg.table_id_, arg.ddl_task_id_, false))) { LOG_WARN("kv commit failed", K(ret)); }
        else if (OB_SUCC(ret)) { log_ns = scn_val.get_val_for_tx(); }
        if (lock_tid != 0) { dtm->unlock(lock_tid); }
      }
    }
    return ret;
  }))) {
    LOG_WARN("write ddl commit log failed", K(ret), K_(leader_ls_id), K_(leader_addr));
  } else if (OB_FAIL(commit_scn.convert_for_tx(log_ns))) {
    LOG_WARN("convert for tx failed", K(ret));
  }
  return ret;
}

int ObDDLRedoLogWriter::write_block_to_disk(const ObDDLMacroBlockRedoInfo &redo_info, const ObLSID &ls_id, 
                                            blocksstable::ObMacroBlockHandle &macro_handle, blocksstable::MacroBlockId &macro_id) 
{
  int ret = OB_SUCCESS;
  macro_handle.reset();
  macro_id.reset();
  if (!redo_info.is_valid() || !ls_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(redo_info), K(ls_id));
  } else {
    ObMacroBlockWriteInfo write_info;
    write_info.buffer_ = redo_info.data_buffer_.ptr();
    write_info.size_= redo_info.data_buffer_.length();
    write_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_COMPACT_WRITE);
    write_info.io_timeout_ms_ = max(DDL_FLUSH_MACRO_BLOCK_TIMEOUT / 1000L, GCONF._data_storage_io_timeout / 1000L);
    if (OB_FAIL(ObBlockManager::async_write_block(write_info, macro_handle))) {
      LOG_WARN("fail to async write block", K(ret), K(write_info), K(macro_handle));
    } else if (OB_FAIL(macro_handle.wait())) {
      LOG_WARN("fail to wait macro block io finish", K(ret));
    } else {
      macro_id = macro_handle.get_macro_id();
    }
  } 
  return ret;
}

ObDDLRedoLogWriter::~ObDDLRedoLogWriter()
{
  if (nullptr != buffer_) {
    ob_free(buffer_);
    buffer_ = nullptr;
  }
}

ObDDLRedoLogWriterCallbackInitParam::ObDDLRedoLogWriterCallbackInitParam()
  : direct_load_type_(DIRECT_LOAD_INVALID),
    block_type_(ObDDLMacroBlockType::DDL_MB_INVALID_TYPE),
    table_key_(),
    start_scn_(),
    task_id_(0),
    data_format_version_(0),
    parallel_cnt_(0),
    need_delay_(false),
    need_submit_io_(true),
    merge_slice_idx_(0),
    macro_meta_store_(nullptr),
    write_stat_(nullptr),
    tx_desc_(nullptr)
{
}

ObDDLRedoLogWriterCallbackInitParam::~ObDDLRedoLogWriterCallbackInitParam()
{
}

bool ObDDLRedoLogWriterCallbackInitParam::is_valid() const
{
  return ls_id_.is_valid()
          && tablet_id_.is_valid()
          && table_key_.is_valid()
          && (DDL_MB_INVALID_TYPE != block_type_)
          && (0 != task_id_)
          && (data_format_version_ >= 0)
          && is_valid_direct_load(direct_load_type_);
}

void ObDDLRedoLogWriterCallbackInitParam::reset()
{
  ls_id_.reset();
  tablet_id_.reset();
  direct_load_type_ = DIRECT_LOAD_INVALID;
  block_type_ = ObDDLMacroBlockType::DDL_MB_INVALID_TYPE;
  table_key_.reset();
  start_scn_.reset();
  task_id_ = 0;
  data_format_version_ = 0;
  parallel_cnt_ = 0;
  need_delay_ = false;
  need_submit_io_ = true;
  merge_slice_idx_ = 0;
  macro_meta_store_ = nullptr;
  write_stat_ = nullptr;
  tx_desc_ = nullptr;
}

ObDDLRedoLogWriterCallback::ObDDLRedoLogWriterCallback()
  : is_inited_(false), param_(), ddl_writer_(), kv_mgr_handle_(), allocator_(), redo_info_array_()
{
  redo_info_array_.set_attr(lib::ObMemAttr("DdlRedoInfo"));
}

ObDDLRedoLogWriterCallback::~ObDDLRedoLogWriterCallback()
{
  (void)wait();
}

int ObDDLRedoLogWriterCallback::init(ObDDLRedoLogWriterCallbackInitParam &init_param)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObLSService *ls_service = nullptr;
  bool is_cache_hit = false;
  ObLSHandle ls_handle;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ddl redo log writer has been inited twice", K(ret));
  } else if (OB_UNLIKELY(!init_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid init param", KR(ret), K(init_param));
  } else if (OB_FAIL(ddl_writer_.init(init_param.ls_id_, init_param.tablet_id_))) {
    LOG_WARN("fail to init ddl_writer_", K(ret), K(init_param.ls_id_), K(init_param.tablet_id_));
  } else {
    // init kv mgr handle for idempotence check
    ObLSService *ls_service = share::g_mp->ls_service();
    ObLSHandle ls_handle;
    ObTabletHandle tablet_handle;
    if (OB_FAIL(ls_service->get_ls(init_param.ls_id_, ls_handle, ObLSGetMod::DDL_MOD))) {
      LOG_WARN("get ls failed", K(ret), K(init_param.ls_id_));
    } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls_handle, init_param.tablet_id_, tablet_handle))) {
      LOG_WARN("get tablet failed", K(ret), K(init_param.ls_id_), K(init_param.tablet_id_));
    } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(kv_mgr_handle_, true /*try_create*/))) {
      LOG_WARN("get ddl kv mgr handle failed", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    param_ = init_param;
    is_inited_ = true;
  }
  return ret;
}

void ObDDLRedoLogWriterCallback::reset()
{
  is_inited_ = false;
  param_.reset();
  ddl_writer_.reset();
  kv_mgr_handle_.reset();
  allocator_.reuse();
}

// check the checksum of macro block
int ObDDLRedoLogWriterCallback::write(const ObStorageObjectHandle &macro_handle,
                                      const ObLogicMacroBlockId &logic_id,
                                      char *buf,
                                      const int64_t buf_len,
                                      const int64_t row_count)
{
  int ret = OB_SUCCESS;
  storage::ObDDLMacroBlockRedoInfo redo_info;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogWriterCallback is not inited", K(ret));
  } else if (OB_UNLIKELY((buf_len <= 0 || nullptr == buf ||
                          (ObDDLMacroBlockType::DDL_MB_DATA_TYPE == param_.block_type_ && row_count <= 0)))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(buf_len), KP(buf), K(param_.block_type_), K(row_count));
  } else if ((!logic_id.is_valid() || (!macro_handle.is_valid() && param_.need_submit_io_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid logic id", K(ret), K(logic_id), K(macro_handle), K_(param_.need_submit_io)); /* only in shared storage is logic id needed */
  }

  if (OB_SUCC(ret)) {
    MacroBlockId macro_block_id = macro_handle.get_macro_id();
    redo_info.table_key_ = param_.table_key_;
    redo_info.block_type_ = param_.block_type_;
    redo_info.logic_id_ = logic_id;
    redo_info.start_scn_ = param_.start_scn_;
    redo_info.macro_block_id_ = macro_handle.get_macro_id();
    redo_info.type_ = param_.direct_load_type_;
    redo_info.data_format_version_ = param_.data_format_version_;
    redo_info.parallel_cnt_ = 0; // TODO @zhuoran.zzr, place holder for shared storage
    if (ObDDLMacroBlockType::DDL_MB_SS_EMPTY_DATA_TYPE == param_.block_type_) {
      redo_info.data_buffer_.assign(nullptr, 0);
    } else {
      redo_info.data_buffer_.assign(buf, buf_len);
    }
    if (OB_FAIL(ret)) {
    } else if (nullptr != param_.macro_meta_store_ && OB_FAIL(param_.macro_meta_store_->append(buf, buf_len, macro_handle.get_macro_id()))) {
        LOG_WARN("append macro meta store failed", K(ret), KP(buf), K(buf_len), K(macro_handle.get_macro_id()));
    } else {
      LOG_TRACE("append macro meta store", K(ret), K(param_.table_key_), KPC(param_.macro_meta_store_));
    }

    if (OB_SUCC(ret) && nullptr != param_.write_stat_) {
      ATOMIC_AAF(&param_.write_stat_->row_count_, row_count);
      LOG_TRACE("update write stat", K(ret), K(param_.table_key_), K(row_count), KPC(param_.write_stat_));
    }
    
    if (OB_FAIL(ret)) {
    } else if (param_.need_delay_) {
      char *tmp_buf = nullptr;
      if (ObDDLMacroBlockType::DDL_MB_SS_EMPTY_DATA_TYPE == param_.block_type_) {
        redo_info.data_buffer_.assign(nullptr, 0);
      } else {
        if (OB_ISNULL(tmp_buf = (char*)(allocator_.alloc(buf_len)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to alloc buf", K(ret));
        } else if (FALSE_IT(MEMCPY(tmp_buf, buf, buf_len))) {
        } else if (FALSE_IT(redo_info.data_buffer_.assign(tmp_buf, buf_len))) {
        }
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(redo_info_array_.push_back(redo_info))) {
        LOG_WARN("failed to push back val", K(ret));
      } else if (redo_info_array_.count() > 10) {
        /* write some warn info, since redo info array should not be too large*/
        LOG_WARN("too much element in redo log callback", K(redo_info_array_.count()), K(lbt()));
      }
    } else {
      if (OB_FAIL(inner_write(redo_info))) {
        LOG_WARN("failed to write macro block", K(ret));
      }
    }
  }
  return ret;
}

int ObDDLRedoLogWriterCallback::inner_write(const ObDDLMacroBlockRedoInfo &redo_info) 
{
  int ret = OB_SUCCESS;
  if (!redo_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(redo_info));
  } else if (OB_FAIL(ddl_writer_.write_macro_block_log(redo_info, redo_info.macro_block_id_, true/*allow remote write*/, param_.task_id_))) {
    LOG_ERROR("fail to write ddl redo log", K(ret), K(redo_info), K(param_.task_id_));
    if (ObDDLRedoLogWriter::need_retry(ret)) {
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(retry(ObDDLRedoLogWriter::DEFAULT_RETRY_TIMEOUT_US, redo_info, redo_info.macro_block_id_))) {
        LOG_WARN("retry wirte ddl macro redo log failed", K(ret), K(tmp_ret), K(param_.task_id_), K(param_.table_key_));
      } else {
        ret = OB_SUCCESS; // overwrite the return code
      }
    }
  }
  return ret;
}

int ObDDLRedoLogWriterCallback::write_redo_info_array()
{
  int ret = OB_SUCCESS;
  if (0 == redo_info_array_.count()) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < redo_info_array_.count(); i++) {
      if (OB_FAIL(inner_write(redo_info_array_.at(i)))) {
        LOG_WARN("failed to write redo info", K(ret));
      }
    }
    allocator_.reuse();
    redo_info_array_.reuse();
  }
  return ret;
}

int ObDDLRedoLogWriterCallback::wait()
{
  int ret = OB_SUCCESS;
  storage::ObDDLMacroBlockRedoInfo unused_redo_info;
  blocksstable::MacroBlockId macro_block_id;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogWriterCallback is not inited", K(ret));
  } else if (param_.need_delay_ && OB_FAIL(write_redo_info_array())) {
    LOG_WARN("fail to write redo info to array", K(ret));
  } 
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ddl_writer_.wait_macro_block_log_finish(unused_redo_info, macro_block_id))) {
    LOG_WARN("fail to wait redo log finish", K(ret));
  }
  if (OB_SUCC(ret) && nullptr != param_.macro_meta_store_) {
    if (OB_FAIL(param_.macro_meta_store_->wait())) {
      LOG_WARN("fail to wait macro meta store", K(ret));
    }
  }
  return ret;
}

int ObDDLRedoLogWriterCallback::retry(const int64_t timeout_us, 
                                      const ObDDLMacroBlockRedoInfo &redo_info, 
                                      const blocksstable::MacroBlockId &macro_block_id)
{
  int ret = OB_SUCCESS;
  int64_t retry_count = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogWriterCallback is not inited", K(ret));
  } else if (timeout_us <= 0) {
    ret = OB_TIMEOUT;
    LOG_WARN("timeout less than 0", K(ret), K(timeout_us));
  } else if (OB_UNLIKELY(!macro_block_id.is_valid() || !redo_info.is_valid())) {
    ret = OB_ERR_SYS;
    LOG_WARN("macro block id or redo info not valid", K(ret), K(macro_block_id), K(redo_info));
  } else {
    int64_t start_ts = ObTimeUtility::fast_current_time();
    while (ObTimeUtility::fast_current_time() - start_ts < timeout_us) { // ignore ret
      if (OB_FAIL(THIS_WORKER.check_status())) {
        LOG_WARN("check status failed", K(ret));
      } else if (OB_FAIL(ddl_writer_.write_macro_block_log(redo_info, macro_block_id, true/*allow remote write*/, param_.task_id_))) {
        LOG_WARN("fail to write ddl redo log", K(ret));
      } else if (OB_FAIL(ddl_writer_.wait_macro_block_log_finish(redo_info, macro_block_id))) {
        LOG_WARN("wait ddl redo log finish failed", K(ret));
      } else {
        FLOG_INFO("retry write ddl macro redo success", K(ret), K(param_.table_key_), K(macro_block_id));
      }
      if (ObDDLRedoLogWriter::need_retry(ret)) {
        ob_usleep(1000L * 1000L); // 1s
        ++retry_count;
        LOG_INFO("retry write ddl macro redo log", K(ret), K(param_.table_key_), K(retry_count));
      } else {
        break;
      }
    }
  }
  return ret;
}
