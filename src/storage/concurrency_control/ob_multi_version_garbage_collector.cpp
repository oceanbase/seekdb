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

#include "storage/concurrency_control/ob_multi_version_garbage_collector.h"
#include "query/ob_i_active_snapshot_service.h"
#include "share/ob_share_util.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tx/ob_trans_service.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/tx/ob_weak_read_util.h"
#include "share/ob_server_struct.h"
#include "share/ob_io_device_helper.h"

namespace oceanbase
{
namespace concurrency_control
{

int64_t ObMultiVersionGarbageCollector::GARBAGE_COLLECT_RETRY_INTERVAL = 1_min;
int64_t ObMultiVersionGarbageCollector::GARBAGE_COLLECT_EXEC_INTERVAL = 10 * GARBAGE_COLLECT_RETRY_INTERVAL;

ObMultiVersionGarbageCollector::ObMultiVersionGarbageCollector()
  : timer_task_(*this),
    timer_(),
    last_study_timestamp_(0),
    last_sstable_overflow_timestamp_(0),
    has_error_when_study_(false),
    gc_is_disabled_(false),
    local_reserved_snapshot_(share::SCN::min_scn()),
    is_inited_(false) {}

ObMultiVersionGarbageCollector::~ObMultiVersionGarbageCollector() {}

int ObMultiVersionGarbageCollector::server_module_init(
    ObMultiVersionGarbageCollector *&m)
{
  return m->init();
}

int ObMultiVersionGarbageCollector::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    MVCC_LOG(WARN, "ObMultiVersionGarbageCollector init twice", K(ret), KP(this));
  } else {
    last_study_timestamp_ = 0;
    last_sstable_overflow_timestamp_ = 0;
    has_error_when_study_ = false;
    gc_is_disabled_ = false;
    local_reserved_snapshot_ = share::SCN::min_scn();
    is_inited_ = true;
    MVCC_LOG(INFO, "multi version garbage collector init", KP(this));
  }
  return ret;
}

void ObMultiVersionGarbageCollector::cure()
{
  last_study_timestamp_ = 0;
  last_sstable_overflow_timestamp_ = 0;
  has_error_when_study_ = false;
  gc_is_disabled_ = false;
  local_reserved_snapshot_ = share::SCN::min_scn();
}

int ObMultiVersionGarbageCollector::start()
{
  int ret = OB_SUCCESS;

  if(!is_inited_) {
    ret = OB_NOT_INIT;
    MVCC_LOG(ERROR, "has not been inited", KR(ret));
  } else if (OB_FAIL(timer_.init("MultiVersionGC", ObMemAttr("MultiVersionGC")))) {
  } else if (OB_FAIL(timer_.schedule(timer_task_,
                                     GARBAGE_COLLECT_RETRY_INTERVAL, true/*repeat*/, false/*immediate*/))) {
  } else {
    MVCC_LOG(INFO, "multi version garbage collector start", KPC(this),
             K(GARBAGE_COLLECT_RETRY_INTERVAL), K(GARBAGE_COLLECT_EXEC_INTERVAL),
             K(local_reserved_snapshot_));
  }

  return ret;
}

int ObMultiVersionGarbageCollector::stop()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    MVCC_LOG(WARN, "ObCheckPointService is not initialized", K(ret));
  } else {
    ObTimeGuard timeguard(__func__, 1 * 1000 * 1000);
    timer_.stop();
    last_study_timestamp_ = 0;
    last_sstable_overflow_timestamp_ = 0;
    has_error_when_study_ = false;
    gc_is_disabled_ = false;
    local_reserved_snapshot_ = share::SCN::min_scn();
    is_inited_ = false;
    MVCC_LOG(INFO, "multi version garbage collector stop", KPC(this));
  }

  return ret;
}

void ObMultiVersionGarbageCollector::wait()
{
  timer_.wait();
  MVCC_LOG(INFO, "multi version garbage collector wait", KPC(this));
}

