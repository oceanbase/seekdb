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

#ifndef OCEANBASE_STORAGE_CONCURRENCY_CONTROL_OB_MULTI_VERSION_GARBAGE_COLLECTOR
#define OCEANBASE_STORAGE_CONCURRENCY_CONTROL_OB_MULTI_VERSION_GARBAGE_COLLECTOR

#include "share/scn.h"
#include "share/ob_occam_timer.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/session/ob_sql_session_mgr.h"

namespace oceanbase
{
namespace concurrency_control
{

class GetMinActiveSnapshotVersionFunctor
{
public:
  GetMinActiveSnapshotVersionFunctor()
    : min_active_snapshot_version_(share::SCN::max_scn()) {}
  virtual ~GetMinActiveSnapshotVersionFunctor() {}
  bool operator()(sql::ObSQLSessionMgr::Key key, sql::ObSQLSessionInfo *sess_info);
  share::SCN get_min_active_snapshot_version()
    { return min_active_snapshot_version_; }
private:
  share::SCN min_active_snapshot_version_;
};

// OceanBase 4.0 reclaims multi-version data through a globally incremented
// timestamp. It's the multi-version data that is less than the specified
// timestamp except for the latest version can be safely recycled. However, the
// setting of this timestamp is worth deliberating: If this timestamp is too
// small, the snapshot of long-running txns will be lost; if the timestamp is
// too large, a large amount of the multi-version data can not be recycled,
// affecting query efficiency and disk utilization.
//
// SeekDB is a single-node product, so ObMultiVersionGarbageCollector detects
// the minimum active-transaction snapshot in this process and uses it as the
// recyclable timestamp.  A multi-observer product must not reuse this local
// watermark without adding cluster-wide aggregation.
//
// In the future, we will expand to further implementations that bring more
// benefiets based on different competing products and papers, including but not
// limited to optimizing the reclaimation with only some range of the
// multi-version data; and optimizing based on table-level visibility, etc.
//
// Above all, we need to achieve the following 3 rules:
// 1. All data that will no longer be read needs to be recycled as soon as possible
// 2. All data that may be read must be preserved
// 3. Guarantee user-understandable recovery in abnormal scenarios
class ObMultiVersionGarbageCollector
{
public:
  ObMultiVersionGarbageCollector();
  ~ObMultiVersionGarbageCollector();
  static int server_module_init(ObMultiVersionGarbageCollector *&p_garbage_colloector);
public:
  int init();
  int start();
  int stop();
  void wait();
  void destroy();
  void run_timer_task();
  // cure means treat myself for the injurity. It resets all state just like
  // treat a patient
  void cure();
  // repeat_xxx will repeatably work for its task under task or meet the time
  // requirement. Currently we retry task every 1 minute and rework the task
  // every 10 minute according to unchangable configuration.
  //
  // Sample the four local safety watermarks and atomically publish their
  // minimum.  The sampling cadence stays at ten minutes; the one-minute timer
  // only retries failures and monitors local disk pressure.
  void repeat_study();

  // Fetch the last complete local safety-watermark sample. Before the first
  // successful sample this returns min_scn(). Explicit configuration disable
  // or local disk pressure falls back to max_scn().
  share::SCN get_reserved_snapshot_for_active_txn() const;
  // report_sstable_overflow marks the last sstable's overflow events and we
  // will use it to disable mvcc gc
  void report_sstable_overflow();
  // is_gc_disabled shows the global gc status of whether the gc is disabled
  bool is_gc_disabled() const;

  TO_STRING_KV(KP(this),
               K_(last_study_timestamp),
               K_(last_sstable_overflow_timestamp),
               K_(has_error_when_study),
               K_(gc_is_disabled),
               K_(local_reserved_snapshot),
               K_(is_inited));

public:
  static int64_t GARBAGE_COLLECT_RETRY_INTERVAL;
  static int64_t GARBAGE_COLLECT_EXEC_INTERVAL;
private:
  int study();
  int refresh_disk_status_();
  void update_study_status_(const int study_ret, const int64_t study_timestamp);
  void update_disk_pressure_status_(const bool is_almost_full);
  int study_min_unallocated_GTS(share::SCN &min_unallocated_GTS);
  int study_min_unallocated_WRS(share::SCN &min_unallocated_WRS);
  int study_max_committed_txn_version(share::SCN &max_committed_txn_version);
  int study_min_active_txn_version(share::SCN &min_active_txn_version);
  int is_disk_almost_full_(bool &is_almost_full);
  bool is_sstable_overflow_();

private:
  class TimerTask : public common::ObTimerTask
  {
  public:
    TimerTask(ObMultiVersionGarbageCollector &collector) : collector_(collector)
    {}

    virtual ~TimerTask() = default;

    void runTimerTask() override { collector_.run_timer_task(); }
  private:
    ObMultiVersionGarbageCollector &collector_;
  };
private:
  TimerTask timer_task_;
  common::ObTimer timer_;

  int64_t last_study_timestamp_;
  // last timestamp sstable reports overflow during merge
  int64_t last_sstable_overflow_timestamp_;
  // Used only to select the retry cadence and report sampling health. A sample
  // failure does not invalidate the last complete local safety watermark.
  bool has_error_when_study_;
  // Local disk pressure disables the active-transaction watermark optimization.
  bool gc_is_disabled_;
  // Last complete local safety-watermark sample for active transactions.
  share::SCN local_reserved_snapshot_;
  bool is_inited_;
};

} // namespace concurrency_control
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_CONCURRENCY_CONTROL_OB_MULTI_VERSION_GARBAGE_COLLECTOR
