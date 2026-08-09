#include "share/rc/ob_server_runtime.h"
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
#include "logservice/ob_log_service.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/ddl/ob_ddl_merge_task.h"
#include "storage/ddl/ob_direct_insert_sstable_ctx.h"
#include "share/ob_structured_event_logger.h"
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

int ObDDLCtrlSpeedItem::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("inited twice", K(ret));
  } else {
    next_available_write_ts_ = ObTimeUtility::current_time();
    if (OB_FAIL(refresh())) {
    } else {
      is_inited_ = true;
      LOG_INFO("succeed to init ObDDLCtrlSpeedItem", K(ret), K(is_inited_),
        K(next_available_write_ts_), K(write_speed_), K(disk_used_stop_write_threshold_));
    }
  }
  return ret;
}

// Refresh DDL log write speed and the database-wide disk threshold.
int ObDDLCtrlSpeedItem::refresh()
{
  int ret = OB_SUCCESS;
  int64_t refresh_speed = 0;
  int64_t total_used_space = 0; // for current tenant, used bytes.
  int64_t total_disk_space = 0; // for current tenant, limit used bytes.
  palf::PalfOptions palf_opt;
  logservice::ObLogService *log_service = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
  if (OB_ISNULL(log_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, nullptr found", K(ret), KP(log_service));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(log_service->get_palf_options(palf_opt))) {
  } else if (OB_FAIL(log_service->get_palf_disk_usage(total_used_space, total_disk_space))) {
  } else if (OB_ISNULL(GCTX.bandwidth_throttle_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, bandwidth throttle is null", K(ret), KP(GCTX.bandwidth_throttle_));
  } else if (OB_FAIL(GCTX.bandwidth_throttle_->get_rate(refresh_speed))) {
  } else {
    write_speed_ = std::max(refresh_speed, 1 * MIN_WRITE_SPEED);
    disk_used_stop_write_threshold_ = min(0 == palf_opt.disk_options_.log_disk_utilization_threshold_ ?
                                          palf::DEFAULT_LOG_UTL_THRESHOLD : palf_opt.disk_options_.log_disk_utilization_threshold_,
                                          palf_opt.disk_options_.log_disk_utilization_limit_threshold_);
    need_stop_write_ = 100.0 * total_used_space / total_disk_space >= disk_used_stop_write_threshold_ ? true : false;
  }
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
  } else if (next_available_ts <= 0 || task_id == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument.", K(ret), K(next_available_ts), K(task_id));
  } else if (OB_FAIL(DDL_SIM(task_id, DDL_REDO_WRITER_SPEED_CONTROL_FAILED))) {
  } else if (OB_TMP_FAIL(check_need_stop_write(checker, is_need_stop_write))) {
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
        if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::common::ObMySQLProxy>())) {
          tmp_ret = OB_NOT_INIT;
          LOG_WARN("sql proxy is not initialized", K(tmp_ret), K(task_id));
        } else if (OB_TMP_FAIL(ObDDLUtil::get_data_information(
                       *::oceanbase::share::server_service<::oceanbase::common::ObMySQLProxy>(), task_id, unused_data_format_version,
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
        } else if (!is_local_build_ddl_task_status(task_status)) {
          is_need_stop_write = false;
          LOG_INFO("exit due to mismatched status", K(task_id));
        }
      }
      if (REACH_TIME_INTERVAL(10 * 1000 * 1000)) {
        ObTaskController::get().allow_next_syslog();
        FLOG_INFO("stop write ddl clog", K(ret),
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
    is_need_stop_write = checker.check_need_stop_write() || need_stop_write_;
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
      || disk_used_stop_write_threshold_ > 100) || bytes < 0 || 0 == task_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument.", K(ret), K(disk_used_stop_write_threshold_), K(bytes), K(task_id));
  } else if (OB_FAIL(cal_limit(bytes, next_available_ts))) {
  } else if (OB_ISNULL(GCTX.bandwidth_throttle_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, bandwidth throttle is null", K(ret), KP(GCTX.bandwidth_throttle_));
  } else if (OB_FAIL(GCTX.bandwidth_throttle_->limit_out_and_sleep(bytes,
                                                                   ObTimeUtility::current_time(),
                                                                   INT64_MAX,
                                                                   &transmit_sleep_us))) {
  } else if (OB_FAIL(do_sleep(next_available_ts, task_id, checker, real_sleep_us))) {
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
  } else {
    is_inited_ = true;
    LOG_INFO("succeed to init ObDDLCtrlSpeedHandle", K(ret));
  }
  return ret;
}

int ObDDLCtrlSpeedHandle::limit_and_sleep(const int64_t bytes,
                                          const int64_t task_id,
                                          ObDDLNeedStopWriteChecker &checker,
                                          int64_t &real_sleep_us)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(bytes < 0 || 0 == task_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(task_id), K(bytes));
  }
  if (OB_SUCC(ret) && OB_FAIL(speed_handle_item_.init())) {
    if (OB_INIT_TWICE != ret) {
      LOG_WARN("fail to init speed handle item", K(ret));
    } else {
      ret = OB_SUCCESS; // already inited, treat as success
    }
  }
  if (OB_SUCC(ret)) {
    ret = speed_handle_item_.limit_and_sleep(bytes, task_id, checker, real_sleep_us);
    if (OB_FAIL(ret)) {
    }
  }
  return ret;
}

