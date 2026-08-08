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

#include "standby/ob_standby_log_sync_service.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log.h"
#include "lib/time/ob_time_utility.h"
#include "lib/worker.h"
#include "logservice/ob_log_handler.h"
#include "logservice/ob_log_service.h"
#include "logservice/replayservice/ob_log_replay_service.h"
#include "share/config/ob_server_config.h"
#include "share/log/palf/log_define.h"
#include "share/ob_server_struct.h"
#include "share/ob_standby_source_util.h"
#include "share/rc/ob_server_runtime.h"
#include "standby/ob_standby_grpc.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace standby
{
namespace
{

int get_log_handler(logservice::ObLogHandler *&log_handler)
{
  int ret = OB_SUCCESS;
  storage::ObLSService *ls_service = share::server_service<storage::ObLSService>();
  storage::ObLS *ls = nullptr;
  log_handler = nullptr;
  if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service is null", KR(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("failed to get local log stream", KR(ret));
  } else if (OB_ISNULL(ls) || OB_ISNULL(log_handler = ls->get_log_handler())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("local log stream or handler is null", KR(ret), KP(ls), KP(log_handler));
  }
  return ret;
}

bool is_fatal_sync_error(const int ret)
{
  return OB_ENTRY_NOT_EXIST == ret
      || OB_ERR_OUT_OF_LOWER_BOUND == ret
      || OB_ERR_OUT_OF_UPPER_BOUND == ret
      || OB_STATE_NOT_MATCH == ret
      || OB_CHECKSUM_ERROR == ret
      || OB_INVALID_DATA == ret;
}

} // namespace

ObStandbyLogSyncService::ObStandbyLogSyncService()
  : timer_(),
    lock_(),
    sync_lock_(),
    is_inited_(false),
    is_scheduled_(false),
    paused_(false),
    fatal_error_(OB_SUCCESS),
    startup_target_scn_()
{
}

ObStandbyLogSyncService &ObStandbyLogSyncService::instance_()
{
  static ObStandbyLogSyncService service;
  return service;
}

int ObStandbyLogSyncService::init()
{
  return instance_().init_();
}

int ObStandbyLogSyncService::start()
{
  return instance_().start_();
}

int ObStandbyLogSyncService::stop()
{
  return instance_().stop_();
}

int ObStandbyLogSyncService::wait()
{
  return instance_().wait_();
}

void ObStandbyLogSyncService::destroy()
{
  instance_().destroy_();
}

int ObStandbyLogSyncService::prepare_switch_to_primary(const bool is_failover)
{
  return instance_().prepare_switch_to_primary_(is_failover);
}

int ObStandbyLogSyncService::validate_switch_to_primary(const bool is_failover)
{
  return instance_().validate_switch_to_primary_(is_failover);
}

int ObStandbyLogSyncService::pause()
{
  return instance_().pause_();
}

int ObStandbyLogSyncService::resume()
{
  return instance_().resume_();
}

int ObStandbyLogSyncService::set_startup_target_scn(const share::SCN &target_scn)
{
  return instance_().set_startup_target_scn_(target_scn);
}

int ObStandbyLogSyncService::wait_startup_replay(
    const std::function<bool()> &is_stopping)
{
  return instance_().wait_startup_replay_(is_stopping);
}

int ObStandbyLogSyncService::get_local_progress(
    share::SCN &end_scn,
    share::SCN &sync_scn)
{
  int ret = OB_SUCCESS;
  logservice::ObLogHandler *log_handler = nullptr;
  logservice::ObLogService *log_service = share::server_service<logservice::ObLogService>();
  end_scn.reset();
  sync_scn.reset();
  if (OB_ISNULL(log_service) || OB_ISNULL(log_service->get_log_replay_service())) {
    ret = OB_NOT_INIT;
    LOG_WARN("log replay service is not initialized", KR(ret), KP(log_service));
  } else if (OB_FAIL(get_log_handler(log_handler))) {
  } else if (OB_FAIL(log_handler->get_end_scn(end_scn))) {
    LOG_WARN("failed to get local log end scn", KR(ret));
  } else if (OB_FAIL(log_service->get_log_replay_service()->get_max_replayed_scn(sync_scn))) {
    LOG_WARN("failed to get local replay progress", KR(ret), K(end_scn));
  }
  return ret;
}

