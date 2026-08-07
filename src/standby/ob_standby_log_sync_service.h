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

#ifndef OCEANBASE_STANDBY_OB_STANDBY_LOG_SYNC_SERVICE_H_
#define OCEANBASE_STANDBY_OB_STANDBY_LOG_SYNC_SERVICE_H_

#include "lib/lock/ob_mutex.h"
#include "lib/net/ob_addr.h"
#include "lib/task/ob_timer.h"
#include "share/log/palf/lsn.h"
#include "share/scn.h"

namespace oceanbase
{
namespace standby
{

// Owns the only standby log-import loop. Role transitions serialize with this
// service so no source log can be appended after promotion starts.
class ObStandbyLogSyncService final : public common::ObTimerTask
{
public:
  static int init();
  static int start();
  static int stop();
  static int wait();
  static void destroy();

  static int prepare_switch_to_primary(const bool is_failover);
  static int pause();
  static int resume();
  static int set_startup_target_scn(const share::SCN &target_scn);
  static int wait_startup_replay();
  static int get_local_progress(share::SCN &end_scn, share::SCN &sync_scn);

  void runTimerTask() override;

private:
  ObStandbyLogSyncService();
  ~ObStandbyLogSyncService() = default;
  static ObStandbyLogSyncService &instance_();

  int init_();
  int start_();
  int stop_();
  int wait_();
  void destroy_();
  int prepare_switch_to_primary_(const bool is_failover);
  int pause_();
  int resume_();
  int set_startup_target_scn_(const share::SCN &target_scn);
  int wait_startup_replay_();
  int get_source_addr_(common::ObAddr &source_addr) const;
  int query_source_end_scn_(const common::ObAddr &source_addr, share::SCN &end_scn);
  int sync_once_(const common::ObAddr &source_addr, bool &made_progress);
  int append_log_group_(const char *buf,
                        const int64_t size,
                        const palf::LSN &source_lsn,
                        const share::SCN &source_scn);
  int wait_local_replay_();

private:
  static const int64_t SYNC_INTERVAL_US = 100 * 1000L;
  static const int64_t RPC_TIMEOUT_US = 10 * 1000 * 1000L;
  static const int64_t SWITCH_TIMEOUT_US = 30 * 1000 * 1000L;
  static const int64_t STARTUP_TIMEOUT_US = 60 * 1000 * 1000L;
  static const int64_t FETCH_BATCH_BYTES = 16 * 1024 * 1024L;

  common::ObTimer timer_;
  lib::ObMutex lock_;
  bool is_inited_;
  bool is_scheduled_;
  bool paused_;
  int fatal_error_;
  share::SCN startup_target_scn_;

  DISALLOW_COPY_AND_ASSIGN(ObStandbyLogSyncService);
};

} // namespace standby
} // namespace oceanbase

#endif /* OCEANBASE_STANDBY_OB_STANDBY_LOG_SYNC_SERVICE_H_ */