void ObMultiVersionGarbageCollector::destroy()
{
  timer_.destroy();
  MVCC_LOG(INFO, "multi version garbage collector destroy", KPC(this));
}

void ObMultiVersionGarbageCollector::run_timer_task()
{
   if (!GCONF._mvcc_gc_using_min_txn_snapshot) {
     cure();
   } else {
     (void)repeat_study();
     const int ret = refresh_disk_status_();
     if (OB_SUCCESS != ret) {
       gc_is_disabled_ = true;
       MVCC_LOG(WARN, "refresh local disk status failed, disable mvcc gc optimization",
                K(ret), KPC(this));
     }
   }
}

// Sample every ten minutes, or every minute while the previous sample failed.
void ObMultiVersionGarbageCollector::repeat_study()
{
  int ret = OB_SUCCESS;
  const int64_t current_timestamp = ObClockGenerator::getRealClock();

  if (has_error_when_study_  // enconter error during last study
      // study every 10 min(default of GARBAGE_COLLECT_EXEC_INTERVAL)
      || current_timestamp - last_study_timestamp_ > GARBAGE_COLLECT_EXEC_INTERVAL) {
    if (OB_FAIL(study())) {
      update_study_status_(ret, current_timestamp);
      if (current_timestamp - last_study_timestamp_ > 10 * GARBAGE_COLLECT_EXEC_INTERVAL
          && 0 != last_study_timestamp_
          // for mock or test that change GARBAGE_COLLECT_EXEC_INTERVAL to a small value
          && current_timestamp - last_study_timestamp_ > 10 * 10_min) {
        MVCC_LOG(ERROR, "repeat study failed too much time", K(ret),
                 KPC(this), K(current_timestamp));
      } else {
        MVCC_LOG(WARN, "repeat study failed, we will retry immediately", K(ret),
                 KPC(this), K(current_timestamp));
      }
    } else {
      update_study_status_(ret, current_timestamp);
      MVCC_LOG(INFO, "repeat study successfully", K(ret), KPC(this),
               K(current_timestamp), K(GARBAGE_COLLECT_EXEC_INTERVAL));
    }
  } else {
    MVCC_LOG(INFO, "skip repeat study", K(ret), KPC(this),
             K(current_timestamp), K(GARBAGE_COLLECT_EXEC_INTERVAL));
  }
}

void ObMultiVersionGarbageCollector::update_study_status_(
    const int study_ret,
    const int64_t study_timestamp)
{
  if (OB_SUCCESS == study_ret) {
    has_error_when_study_ = false;
    last_study_timestamp_ = study_timestamp;
  } else {
    // A failed sample is retried on the next timer tick, but it does not
    // invalidate the last complete sample.  Before the first successful
    // sample the cached value is min_scn(), which conservatively keeps all
    // multi-version data.
    has_error_when_study_ = true;
  }
}

