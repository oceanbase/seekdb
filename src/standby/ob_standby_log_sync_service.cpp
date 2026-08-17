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
#include "share/ob_debug_sync.h"
#include "share/log/palf/log_define.h"
#include "share/rc/ob_server_runtime.h"
#include "standby/ob_standby_grpc.h"
#include "standby/ob_standby_source_util.h"
#include "standby/standby_host.h"
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

int get_local_position(
    palf::LSN &end_lsn,
    share::SCN &end_scn,
    share::SCN &sync_scn)
{
  int ret = OB_SUCCESS;
  logservice::ObLogHandler *log_handler = nullptr;
  logservice::ObLogService *log_service = share::server_service<logservice::ObLogService>();
  end_lsn.reset();
  end_scn.reset();
  sync_scn.reset();
  if (OB_ISNULL(log_service) || OB_ISNULL(log_service->get_log_replay_service())) {
    ret = OB_NOT_INIT;
    LOG_WARN("log replay service is not initialized", KR(ret), KP(log_service));
  } else if (OB_FAIL(get_log_handler(log_handler))) {
  } else if (OB_FAIL(log_handler->get_end_lsn(end_lsn))) {
    LOG_WARN("failed to get local log end lsn", KR(ret));
  } else if (OB_FAIL(log_handler->get_end_scn(end_scn))) {
    LOG_WARN("failed to get local log end scn", KR(ret), K(end_lsn));
  } else if (OB_FAIL(log_service->get_log_replay_service()->get_max_replayed_scn(sync_scn))) {
    LOG_WARN("failed to get local replay progress", KR(ret), K(end_lsn), K(end_scn));
  }
  return ret;
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
    startup_target_scn_(),
    config_(nullptr),
    host_(nullptr)
{
}

int ObStandbyLogSyncService::init(const StandbyConfig &config, IStandbyHost &host)
{
  int ret = OB_SUCCESS;
  config_ = &config;
  host_ = &host;
  if (OB_FAIL(init_())) {
    config_ = nullptr;
    host_ = nullptr;
  }
  return ret;
}

int ObStandbyLogSyncService::start()
{
  return start_();
}

int ObStandbyLogSyncService::stop()
{
  return stop_();
}

int ObStandbyLogSyncService::wait()
{
  return wait_();
}

void ObStandbyLogSyncService::destroy()
{
  destroy_();
}

int ObStandbyLogSyncService::prepare_promotion(
    const bool is_failover,
    share::SCN &target_scn)
{
  return prepare_promotion_(is_failover, target_scn);
}

void ObStandbyLogSyncService::cancel_promotion_preparation()
{
  cancel_promotion_preparation_();
}

int ObStandbyLogSyncService::validate_switch_to_primary(const bool is_failover)
{
  return validate_switch_to_primary_(is_failover);
}

int ObStandbyLogSyncService::prepare_persisted_promotion(
    const share::SCN &target_scn)
{
  return prepare_persisted_promotion_(target_scn);
}

int ObStandbyLogSyncService::set_startup_target_scn(const share::SCN &target_scn)
{
  return set_startup_target_scn_(target_scn);
}

int ObStandbyLogSyncService::wait_startup_replay(
    const std::function<bool()> &is_stopping)
{
  return wait_startup_replay_(is_stopping);
}

int ObStandbyLogSyncService::get_local_progress(
    share::SCN &end_scn,
    share::SCN &sync_scn)
{
  int ret = OB_SUCCESS;
  palf::LSN end_lsn;
  ret = get_local_position(end_lsn, end_scn, sync_scn);
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
  } else if (config_->embedded_mode_) {
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
  } else if (config_->embedded_mode_ || is_scheduled_) {
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
  int ret = OB_SUCCESS;
  bool should_stop = false;
  {
    lib::ObMutexGuard guard(lock_);
    if (OB_FAIL(guard.get_ret())) {
      LOG_WARN("failed to lock standby log sync service", KR(ret));
    } else {
      should_stop = is_scheduled_;
    }
  }
  // ObTimer::stop waits for an in-flight task. The task also takes lock_, so
  // waiting while holding lock_ would deadlock with a concurrent sync round.
  if (OB_SUCC(ret) && should_stop) {
    timer_.stop();
  }
  return ret;
}

int ObStandbyLogSyncService::wait_()
{
  int ret = OB_SUCCESS;
  if (is_inited_ && is_scheduled_ && !config_->embedded_mode_) {
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
    config_ = nullptr;
    host_ = nullptr;
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
  common::ObArenaAllocator allocator("StandbySource");
  common::ObString source;
  int64_t version = 0;
  ret = load_source_snapshot_(allocator, source, version, source_addr);
  return ret;
}

int ObStandbyLogSyncService::load_source_snapshot_(
    common::ObIAllocator &allocator,
    common::ObString &source,
    int64_t &version,
    common::ObAddr &source_addr) const
{
  int ret = OB_SUCCESS;
  source.reset();
  source_addr.reset();
  version = 0;
  if (OB_FAIL(host_->load_log_restore_source(allocator, source, version))) {
    LOG_WARN("failed to load standby log source", KR(ret));
  } else if (source.empty()) {
    ret = OB_ENTRY_NOT_EXIST;
  } else if (OB_FAIL(StandbySourceParser::get_first_service_addr(source, source_addr))) {
    LOG_WARN("failed to parse standby log source", KR(ret), K(source));
  } else if (!source_addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("standby log source is invalid", KR(ret), K(source), K(source_addr));
  }
  return ret;
}

int ObStandbyLogSyncService::query_source_promotion_boundary_(
    const common::ObAddr &source_addr,
    StandbyPromotionBoundary &boundary)
{
  int ret = OB_SUCCESS;
  ObStandbyGrpcClient client;
  StandbyPromotionBoundaryRequest request;
  boundary = StandbyPromotionBoundary();
  if (OB_FAIL(request.add_visited(config_->promotion_node_id_))) {
    LOG_WARN("failed to initialize promotion boundary path",
        KR(ret), K(config_->promotion_node_id_));
  } else if (OB_FAIL(client.init(source_addr, RPC_TIMEOUT_US, config_->rpc_tls_enabled_))) {
    LOG_WARN("failed to init standby grpc client", KR(ret), K(source_addr));
  } else if (OB_FAIL(client.get_promotion_boundary(request, boundary))) {
    LOG_WARN("failed to query source promotion boundary", KR(ret), K(source_addr));
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
  } else if (OB_FAIL(log_handler->get_max_lsn(start_lsn))) {
    // Imported groups advance max_lsn before their asynchronous flush advances
    // end_lsn. Starting the next batch at end_lsn can fetch an already
    // submitted group again and violate PALF's strict continuity check.
    LOG_WARN("failed to get local standby log import position", KR(ret));
  } else if (OB_FAIL(client.init(source_addr, RPC_TIMEOUT_US, config_->rpc_tls_enabled_))) {
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

void ObStandbyLogSyncService::cancel_promotion_preparation_()
{
  lib::ObMutexGuard guard(lock_);
  if (OB_SUCCESS == guard.get_ret() && is_inited_) {
    paused_ = false;
  }
}

int ObStandbyLogSyncService::validate_switch_to_primary_(const bool is_failover)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator source_allocator("PromoteVerify");
  common::ObArenaAllocator recheck_allocator("PromoteVrfy2");
  common::ObString source;
  common::ObString rechecked_source;
  common::ObAddr source_addr;
  common::ObAddr rechecked_addr;
  int64_t source_version = 0;
  int64_t rechecked_version = 0;
  StandbyPromotionBoundary boundary;
  StandbyPromotionBoundary rechecked_boundary;
  share::SCN target_scn;
  share::SCN local_end_scn;
  share::SCN local_sync_scn;
  const int64_t deadline_us = THIS_WORKER.is_timeout_ts_valid()
      ? THIS_WORKER.get_timeout_ts()
      : common::ObTimeUtility::current_time() + config_->operation_timeout_us_;

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

  if (OB_SUCC(ret) && !is_failover
      && OB_FAIL(load_source_snapshot_(
          source_allocator, source, source_version, source_addr))) {
    LOG_WARN("lossless switchover requires a valid log source", KR(ret));
  } else if (OB_SUCC(ret) && !is_failover
             && OB_FAIL(query_source_promotion_boundary_(source_addr, boundary))) {
    LOG_WARN("failed to resolve fenced-primary boundary for switchover validation",
        KR(ret), K(source_addr));
  } else if (OB_SUCC(ret) && !is_failover) {
    target_scn = boundary.cutover_scn_;
  } else if (OB_SUCC(ret)
             && OB_FAIL(get_local_progress(target_scn, local_sync_scn))) {
    LOG_WARN("failed to capture local failover boundary", KR(ret));
  }

  while (OB_SUCC(ret)) {
    if (OB_FAIL(get_local_progress(local_end_scn, local_sync_scn))) {
      LOG_WARN("failed to get local progress during switchover validation", KR(ret));
    } else if ((is_failover || local_end_scn >= target_scn)
               && local_sync_scn >= target_scn) {
      break;
    } else if (common::ObTimeUtility::current_time() >= deadline_us) {
      ret = OB_TIMEOUT;
      LOG_WARN("standby is not ready for promotion", KR(ret), K(is_failover),
          K(source_addr), K(target_scn), K(local_end_scn), K(local_sync_scn));
    } else {
      ob_usleep(SYNC_INTERVAL_US);
    }
  }
  if (OB_SUCC(ret) && !is_failover
      && OB_FAIL(load_source_snapshot_(
          recheck_allocator,
          rechecked_source,
          rechecked_version,
          rechecked_addr))) {
    LOG_WARN("failed to recheck switchover source", KR(ret));
  } else if (OB_SUCC(ret) && !is_failover
             && (source_version != rechecked_version
                 || source.compare(rechecked_source) != 0
                 || source_addr != rechecked_addr)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("standby source changed during switchover validation", KR(ret),
        K(source_version), K(rechecked_version), K(source), K(rechecked_source));
  } else if (OB_SUCC(ret) && !is_failover
             && OB_FAIL(query_source_promotion_boundary_(
                 rechecked_addr, rechecked_boundary))) {
    LOG_WARN("failed to revalidate promotion source chain", KR(ret), K(rechecked_addr));
  } else if (OB_SUCC(ret) && !is_failover
             && !boundary.is_same_as(rechecked_boundary)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("promotion source chain changed during validation",
        KR(ret), K(boundary), K(rechecked_boundary));
  }
  return ret;
}

int ObStandbyLogSyncService::prepare_promotion_(
    const bool is_failover,
    share::SCN &target_scn)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator source_allocator("PromoteSource");
  common::ObArenaAllocator recheck_allocator("PromoteCheck");
  common::ObString source;
  common::ObString rechecked_source;
  common::ObAddr source_addr;
  common::ObAddr rechecked_addr;
  int64_t source_version = 0;
  int64_t rechecked_version = 0;
  StandbyPromotionBoundary boundary;
  StandbyPromotionBoundary rechecked_boundary;
  share::SCN local_end_scn;
  share::SCN local_sync_scn;
  bool paused_by_this_call = false;
  const int64_t deadline_us = THIS_WORKER.is_timeout_ts_valid()
      ? THIS_WORKER.get_timeout_ts()
      : common::ObTimeUtility::current_time() + config_->operation_timeout_us_;
  target_scn.reset();

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

  if (OB_SUCC(ret) && !is_failover
      && OB_FAIL(load_source_snapshot_(
          source_allocator, source, source_version, source_addr))) {
    LOG_WARN("lossless switchover requires a valid log source", KR(ret));
  } else if (OB_SUCC(ret) && !is_failover
             && OB_FAIL(query_source_promotion_boundary_(source_addr, boundary))) {
    LOG_WARN("failed to resolve fenced-primary boundary for lossless switchover",
        KR(ret), K(source_addr));
  } else if (OB_SUCC(ret) && !is_failover) {
    target_scn = boundary.cutover_scn_;
    (void)DEBUG_SYNC(common::AFTER_STANDBY_PROMOTION_BOUNDARY_RESOLVED);
  }

  while (OB_SUCC(ret) && !is_failover) {
    if (OB_FAIL(get_local_progress(local_end_scn, local_sync_scn))) {
      LOG_WARN("failed to read standby promotion progress", KR(ret));
    } else if (local_end_scn >= target_scn && local_sync_scn >= target_scn) {
      break;
    } else if (common::ObTimeUtility::current_time() >= deadline_us) {
      ret = OB_TIMEOUT;
      LOG_WARN("standby did not replay the fenced-primary boundary", KR(ret),
          K(target_scn), K(local_end_scn), K(local_sync_scn));
    } else {
      ob_usleep(SYNC_INTERVAL_US);
    }
  }

  if (OB_SUCC(ret)) {
    lib::ObMutexGuard sync_guard(sync_lock_);
    if (OB_FAIL(sync_guard.get_ret())) {
      LOG_WARN("failed to serialize standby promotion preparation", KR(ret));
    } else if (!is_failover
               && OB_FAIL(load_source_snapshot_(
                   recheck_allocator,
                   rechecked_source,
                   rechecked_version,
                   rechecked_addr))) {
      LOG_WARN("failed to recheck standby source before promotion", KR(ret));
    } else if (!is_failover
               && (source_version != rechecked_version
                   || source.compare(rechecked_source) != 0
                   || source_addr != rechecked_addr)) {
      ret = OB_STATE_NOT_MATCH;
      LOG_WARN("standby source changed before promotion commit", KR(ret),
          K(source_version), K(rechecked_version), K(source), K(rechecked_source));
    } else if (!is_failover
               && OB_FAIL(query_source_promotion_boundary_(
                   rechecked_addr, rechecked_boundary))) {
      LOG_WARN("failed to revalidate promotion source chain before commit",
          KR(ret), K(rechecked_addr));
    } else if (!is_failover
               && !boundary.is_same_as(rechecked_boundary)) {
      ret = OB_STATE_NOT_MATCH;
      LOG_WARN("promotion source chain changed before commit",
          KR(ret), K(boundary), K(rechecked_boundary));
    } else {
      lib::ObMutexGuard guard(lock_);
      if (OB_FAIL(guard.get_ret())) {
        LOG_WARN("failed to pause standby importer", KR(ret));
      } else if (!is_inited_) {
        ret = OB_NOT_INIT;
      } else if (fatal_error_ != OB_SUCCESS) {
        ret = fatal_error_;
      } else {
        paused_ = true;
        paused_by_this_call = true;
      }
    }

    if (OB_SUCC(ret) && OB_FAIL(wait_local_replay_(deadline_us))) {
      LOG_WARN("failed to drain local replay before promotion", KR(ret), K(target_scn));
    } else if (OB_SUCC(ret)
               && OB_FAIL(get_local_progress(local_end_scn, local_sync_scn))) {
      LOG_WARN("failed to capture paused standby progress", KR(ret));
    } else if (OB_SUCC(ret) && !is_failover
               && (local_end_scn < target_scn || local_sync_scn < local_end_scn)) {
      ret = OB_STATE_NOT_MATCH;
      LOG_WARN("standby did not stop at a replayed promotion boundary", KR(ret),
          K(target_scn), K(local_end_scn), K(local_sync_scn));
    } else if (OB_SUCC(ret) && is_failover) {
      target_scn = local_end_scn;
    }
  }

  if (OB_SUCC(ret) && !target_scn.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("captured invalid promotion target", KR(ret), K(is_failover), K(target_scn));
  }
  if (OB_SUCCESS != ret && paused_by_this_call) {
    cancel_promotion_preparation_();
  }
  return ret;
}

int ObStandbyLogSyncService::prepare_persisted_promotion_(
    const share::SCN &target_scn)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard sync_guard(sync_lock_);
  share::SCN local_end_scn;
  share::SCN local_sync_scn;
  const int64_t deadline_us = THIS_WORKER.is_timeout_ts_valid()
      ? THIS_WORKER.get_timeout_ts()
      : common::ObTimeUtility::current_time() + config_->operation_timeout_us_;

  if (!target_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid persisted promotion target", KR(ret), K(target_scn));
  } else if (OB_FAIL(sync_guard.get_ret())) {
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
      paused_ = true;
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(wait_local_replay_(deadline_us))) {
    LOG_WARN("failed to wait local replay before promotion", KR(ret), K(target_scn));
  } else if (OB_SUCC(ret)
             && OB_FAIL(get_local_progress(local_end_scn, local_sync_scn))) {
    LOG_WARN("failed to capture persisted promotion progress", KR(ret));
  } else if (OB_SUCC(ret)
             && (local_end_scn < target_scn || local_sync_scn < local_end_scn)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("standby is not replayed to the persisted promotion boundary", KR(ret),
        K(target_scn), K(local_end_scn), K(local_sync_scn));
  } else if (OB_SUCC(ret)) {
    LOG_INFO("persisted promotion is independent of its source",
        K(target_scn), K(local_end_scn), K(local_sync_scn));
  }
  return ret;
}

void ObStandbyLogSyncService::runTimerTask()
{
  int ret = OB_SUCCESS;
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
  } else if (OB_SUCC(ret) && should_sync
             && OB_FAIL(DEBUG_SYNC(common::BEFORE_STANDBY_LOG_SYNC_FETCH))) {
    LOG_WARN("standby log fetch paused by debug sync", KR(ret), K(source_addr));
  } else if (OB_SUCC(ret) && should_sync && OB_FAIL(sync_once_(source_addr, made_progress))) {
    if (REACH_TIME_INTERVAL(10 * 1000 * 1000L)) {
      LOG_WARN("standby log sync round failed", KR(ret), K(source_addr),
          "fatal_error", get_fatal_error_());
    }
  }
}

} // namespace standby
} // namespace oceanbase
