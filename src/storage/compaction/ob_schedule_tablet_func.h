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
#ifndef OB_STORAGE_COMPACTION_SCHEDULE_TABLET_FUNC_H_
#define OB_STORAGE_COMPACTION_SCHEDULE_TABLET_FUNC_H_
#include "storage/compaction/ob_basic_schedule_tablet_func.h"
namespace oceanbase
{
namespace storage
{
class ObLS;
class ObTablet;
class ObTabletHandle;
}
namespace compaction
{
struct ObScheduleTabletFunc final : public ObBasicScheduleTabletFunc
{
  ObScheduleTabletFunc(
    const int64_t merge_version,
    const int64_t loop_cnt = 0,
    const ObAdaptiveMergePolicy::AdaptiveMergeReason merge_reason = ObAdaptiveMergePolicy::NONE);
  virtual ~ObScheduleTabletFunc() {}
  int schedule_tablet(
    storage::ObTabletHandle &tablet_handle,
    bool &tablet_merge_finish);
  int request_schedule_new_round(
    storage::ObTabletHandle &tablet_handle,
    const bool user_request);
  const ObTabletStatusCache &get_tablet_status() const { return tablet_status_; }
  virtual const ObCompactionTimeGuard &get_time_guard() const override { return time_guard_; }
  int diagnose_switch_tablet(storage::ObLS &ls, const storage::ObTablet &tablet);
  void set_loop_cnt(const int64_t loop_cnt) { loop_cnt_ = loop_cnt; }
  INHERIT_TO_STRING_KV("ObScheduleTabletFunc", ObBasicScheduleTabletFunc,
    K_(merge_reason), K_(tablet_status), K_(time_guard));
private:
  virtual void schedule_freeze_dag(const bool force) override;
  int schedule_tablet_new_round(
    storage::ObTabletHandle &tablet_handle,
    const bool user_request);
  int schedule_tablet_execute(
    storage::ObTablet &tablet);
  int get_schedule_execute_info(
    storage::ObTablet &tablet,
    int64_t &schedule_scn,
    ObCOMajorMergePolicy::ObCOMajorMergeType &co_major_merge_type);
private:
  ObTabletStatusCache tablet_status_;
  ObCompactionScheduleTimeGuard time_guard_;
  ObSEArray<ObTabletID, 64> clear_stat_tablets_;
  ObAdaptiveMergePolicy::AdaptiveMergeReason merge_reason_;
};

} // namespace compaction
} // namespace oceanbase

#endif // OB_STORAGE_COMPACTION_SCHEDULE_TABLET_FUNC_H_