int ObStandbyLogSyncService::init_()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(lock_);
  if (OB_FAIL(guard.get_ret())) {
    LOG_WARN("failed to lock standby log sync service", KR(ret));
  } else if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (GCTX.is_embedded_mode()) {
    is_inited_ = true;
  } else if (OB_FAIL(timer_.init("StbyLogSync", common::ObMemAttr("StbyLogSync")))) {
    LOG_WARN("failed to init standby log sync timer", KR(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObStandbyLogSyncService::start_()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(lock_);
  if (OB_FAIL(guard.get_ret())) {
    LOG_WARN("failed to lock standby log sync service", KR(ret));
  } else if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (GCTX.is_embedded_mode() || is_scheduled_) {
  } else if (OB_FAIL(timer_.schedule(*this, SYNC_INTERVAL_US, true /*repeat*/, true /*immediate*/))) {
    LOG_WARN("failed to schedule standby log sync task", KR(ret));
  } else {
    is_scheduled_ = true;
    LOG_INFO("standby log sync service started");
  }
  return ret;
}

int ObStandbyLogSyncService::stop_()
{
  lib::ObMutexGuard guard(lock_);
  const int ret = guard.get_ret();
  if (OB_SUCCESS == ret && is_scheduled_) {
    timer_.stop();
  }
  return ret;
}

int ObStandbyLogSyncService::wait_()
{
  int ret = OB_SUCCESS;
  if (is_inited_ && is_scheduled_ && !GCTX.is_embedded_mode()) {
    timer_.wait();
    lib::ObMutexGuard guard(lock_);
    if (OB_FAIL(guard.get_ret())) {
      LOG_WARN("failed to lock standby log sync service", KR(ret));
    } else {
      is_scheduled_ = false;
    }
  }
  return ret;
}

void ObStandbyLogSyncService::destroy_()
{
  stop_();
  wait_();
  timer_.destroy();
  lib::ObMutexGuard guard(lock_);
  if (OB_SUCCESS == guard.get_ret()) {
    is_inited_ = false;
    is_scheduled_ = false;
    paused_ = false;
    fatal_error_ = OB_SUCCESS;
    startup_target_scn_.reset();
  }
}

int ObStandbyLogSyncService::get_fatal_error_() const
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(lock_);
  if (OB_SUCCESS == guard.get_ret()) {
    ret = fatal_error_;
  } else {
    ret = guard.get_ret();
  }
  return ret;
}

void ObStandbyLogSyncService::record_fatal_error_(const int error)
{
  lib::ObMutexGuard guard(lock_);
  if (OB_SUCCESS == guard.get_ret() && OB_SUCCESS == fatal_error_) {
    fatal_error_ = error;
  }
}

int ObStandbyLogSyncService::set_startup_target_scn_(const share::SCN &target_scn)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(lock_);
  if (OB_FAIL(guard.get_ret())) {
    LOG_WARN("failed to lock standby log sync service", KR(ret));
  } else if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!target_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid standby startup target", KR(ret), K(target_scn));
  } else {
    startup_target_scn_ = target_scn;
    LOG_INFO("standby startup replay target captured", K_(startup_target_scn));
  }
  return ret;
}

int ObStandbyLogSyncService::wait_startup_replay_(
    const std::function<bool()> &is_stopping)
{
  int ret = OB_SUCCESS;
  share::SCN target_scn;
  share::SCN end_scn;
  share::SCN sync_scn;
  if (!is_stopping) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("startup replay wait requires a stop predicate", KR(ret));
  } else {
    lib::ObMutexGuard guard(lock_);
    if (OB_FAIL(guard.get_ret())) {
      LOG_WARN("failed to lock standby log sync service", KR(ret));
    } else if (!is_inited_ || !is_scheduled_) {
      ret = OB_NOT_INIT;
    } else if (fatal_error_ != OB_SUCCESS) {
      ret = fatal_error_;
    } else {
      target_scn = startup_target_scn_;
    }
  }

  if (OB_SUCC(ret) && !target_scn.is_valid()) {
    if (OB_FAIL(get_local_progress(target_scn, sync_scn))) {
      LOG_WARN("failed to capture restart replay target", KR(ret));
    }
  }

  while (OB_SUCC(ret) && !is_stopping()) {
    if (OB_SUCCESS != (ret = get_fatal_error_())) {
      LOG_WARN("standby log import failed during startup replay", KR(ret));
    } else if (OB_FAIL(get_local_progress(end_scn, sync_scn))) {
      LOG_WARN("failed to get standby startup replay progress", KR(ret));
    } else if (sync_scn >= target_scn) {
      LOG_INFO("standby startup replay is ready", K(target_scn), K(end_scn), K(sync_scn));
      break;
    } else {
      ob_usleep(SYNC_INTERVAL_US);
    }
  }
  if (OB_SUCC(ret) && is_stopping()) {
    LOG_INFO("standby startup replay wait stopped", K(target_scn), K(end_scn), K(sync_scn));
  }
  return ret;
}