int ObDDLCtrlSpeedHandle::refresh()
{
  int ret = OB_SUCCESS;
  SERVER_MODULE_SCOPE {
    if (OB_FAIL(speed_handle_item_.refresh())) {
    }
  } else if (OB_SERVER_RUNTIME_NOT_READY == ret || OB_IN_STOP_STATE == ret) {
    speed_handle_item_.reset_need_stop_write();
    ret = OB_SUCCESS;
  } else {
    LOG_WARN("enter server module scope failed", K(ret));
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
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObDDLRedoLogWriter::local_write_ddl_macro_redo(
    const ObDDLMacroBlockRedoInfo &redo_info,
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
  SCN base_scn = SCN::min_scn();
  SCN scn;
  int64_t real_sleep_us = 0;
  int tmp_ret = OB_SUCCESS;

  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  ddl_kv_mgr_handle.reset();
  if (OB_UNLIKELY(!redo_info.is_valid()
                  || nullptr == log_handler
                  || nullptr == buffer
                  || 0 == task_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(redo_info), KP(log_handler), KP(buffer), K(task_id));
  } else if (OB_FAIL(log.init(redo_info))) {
  } else if (FALSE_IT(buffer_size = base_header.get_serialize_size()
                                    + ddl_header.get_serialize_size()
                                    + log.get_serialize_size())) {
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
  } else if (OB_FAIL(ls->get_tablet(log.get_redo_info().table_key_.tablet_id_, tablet_handle, ObTabletCommon::DEFAULT_GET_TABLET_NO_WAIT, ObMDSGetTabletMode::READ_ALL_COMMITED))) {
  } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle, 
                                                             ObDDLUtil::use_idempotent_mode()))) {
  } else {
    ObDDLFullNeedStopWriteChecker checker(ddl_kv_mgr_handle);
    if (OB_TMP_FAIL(ObDDLCtrlSpeedHandle::get_instance().limit_and_sleep(buffer_size, task_id, checker, real_sleep_us))) {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(cb = op_alloc(ObDDLMacroBlockClogCb))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(base_header.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(ddl_header.serialize(buffer, buffer_size, pos))) {
  } else if (FALSE_IT(log_start_pos = pos)) {
  } else if (OB_FAIL(log.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(tmp_log.deserialize(buffer, buffer_size, log_start_pos))) {
  } else if (OB_FAIL(cb->init(tmp_log.get_redo_info(), macro_block_id, tablet_handle, tmp_log.get_redo_info().type_))) {
  } else if (OB_FAIL(DDL_SIM(task_id, DDL_REDO_WRITER_WRITE_MACRO_LOG_FAILED))) {
  } else if (OB_FAIL(log_handler->append(buffer,
                                         buffer_size,
                                         base_scn,
                                         need_nonblock,
                                         cb,
                                         lsn,
                                         scn))) {
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
    ObLS *ls,
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
  SCN scn = SCN::min_scn();
  ObDDLRedoLockGuard guard(log.get_table_key().get_tablet_id().hash());
  if (OB_ISNULL(cb = op_alloc(ObDDLStartClogCb))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(cb->init(log.get_table_key(), log.get_data_format_version(), log.get_execution_id(),
    ddl_kv_mgr_handle, lob_kv_mgr_handle, direct_load_mgr_handle, lock_tid))) {
  } else if (OB_FAIL(base_header.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(ddl_header.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(log.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(ls->get_ddl_log_handler()->add_tablet(log.get_table_key().get_tablet_id()))) {
  } else if (lob_kv_mgr_handle.is_valid() && OB_FAIL(ls->get_ddl_log_handler()->add_tablet(lob_kv_mgr_handle.get_obj()->get_tablet_id()))) {
    LOG_WARN("add lob tablet failed", K(ret), "lob_tablet_id", lob_kv_mgr_handle.get_obj()->get_tablet_id());
  } else if (OB_FAIL(log_handler->append(buffer,
                                         buffer_size,
                                         SCN::min_scn(),
                                         need_nonblock,
                                         cb,
                                         lsn,
                                         scn))) {
  } else {
    ObDDLStartClogCb *tmp_cb = cb;
    cb = nullptr;
    lock_tid = 0;
    bool finish = false;
    const int64_t start_time = ObTimeUtility::current_time();
    start_scn = scn;
    while (OB_SUCC(ret) && !finish) {
      if (OB_FAIL(THIS_WORKER.check_status())) {
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
  SCN base_scn = SCN::min_scn();
  SCN scn = SCN::min_scn();
if (OB_ISNULL(buffer = static_cast<char *>(ob_malloc(buffer_size, ObMemAttr("DDL_COMMIT_LOG"))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_ISNULL(cb = op_alloc(ObDDLCommitClogCb))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(cb->init(log.get_table_key().tablet_id_, log.get_start_scn(), lock_tid, direct_load_mgr_handle, lob_direct_load_mgr_handle))) {
  } else if (OB_FAIL(base_header.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(ddl_header.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(log.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(OB_TS_MGR.get_gts_sync(ObDDLRedoLogHandle::DDL_REDO_LOG_TIMEOUT, base_scn))) {
  } else if (OB_FAIL(log_handler->append(buffer,
                                         buffer_size,
                                         base_scn,
                                         need_nonblock,
                                         cb,
                                         lsn,
                                         scn))) {
  } else {
    ObDDLCommitClogCb *tmp_cb = cb;
    cb = nullptr;
    lock_tid = 0;
    if (OB_FAIL(OB_TS_MGR.wait_gts_elapse(scn))) {
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
int ObDDLRedoLogWriter::write_auto_fork_log(
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
  ObLS *ls = nullptr;
  if (OB_UNLIKELY(ObDDLClogType::DDL_TABLE_FORK_FREEZE_LOG != clog_type &&
                          ObDDLClogType::DDL_TABLE_FORK_START_LOG != clog_type &&
                          ObDDLClogType::DDL_TABLE_FORK_FINISH_LOG != clog_type) ||
      OB_UNLIKELY(!log.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(clog_type), K(log));
  } else if (OB_ISNULL(buffer = static_cast<char *>(tmp_arena.alloc(buffer_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc failed", K(ret), K(buffer_size));
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
  } else if (OB_ISNULL(ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("local ls is null", K(ret));
  } else if (OB_ISNULL(cb = op_alloc(ObDDLClogCb))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(base_header.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(ddl_header.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(log.serialize(buffer, buffer_size, pos))) {
  } else if (OB_FAIL(ls->get_log_handler()->append(buffer,
                                         buffer_size,
                                         SCN::min_scn(),
                                         need_nonblock,
                                         cb,
                                         lsn,
                                         scn))) {
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
  LOG_INFO("write fork log finished", K(ret), K(source_tablet_ids), K(clog_type), K(replay_barrier_type), K(scn));
  return ret;
}

template int ObDDLRedoLogWriter::write_auto_fork_log(const ObDDLClogType &clog_type,
                                              const ObReplayBarrierType &replay_barrier_type,
                                              const ObTableForkFreezeLog &log,
                                              SCN &scn);
template int ObDDLRedoLogWriter::write_auto_fork_log(const ObDDLClogType &clog_type,
                                              const ObReplayBarrierType &replay_barrier_type,
                                              const ObTableForkStartLog &log,
                                              SCN &scn);
template int ObDDLRedoLogWriter::write_auto_fork_log(const ObDDLClogType &clog_type,
                                              const ObReplayBarrierType &replay_barrier_type,
                                              const ObTableForkFinishLog &log,
                                              SCN &scn);

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
      } else if (cb_->is_success()) {
        finish = true;
        ret = cb_->get_ret_code();
        if (OB_FAIL(ret)) {
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
  if (nullptr != cb_) {
    cb_->try_release();
    cb_ = nullptr;
  }
}

ObDDLRedoLogWriter::ObDDLRedoLogWriter()
  : is_inited_(false), tablet_id_(), ddl_redo_handle_array_(), buffer_(nullptr)
{
  ddl_redo_handle_array_.set_attr(lib::ObMemAttr("DdlWriteHdl"));
} 

int ObDDLRedoLogWriter::init(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ddl redo log writer has been inited twice", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id));
  } else {
    tablet_id_ = tablet_id;
    is_inited_ = true;
  }
  return ret;
}

void ObDDLRedoLogWriter::reset()
{
  is_inited_ = false;
  tablet_id_.reset();
  ddl_redo_handle_array_.reuse();
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
  ObTabletHandle tablet_handle;
  start_scn.set_min();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ddl redo log writer has not been inited", K(ret));
  } else if (OB_UNLIKELY(!table_key.is_valid() || execution_id < 0 || data_format_version <= 0 || !is_full_direct_load(direct_load_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(table_key), K(execution_id), K(data_format_version), K(direct_load_type));
  } else if (OB_FAIL(log.init(table_key, data_format_version, execution_id, direct_load_type,
          lob_kv_mgr_handle.is_valid() ? lob_kv_mgr_handle.get_obj()->get_tablet_id() : ObTabletID()))) {
  }  else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
  } else if (OB_FAIL(local_write_ddl_start_log(log, ls, ls->get_log_handler(),
      ddl_kv_mgr_handle, lob_kv_mgr_handle, direct_load_mgr_handle, lock_tid, start_scn))) {
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
    const int64_t task_id)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  const int64_t BUF_SIZE = 2 * 1024 * 1024 + 16 * 1024;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ddl redo log writer has not been inited", K(ret));
  } else if (OB_UNLIKELY(!redo_info.is_valid() || 0 == task_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(redo_info), K(task_id));
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
  } else if (nullptr == buffer_ && OB_ISNULL(buffer_ = static_cast<char *>(ob_malloc(BUF_SIZE, ObMemAttr("DDL_REDO_LOG"))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret), K(BUF_SIZE));
  } else if (OB_FAIL(ddl_redo_handle_array_.push_back(ObDDLRedoLogHandle()))) {
  } else if (OB_FAIL(local_write_ddl_macro_redo(redo_info, task_id,
      ls->get_log_handler(), macro_block_id, buffer_,
      ddl_redo_handle_array_.at(ddl_redo_handle_array_.count() - 1)))) {
  } else {
    LOG_INFO("write redo log of macro block", K(redo_info), K(macro_block_id));
  }
  return ret;
}

int ObDDLRedoLogWriter::wait_macro_block_log_finish()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ddl redo log writer has not been inited", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_redo_handle_array_.count(); i++) {
      if (OB_ISNULL(ddl_redo_handle_array_.at(i).cb_)) {
      } else if (!ddl_redo_handle_array_.at(i).is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid handle", K(ret), K(ddl_redo_handle_array_.at(i)));
      } else if (OB_FAIL(ddl_redo_handle_array_.at(i).wait())) {
      } else if (OB_FAIL(ddl_redo_handle_array_.at(i).cb_->get_ret_code())) {
      }
    }
    if (OB_SUCC(ret)) {
      DEBUG_SYNC(AFTER_MACRO_BLOCK_WRITER_DDL_CALLBACK_WAIT);
    }
  }
  ddl_redo_handle_array_.reuse();
  return ret;
}

int ObDDLRedoLogWriter::write_commit_log(
    const ObITable::TableKey &table_key,
    const share::SCN &start_scn,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle,
    ObTabletHandle &tablet_handle,
    SCN &commit_scn,
    uint32_t &lock_tid)
{
  int ret = OB_SUCCESS;
#ifdef ERRSIM
  SERVER_EVENT_SYNC_ADD("storage_ddl", "before_write_prepare_log",
                        "table_key", table_key);
  DEBUG_SYNC(BEFORE_DDL_WRITE_PREPARE_LOG);
#endif
  commit_scn.set_min();
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
  } else if (OB_FAIL(log.init(table_key, start_scn, ddl_data.lob_meta_tablet_id_))) {
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
  } else if (start_scn != direct_load_mgr_handle.get_obj()->get_start_scn()) {
    ret = OB_TASK_EXPIRED;
    LOG_WARN("current task is restarted", K(ret), K(start_scn), "current_start_scn", direct_load_mgr_handle.get_obj()->get_start_scn());
  } else if (direct_load_mgr_handle.get_obj()->get_commit_scn(tablet_handle.get_obj()->get_tablet_meta()).is_valid_and_not_min()) {
    commit_scn = direct_load_mgr_handle.get_obj()->get_commit_scn(tablet_handle.get_obj()->get_tablet_meta());
    LOG_WARN("already committed", K(ret), K(start_scn), K(commit_scn), K(direct_load_mgr_handle.get_obj()->get_start_scn()), K(log));
  } else {
    // direct load mgr handle of lob meta tablet may not bind to data tablet handle, get it manually here
    ObTabletBindingMdsUserData ddl_data;
    ObTabletDirectLoadMgrHandle lob_direct_load_mgr_handle;
    if (OB_FAIL(tablet_handle.get_obj()->ObITabletMdsInterface::get_ddl_data(share::SCN::max_scn(), ddl_data))) {
    } else if (ddl_data.lob_meta_tablet_id_.is_valid()) {
      bool is_lob_major_sstable_exist = false;
      if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObDirectLoadMgr>()->get_tablet_mgr_and_check_major(ddl_data.lob_meta_tablet_id_,
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
      log, ObDDLClogType::DDL_COMMIT_LOG, ls->get_log_handler(), direct_load_mgr_handle, lob_direct_load_mgr_handle, handle, lock_tid))) {
    } else if (OB_FAIL(handle.wait())) {
    } else {
      commit_scn = handle.get_commit_scn();
      LOG_INFO("local write ddl commit log", K(ret), K(table_key), K(commit_scn));
    }
  }
  SERVER_EVENT_ADD("ddl", "ddl write commit log",
    "ret", ret,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "start_scn", direct_load_mgr_handle.get_obj()->get_start_scn(),
    "tablet_id", tablet_id_,
    "commit_scn", commit_scn);
  LOG_INFO("ddl write commit log", K(ret), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));
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
  : tablet_id_(),
    direct_load_type_(DIRECT_LOAD_INVALID),
    block_type_(ObDDLMacroBlockType::DDL_MB_INVALID_TYPE),
    table_key_(),
    start_scn_(),
    task_id_(0),
    data_format_version_(0),
    need_delay_(false),
    need_submit_io_(true),
    merge_slice_idx_(0),
    macro_meta_store_(nullptr),
    write_stat_(nullptr)
{
}

ObDDLRedoLogWriterCallbackInitParam::~ObDDLRedoLogWriterCallbackInitParam()
{
}

bool ObDDLRedoLogWriterCallbackInitParam::is_valid() const
{
  return tablet_id_.is_valid()
          && table_key_.is_valid()
          && (DDL_MB_INVALID_TYPE != block_type_)
          && (0 != task_id_)
          && (data_format_version_ >= 0)
          && is_full_direct_load(direct_load_type_);
}

void ObDDLRedoLogWriterCallbackInitParam::reset()
{
  tablet_id_.reset();
  direct_load_type_ = DIRECT_LOAD_INVALID;
  block_type_ = ObDDLMacroBlockType::DDL_MB_INVALID_TYPE;
  table_key_.reset();
  start_scn_.reset();
  task_id_ = 0;
  data_format_version_ = 0;
  need_delay_ = false;
  need_submit_io_ = true;
  merge_slice_idx_ = 0;
  macro_meta_store_ = nullptr;
  write_stat_ = nullptr;
}

ObDDLRedoLogWriterCallback::ObDDLRedoLogWriterCallback()
  : is_inited_(false), param_(), ddl_writer_(), kv_mgr_handle_(), allocator_(),
    redo_info_array_(), macro_block_id_array_()
{
  redo_info_array_.set_attr(lib::ObMemAttr("DdlRedoInfo"));
  macro_block_id_array_.set_attr(lib::ObMemAttr("DdlMacroIds"));
}

ObDDLRedoLogWriterCallback::~ObDDLRedoLogWriterCallback()
{
  (void)wait();
}

int ObDDLRedoLogWriterCallback::init(ObDDLRedoLogWriterCallbackInitParam &init_param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ddl redo log writer has been inited twice", K(ret));
  } else if (OB_UNLIKELY(!init_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid init param", KR(ret), K(init_param));
  } else if (OB_FAIL(ddl_writer_.init(init_param.tablet_id_))) {
  } else {
    // init kv mgr handle for idempotence check
    ObLSService *ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
    ObLS *ls = nullptr;
    ObTabletHandle tablet_handle;
    if (OB_FAIL(ls_service->get_ls(ls))) {
    } else if (OB_FAIL(ObDDLStorageUtil::ddl_get_tablet(ls, init_param.tablet_id_, tablet_handle))) {
    } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(kv_mgr_handle_, true /*try_create*/))) {
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
  redo_info_array_.reuse();
  macro_block_id_array_.reuse();
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
    LOG_WARN("invalid logic id", K(ret), K(logic_id), K(macro_handle), K_(param_.need_submit_io));
  }

  if (OB_SUCC(ret)) {
    MacroBlockId macro_block_id = macro_handle.get_macro_id();
    redo_info.table_key_ = param_.table_key_;
    redo_info.block_type_ = param_.block_type_;
    redo_info.logic_id_ = logic_id;
    redo_info.start_scn_ = param_.start_scn_;
    redo_info.type_ = param_.direct_load_type_;
    redo_info.data_format_version_ = param_.data_format_version_;
    redo_info.data_buffer_.assign(buf, buf_len);
    if (OB_FAIL(ret)) {
    } else if (nullptr != param_.macro_meta_store_ && OB_FAIL(param_.macro_meta_store_->append(buf, buf_len, macro_handle.get_macro_id()))) {
        LOG_WARN("append macro meta store failed", K(ret), KP(buf), K(buf_len), K(macro_handle.get_macro_id()));
    } else {
    }

    if (OB_SUCC(ret) && nullptr != param_.write_stat_) {
      ATOMIC_AAF(&param_.write_stat_->row_count_, row_count);
    }
    
    if (OB_FAIL(ret)) {
    } else if (param_.need_delay_) {
      char *tmp_buf = nullptr;
      if (OB_ISNULL(tmp_buf = (char*)(allocator_.alloc(buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc buf", K(ret));
      } else if (FALSE_IT(MEMCPY(tmp_buf, buf, buf_len))) {
      } else if (FALSE_IT(redo_info.data_buffer_.assign(tmp_buf, buf_len))) {
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(redo_info_array_.push_back(redo_info))) {
      } else if (OB_FAIL(macro_block_id_array_.push_back(macro_block_id))) {
        redo_info_array_.pop_back();
        LOG_WARN("failed to record macro block id", K(ret), K(macro_block_id));
      } else if (redo_info_array_.count() > 10) {
        /* write some warn info, since redo info array should not be too large*/
        LOG_WARN("too much element in redo log callback", K(redo_info_array_.count()), K(lbt()));
      }
    } else {
      if (OB_FAIL(inner_write(redo_info, macro_block_id))) {
      }
    }
  }
  return ret;
}

int ObDDLRedoLogWriterCallback::inner_write(
    const ObDDLMacroBlockRedoInfo &redo_info,
    const blocksstable::MacroBlockId &macro_block_id)
{
  int ret = OB_SUCCESS;
  if (!redo_info.is_valid() || !macro_block_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(redo_info), K(macro_block_id));
  } else if (OB_FAIL(ddl_writer_.write_macro_block_log(
      redo_info, macro_block_id, param_.task_id_))) {
  }
  return ret;
}

int ObDDLRedoLogWriterCallback::write_redo_info_array()
{
  int ret = OB_SUCCESS;
  if (0 == redo_info_array_.count()) {
  } else if (OB_UNLIKELY(redo_info_array_.count() != macro_block_id_array_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("redo info and macro id count mismatch", K(ret),
              K(redo_info_array_.count()), K(macro_block_id_array_.count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < redo_info_array_.count(); i++) {
      if (OB_FAIL(inner_write(redo_info_array_.at(i), macro_block_id_array_.at(i)))) {
      }
    }
    allocator_.reuse();
    redo_info_array_.reuse();
    macro_block_id_array_.reuse();
  }
  return ret;
}

int ObDDLRedoLogWriterCallback::wait()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogWriterCallback is not inited", K(ret));
  } else if (param_.need_delay_ && OB_FAIL(write_redo_info_array())) {
    LOG_WARN("fail to write redo info to array", K(ret));
  } 
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ddl_writer_.wait_macro_block_log_finish())) {
  }
  if (OB_SUCC(ret) && nullptr != param_.macro_meta_store_) {
    if (OB_FAIL(param_.macro_meta_store_->wait())) {
    }
  }
  return ret;
}