// The four values cover transactions that began before this sample and each
// snapshot source available to transactions that begin after it.  Their minimum
// is the local safe watermark; it is published only after every sample succeeds.
int ObMultiVersionGarbageCollector::study()
{
  int ret = OB_SUCCESS;
  share::SCN min_unallocated_GTS(share::SCN::max_scn());
  share::SCN min_unallocated_WRS(share::SCN::max_scn());
  share::SCN max_committed_txn_version(share::SCN::max_scn());
  share::SCN min_active_txn_version(share::SCN::max_scn());

  ObTimeGuard timeguard(__func__, 1 * 1000 * 1000);

  // standby cluster uses the same interface for GTS
  if (OB_FAIL(study_min_unallocated_GTS(min_unallocated_GTS))) {
  } else if (!min_unallocated_GTS.is_valid()
             || min_unallocated_GTS.is_min()
             || min_unallocated_GTS.is_max()) {
    ret = OB_ERR_UNEXPECTED;
    MVCC_LOG(ERROR, "wrong min unallocated GTS",
             K(ret), K(min_unallocated_GTS), KPC(this));
  } else {
    MVCC_LOG(INFO, "study min unallocated gts succeed",
             K(ret), K(min_unallocated_GTS), KPC(this));
  }

  timeguard.click("study_min_unallocated_GTS");

  if (OB_SUCC(ret)) {
    bool write_enabled = false;

    if (OB_FAIL(ObShareUtil::is_server_write_enabled(write_enabled))) {
    } else if (write_enabled && OB_FAIL(study_min_unallocated_WRS(min_unallocated_WRS))) {
      MVCC_LOG(WARN, "study min unallocated GTS failed", K(ret), K(write_enabled));
    } else if (!min_unallocated_WRS.is_valid() || min_unallocated_WRS.is_min()) {
      ret = OB_ERR_UNEXPECTED;
      MVCC_LOG(ERROR, "wrong min unallocated WRS",
               K(ret), K(min_unallocated_WRS), KPC(this), K(write_enabled));
    } else {
      MVCC_LOG(INFO, "study min unallocated wrs succeed",
               K(ret), K(min_unallocated_WRS), KPC(this), K(write_enabled));
    }
  }

  timeguard.click("study_min_unallocated_WRS");

  if (OB_SUCC(ret)) {
    if (OB_FAIL(study_max_committed_txn_version(max_committed_txn_version))) {
    } else if (!max_committed_txn_version.is_valid()
               || max_committed_txn_version.is_max()) {
      ret = OB_ERR_UNEXPECTED;
      MVCC_LOG(ERROR, "wrong max committed txn version",
               K(ret), K(max_committed_txn_version), KPC(this));
    } else {
      MVCC_LOG(INFO, "study max committed txn version succeed",
               K(ret), K(max_committed_txn_version), KPC(this));
    }
  }

  timeguard.click("study_max_commited_txn_version");

  if (OB_SUCC(ret)) {
    if (OB_FAIL(study_min_active_txn_version(min_active_txn_version))) {
    } else {
      MVCC_LOG(INFO, "study min active txn version succeed",
               K(ret), K(min_active_txn_version), KPC(this));
    }
  }

  timeguard.click("study_min_active_txn_version");

  if (OB_SUCC(ret)) {
    share::SCN reserved_snapshot = min_unallocated_GTS;
    if (min_unallocated_WRS < reserved_snapshot) {
      reserved_snapshot = min_unallocated_WRS;
    }
    if (max_committed_txn_version < reserved_snapshot) {
      reserved_snapshot = max_committed_txn_version;
    }
    if (min_active_txn_version < reserved_snapshot) {
      reserved_snapshot = min_active_txn_version;
    }
    if (OB_UNLIKELY(!reserved_snapshot.is_valid() || reserved_snapshot.is_max())) {
      ret = OB_ERR_UNEXPECTED;
      MVCC_LOG(ERROR, "invalid locally calculated reserved snapshot",
               K(ret), K(reserved_snapshot));
    } else {
      // Publish only after all four samples succeed.  Readers therefore see
      // either the previous complete sample or this complete sample, never a
      // partially refreshed set of watermarks.
      local_reserved_snapshot_.atomic_set(reserved_snapshot);
      MVCC_LOG(INFO, "publish local reserved snapshot", K(reserved_snapshot));
    }
  }

  timeguard.click("publish");

  MVCC_LOG(INFO, "study multi version garabage collector end",
           K(ret), KPC(this), K(min_unallocated_GTS), K(min_unallocated_WRS),
           K(max_committed_txn_version), K(min_active_txn_version));

  return ret;
}

// The read snapshot version may base on GTS for most txns, so we need study it on each machine.
int ObMultiVersionGarbageCollector::study_min_unallocated_GTS(share::SCN &min_unallocated_GTS)
{
  int ret = OB_SUCCESS;

  const int64_t timeout_us = 1 * 1000 * 1000; // 1s
  share::SCN gts_scn;

  if (OB_FAIL(OB_TS_MGR.get_gts_sync(timeout_us, gts_scn))) {
  } else if (!gts_scn.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    MVCC_LOG(ERROR, "get gts fail", K(gts_scn), K(ret));
  } else {
    min_unallocated_GTS = gts_scn;
  }

  return ret;
}