int ObStandbyLogSyncService::get_source_addr_(common::ObAddr &source_addr) const
{
  int ret = OB_SUCCESS;
  source_addr.reset();
  const common::ObString source = GCONF.log_restore_source.str();
  if (source.empty()) {
    ret = OB_ENTRY_NOT_EXIST;
  } else if (OB_FAIL(share::ObStandbySourceUtil::get_first_service_addr(source, source_addr))) {
    LOG_WARN("failed to parse standby log source", KR(ret), K(source));
  } else if (!source_addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("standby log source is invalid", KR(ret), K(source), K(source_addr));
  }
  return ret;
}

int ObStandbyLogSyncService::query_source_end_scn_(
    const common::ObAddr &source_addr,
    share::SCN &end_scn)
{
  int ret = OB_SUCCESS;
  ObStandbyGrpcClient client;
  end_scn.reset();
  if (OB_FAIL(client.init(source_addr, RPC_TIMEOUT_US))) {
    LOG_WARN("failed to init standby grpc client", KR(ret), K(source_addr));
  } else if (OB_FAIL(client.get_log_end_scn(end_scn))) {
    LOG_WARN("failed to query source log end scn", KR(ret), K(source_addr));
  } else if (!end_scn.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("source returned invalid log end scn", KR(ret), K(source_addr), K(end_scn));
  }
  return ret;
}

int ObStandbyLogSyncService::append_log_group_(
    const char *buf,
    const int64_t size,
    const palf::LSN &source_lsn,
    const share::SCN &source_scn)
{
  int ret = OB_SUCCESS;
  logservice::ObLogHandler *log_handler = nullptr;
  if (OB_ISNULL(buf) || size <= 0 || size > palf::MAX_LOG_BUFFER_SIZE
      || !source_lsn.is_valid() || !source_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid standby log group", KR(ret), KP(buf), K(size), K(source_lsn), K(source_scn));
  } else if (OB_FAIL(get_log_handler(log_handler))) {
  } else if (OB_FAIL(log_handler->append_imported_group(
      source_lsn, source_scn, buf, size))) {
    if (OB_STATE_NOT_MATCH == ret || OB_CHECKSUM_ERROR == ret || OB_INVALID_DATA == ret) {
      record_fatal_error_(ret);
    }
    LOG_WARN("failed to append source log group locally", KR(ret), K(source_lsn), K(source_scn), K(size));
  }
  return ret;
}

int ObStandbyLogSyncService::sync_once_(
    const common::ObAddr &source_addr,
    bool &made_progress)
{
  int ret = OB_SUCCESS;
  palf::LSN start_lsn;
  logservice::ObLogHandler *log_handler = nullptr;
  ObStandbyGrpcClient client;
  made_progress = false;
  if (OB_SUCCESS != (ret = get_fatal_error_())) {
  } else if (OB_FAIL(get_log_handler(log_handler))) {
  } else if (OB_FAIL(log_handler->get_end_lsn(start_lsn))) {
    LOG_WARN("failed to get local standby log end lsn", KR(ret));
  } else if (OB_FAIL(client.init(source_addr, RPC_TIMEOUT_US))) {
    LOG_WARN("failed to init standby grpc client", KR(ret), K(source_addr));
  } else if (OB_FAIL(client.fetch_log(
      start_lsn,
      FETCH_BATCH_BYTES,
      [this, &made_progress](const char *buf, const int64_t size,
                            const palf::LSN &source_lsn,
                            const share::SCN &source_scn) -> int {
        int ret = append_log_group_(buf, size, source_lsn, source_scn);
        if (OB_SUCC(ret)) {
          made_progress = true;
        }
        return ret;
      }))) {
    if (is_fatal_sync_error(ret)) {
      record_fatal_error_(ret);
    }
    LOG_WARN("failed to fetch standby logs", KR(ret), K(source_addr), K(start_lsn));
  }
  return ret;
}

