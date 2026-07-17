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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_DAILY_MAJOR_FREEZE_LAUNCHER_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_DAILY_MAJOR_FREEZE_LAUNCHER_

#include "share/ob_define.h"
#include "lib/net/ob_addr.h"
#include "lib/task/ob_timer.h"
#include "share/scn.h"

namespace oceanbase
{
namespace common
{
class ObServerConfig;
class ObMySQLProxy;
}
namespace rootserver
{
class ObMajorMergeInfoManager;
// primary cluster: sys tenant, meta tenant, user tenant all have this launcher
// standby cluster: only sys tenant, meta tenant have this launcher
class ObDailyMajorFreezeLauncher : public common::ObTimerTask
{
public:
  ObDailyMajorFreezeLauncher();
  virtual ~ObDailyMajorFreezeLauncher();
  int init(common::ObServerConfig &config,
           common::ObMySQLProxy &proxy,
           ObMajorMergeInfoManager &merge_info_manager);

  virtual void runTimerTask() override;
  int start();
  void stop();
  void wait();
  int destroy();
  void pause() { is_paused_ = true; }
  void resume() { is_paused_ = false; }
  bool is_paused() const { return is_paused_; }

private:
  int try_launch_major_freeze();
  int try_gc_freeze_info();
  int try_gc_tablet_checksum();

private:
  static const int64_t MAJOR_FREEZE_RETRY_LIMIT = 120;
  static const int64_t MAJOR_FREEZE_LAUNCHER_THREAD_CNT = 1;
  static const int64_t LAUNCHER_INTERVAL_US = 5 * 1000 * 1000; // 5s
  static const int64_t MAJOR_FREEZE_RETRY_INTERVAL_US = 1000 * 1000; // 1s
#ifdef _WIN32
  static constexpr int64_t MODIFY_GC_INTERVAL = 86400000000LL; // 1 day
#else
  static const int64_t MODIFY_GC_INTERVAL = 24 * 60 * 60 * 1000 * 1000L; // 1 day
#endif
  static const int64_t TABLET_CKM_CHECK_INTERVAL_US = 30 * 60 * 1000 * 1000L; // 30 min

  bool is_inited_;
  bool is_paused_;
  bool already_launch_;
  common::ObMySQLProxy *sql_proxy_;
  common::ObServerConfig *config_;
  int64_t gc_freeze_info_last_timestamp_;
  ObMajorMergeInfoManager *merge_info_mgr_;
  int64_t last_check_tablet_ckm_us_;
  share::SCN tablet_ckm_gc_compaction_scn_;
  volatile bool stop_;
  common::ObTimer timer_;

  DISALLOW_COPY_AND_ASSIGN(ObDailyMajorFreezeLauncher);
};

}//end namespace rootserver
}//end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_FREEZE_OB_DAILY_MAJOR_FREEZE_LAUNCHER_