// The read snapshot version may base on WRS for the boundary weak read txn, so we
// need study it on each machine.
int ObMultiVersionGarbageCollector::study_min_unallocated_WRS(
  share::SCN &min_unallocated_WRS)
{
  int ret = OB_SUCCESS;

  const int64_t current_time = ObTimeUtility::current_time();
  const int64_t max_read_stale_time =
    transaction::ObWeakReadUtil::max_stale_time_for_weak_consistency();

  transaction::ObTransService *trans_service =
      ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>();
  if (OB_ISNULL(trans_service)) {
    ret = OB_NOT_INIT;
    MVCC_LOG(WARN, "transaction service is nullptr", K(ret));
  } else if (OB_FAIL(trans_service->get_weak_read_snapshot_version(
                -1, // system variable : max read stale time for user
                min_unallocated_WRS))) {
    MVCC_LOG(WARN, "fail to get weak read snapshot", K(ret));
    if (OB_REPLICA_NOT_READABLE == ret) {
      // The global weak read service cannot provide services in some cases(for
      // example backup cluster's weak read service may hung during recovery).
      // So instead of report the error, we decide to use the max allowed stale
      // time for garbage collector.
      min_unallocated_WRS.convert_from_ts(current_time - max_read_stale_time);
      ret = OB_SUCCESS;
    }
  }

  return ret;
}

// The read snapshot version may be based on the maximum committed transaction version.
int ObMultiVersionGarbageCollector::study_max_committed_txn_version(
  share::SCN &max_committed_txn_version)
{
  int ret = OB_SUCCESS;

  transaction::ObTransService *trans_service =
      ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>();
  if (OB_ISNULL(trans_service)) {
    ret = OB_NOT_INIT;
    MVCC_LOG(WARN, "transaction service is nullptr", K(ret));
  } else {
    max_committed_txn_version =
        trans_service->get_tx_version_mgr().get_max_commit_ts(false /*elr*/);
  }

  if (OB_SUCC(ret) && max_committed_txn_version.is_base_scn()) {
    // if the max committed txn version is base_scn(not updated by any txns and
    // async loop worker), we need ignore it and retry the next time
    ret = OB_EAGAIN;
    MVCC_LOG(WARN, "get max committed txn version is base version",
             K(ret), K(max_committed_txn_version));
  }

  return ret;
}

// We need collect all active txns, so decide to collect all snapshot version on
// one machine through tranversing the sessions. Lets' show all possibilities of
// the txns:
// 1. RR/SI, AC=0 txn: it will create the session with tx_desc on the scheduler
//      and record the snapshot_version on it. We can directly use it.
// 2. RC, AC=0 txn: it will create the session with tx_desc on the scheduler
//      while not recording the snapshot_version. We currently use session_state
//      and query_start_ts to act as the alive stmt snapshot.
//      TODO(handora.qc): record the snapshot version to tx_desc in the feture.
// 3. AC=1 txn: it may contain no tx_desc on session, while it must create session.
//      Even for remote execution, it will create session on the execution machine.
int ObMultiVersionGarbageCollector::study_min_active_txn_version(
  share::SCN &min_active_txn_version)
{
  int ret = OB_SUCCESS;
  query::ObIActiveSnapshotService *snapshot_service =
      ::oceanbase::share::server_service<::oceanbase::query::ObIActiveSnapshotService>();
  if (OB_ISNULL(snapshot_service)) {
    ret = OB_INVALID_ARGUMENT;
    MVCC_LOG(WARN, "active snapshot service is nullptr");
  } else if (OB_FAIL(snapshot_service->get_min_active_snapshot_version(min_active_txn_version))) {
  }

  return ret;
}