int ObStandbyLogSyncService::wait_local_replay_(const int64_t deadline_us)
{
  int ret = OB_SUCCESS;
  share::SCN end_scn;
  share::SCN sync_scn;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(get_local_progress(end_scn, sync_scn))) {
      LOG_WARN("failed to get local standby replay progress", KR(ret));
    } else if (!end_scn.is_valid() || !sync_scn.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid local standby replay progress", KR(ret), K(end_scn), K(sync_scn));
    } else if (sync_scn >= end_scn) {
      break;
    } else if (common::ObTimeUtility::current_time() >= deadline_us) {
      ret = OB_TIMEOUT;
      LOG_WARN("wait standby replay catch up timed out", KR(ret), K(end_scn), K(sync_scn));
    } else {
      ob_usleep(SYNC_INTERVAL_US);
    }
  }
  return ret;
}

int ObStandbyLogSyncService::validate_switch_to_primary_(const bool is_failover)
{
  int ret = OB_SUCCESS;
  common::ObAddr source_addr;
  share::SCN target_scn;
  share::SCN local_end_scn;
  share::SCN local_sync_scn;
  const int64_t deadline_us = THIS_WORKER.is_timeout_ts_valid()
      ? THIS_WORKER.get_timeout_ts()
      : common::ObTimeUtility::current_time() + GCONF.internal_sql_execute_timeout;

  {
    lib::ObMutexGuard guard(lock_);
    if (OB_FAIL(guard.get_ret())) {
      LOG_WARN("failed to lock standby log sync service", KR(ret));
    } else if (!is_inited_) {
      ret = OB_NOT_INIT;
    } else if (fatal_error_ != OB_SUCCESS) {
      ret = fatal_error_;
    }
  }

  if (OB_SUCC(ret) && !is_failover && OB_FAIL(get_source_addr_(source_addr))) {
    LOG_WARN("lossless switchover requires a valid log source", KR(ret));
  } else if (OB_SUCC(ret) && !is_failover
             && OB_FAIL(query_source_end_scn_(source_addr, target_scn))) {
    LOG_WARN("failed to capture source end scn for switchover validation",
        KR(ret), K(source_addr));
  }

  while (OB_SUCC(ret)) {
    if (OB_FAIL(get_local_progress(local_end_scn, local_sync_scn))) {
      LOG_WARN("failed to get local progress during switchover validation", KR(ret));
    } else if ((is_failover || local_end_scn >= target_scn)
               && local_sync_scn >= local_end_scn) {
      break;
    } else if (common::ObTimeUtility::current_time() >= deadline_us) {
      ret = OB_TIMEOUT;
      LOG_WARN("standby is not ready for promotion", KR(ret), K(is_failover),
          K(source_addr), K(target_scn), K(local_end_scn), K(local_sync_scn));
    } else {
      ob_usleep(SYNC_INTERVAL_US);
    }
  }
  return ret;
}

int ObStandbyLogSyncService::prepare_switch_to_primary_(const bool is_failover)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard sync_guard(sync_lock_);
  common::ObAddr source_addr;
  share::SCN target_scn;
  share::SCN local_end_scn;
  share::SCN local_sync_scn;
  bool was_paused = false;
  const int64_t deadline_us = THIS_WORKER.is_timeout_ts_valid()
      ? THIS_WORKER.get_timeout_ts()
      : common::ObTimeUtility::current_time() + GCONF.internal_sql_execute_timeout;

  if (OB_FAIL(sync_guard.get_ret())) {
    LOG_WARN("failed to serialize standby log sync", KR(ret));
  } else {
    lib::ObMutexGuard guard(lock_);
    if (OB_FAIL(guard.get_ret())) {
      LOG_WARN("failed to lock standby log sync service", KR(ret));
    } else if (!is_inited_) {
      ret = OB_NOT_INIT;
    } else if (fatal_error_ != OB_SUCCESS) {
      ret = fatal_error_;
    } else {
      was_paused = paused_;
    }
  }

  if (OB_SUCC(ret) && !is_failover && OB_FAIL(get_source_addr_(source_addr))) {
    LOG_WARN("lossless switchover requires a valid log source", KR(ret));
  } else if (OB_SUCC(ret) && !is_failover
             && OB_FAIL(query_source_end_scn_(source_addr, target_scn))) {
    LOG_WARN("failed to capture source end scn for lossless switchover", KR(ret), K(source_addr));
  }

  while (OB_SUCC(ret) && !is_failover) {
    bool made_progress = false;
    if (OB_FAIL(get_local_progress(local_end_scn, local_sync_scn))) {
      LOG_WARN("failed to get local progress during switchover", KR(ret));
    } else if (local_end_scn >= target_scn) {
      break;
    } else if (common::ObTimeUtility::current_time() >= deadline_us) {
      ret = OB_TIMEOUT;
      LOG_WARN("standby did not reach captured source end scn", KR(ret),
          K(source_addr), K(target_scn), K(local_end_scn), K(local_sync_scn));
    } else if (OB_FAIL(sync_once_(source_addr, made_progress))) {
      if (OB_EAGAIN == ret) {
        ret = OB_SUCCESS;
        ob_usleep(SYNC_INTERVAL_US);
      } else {
        LOG_WARN("failed to import logs during lossless switchover", KR(ret), K(source_addr), K(target_scn));
      }
    } else if (!made_progress) {
      ob_usleep(SYNC_INTERVAL_US);
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(wait_local_replay_(deadline_us))) {
    LOG_WARN("failed to wait local replay before promotion", KR(ret), K(is_failover), K(target_scn));
  }

  {
    lib::ObMutexGuard guard(lock_);
    const int lock_ret = guard.get_ret();
    if (OB_SUCCESS != lock_ret) {
      if (OB_SUCC(ret)) {
        ret = lock_ret;
      }
      LOG_WARN("failed to lock standby log sync service", K(ret), K(lock_ret));
    } else if (OB_SUCC(ret)) {
      paused_ = true;
      LOG_INFO("standby log import paused for promotion", K(is_failover), K(target_scn));
    } else if (fatal_error_ == OB_SUCCESS) {
      paused_ = was_paused;
    }
  }
  return ret;
}

int ObStandbyLogSyncService::pause_()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(lock_);
  if (OB_FAIL(guard.get_ret())) {
    LOG_WARN("failed to lock standby log sync service", KR(ret));
  } else if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    paused_ = true;
    LOG_INFO("standby log import paused");
  }
  return ret;
}

int ObStandbyLogSyncService::resume_()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(lock_);
  if (OB_FAIL(guard.get_ret())) {
    LOG_WARN("failed to lock standby log sync service", KR(ret));
  } else if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (fatal_error_ != OB_SUCCESS) {
    ret = fatal_error_;
  } else {
    paused_ = false;
    LOG_INFO("standby log import resumed");
  }
  return ret;
}

void ObStandbyLogSyncService::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (!GCTX.is_standby_server()) {
    return;
  }

  lib::ObMutexGuard sync_guard(sync_lock_);
  common::ObAddr source_addr;
  bool made_progress = false;
  bool should_sync = false;
  if (OB_FAIL(sync_guard.get_ret())) {
    LOG_WARN("failed to serialize standby log sync", KR(ret));
  } else {
    lib::ObMutexGuard guard(lock_);
    if (OB_FAIL(guard.get_ret())) {
      LOG_WARN("failed to lock standby log sync service", KR(ret));
    } else {
      should_sync = is_inited_ && !paused_ && fatal_error_ == OB_SUCCESS;
    }
  }

  if (OB_SUCC(ret) && should_sync && OB_FAIL(get_source_addr_(source_addr))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else if (REACH_TIME_INTERVAL(10 * 1000 * 1000L)) {
      LOG_WARN("standby log source is not usable", KR(ret));
    }
  } else if (OB_SUCC(ret) && should_sync && OB_FAIL(sync_once_(source_addr, made_progress))) {
    if (REACH_TIME_INTERVAL(10 * 1000 * 1000L)) {
      LOG_WARN("standby log sync round failed", KR(ret), K(source_addr),
          "fatal_error", get_fatal_error_());
    }
  }
}

} // namespace standby
} // namespace oceanbase