int ObMultiVersionGarbageCollector::refresh_disk_status_()
{
  int ret = OB_SUCCESS;
  bool is_almost_full = false;
  if (OB_FAIL(is_disk_almost_full_(is_almost_full))) {
  } else {
    update_disk_pressure_status_(is_almost_full);
  }
  return ret;
}

void ObMultiVersionGarbageCollector::update_disk_pressure_status_(
    const bool is_almost_full)
{
  const int ret = OB_SUCCESS;
  if (is_almost_full && !gc_is_disabled_) {
    MVCC_LOG(WARN, "local mvcc gc disabled by disk pressure", KPC(this));
  } else if (!is_almost_full && gc_is_disabled_) {
    MVCC_LOG(INFO, "local mvcc gc re-enabled after disk pressure cleared", KPC(this));
  }
  gc_is_disabled_ = is_almost_full;
}

share::SCN ObMultiVersionGarbageCollector::get_reserved_snapshot_for_active_txn() const
{
  return get_reserved_snapshot_for_active_txn_(
      GCONF._mvcc_gc_using_min_txn_snapshot);
}

share::SCN ObMultiVersionGarbageCollector::get_reserved_snapshot_for_active_txn_(
    const bool config_enabled) const
{
  if (!config_enabled) {
    return share::SCN::max_scn();
  } else if (gc_is_disabled_) {
    if (REACH_THREAD_TIME_INTERVAL(1_s)) {
      MVCC_LOG_RET(WARN, OB_ERR_UNEXPECTED, "get reserved snapshot for active txn with gc is disabled", KPC(this));
    }
    return share::SCN::max_scn();
  } else {
    return local_reserved_snapshot_.atomic_load();
  }
}

bool ObMultiVersionGarbageCollector::is_gc_disabled() const
{
  return gc_is_disabled_;
}

// Disable the local active-transaction watermark optimization while disk
// pressure or an sstable overflow is present.
int ObMultiVersionGarbageCollector::is_disk_almost_full_(bool &is_almost_full)
{
  int ret = OB_SUCCESS;
  is_almost_full = false;
  const int64_t required_size = 0;

  // Case1: io device is almost full
  if (!is_almost_full
      && OB_FAIL(LOCAL_DEVICE_INSTANCE.check_space_full(required_size))) {
    if (OB_SERVER_OUTOF_DISK_SPACE == ret) {
      ret = OB_SUCCESS;
      is_almost_full = true;
      MVCC_LOG(WARN, "disk is almost full, we should give up", KPC(this));
    } else {
      MVCC_LOG(WARN, "failed to check space full", K(ret));
    }
  }

  // Case2: sstable is overflow during merge
  if (!is_almost_full
      && is_sstable_overflow_()) {
    is_almost_full = true;
    MVCC_LOG(WARN, "disk is almost full, we should give up", KPC(this));
  }

  return ret;
}

void ObMultiVersionGarbageCollector::report_sstable_overflow()
{
  const int64_t current_timestamp = common::ObTimeUtility::current_time();
  ATOMIC_STORE(&last_sstable_overflow_timestamp_, current_timestamp);
  MVCC_LOG_RET(WARN, OB_SIZE_OVERFLOW, "sstable is alomost overflow, we should give up", KPC(this));
}

bool ObMultiVersionGarbageCollector::is_sstable_overflow_()
{
  bool b_ret = false;
  const int64_t current_timestamp = common::ObTimeUtility::current_time();
  const int64_t last_sstable_overflow_timestamp = ATOMIC_LOAD(&last_sstable_overflow_timestamp_);
  if (0 != last_sstable_overflow_timestamp
      && current_timestamp >= last_sstable_overflow_timestamp
      // We currenly think that there may be a disk full problem if there exists
      // an sstable overflow error within 5 minutes
      && current_timestamp - last_sstable_overflow_timestamp <= 5 * 1_min) {
    b_ret = true;
  }
  return b_ret;
}

} // namespace concurrency_control
} // namespace oceanbase
