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

#ifndef OCEANBASE_OBSERVER_OB_TABLET_META_CHECKER
#define OCEANBASE_OBSERVER_OB_TABLET_META_CHECKER

#include "lib/task/ob_timer.h" // ObTimerTask
#include "share/tablet/ob_tablet_info.h" // ObTabletInfo

namespace oceanbase
{
namespace share
{
class ObTabletTableOperator;
}

namespace observer
{
class ObTabletMetaChecker;

class ObTabletMetaTableCheckTask : public common::ObTimerTask
{
public:
  explicit ObTabletMetaTableCheckTask(ObTabletMetaChecker &checker);
  virtual ~ObTabletMetaTableCheckTask() {}
  virtual void runTimerTask() override;
private:
  ObTabletMetaChecker &checker_;
};

// Checks the current database's records in __all_tablet_meta_table.
// It will supplement the missing tablet and remove residual tablet to meta table.
class ObTabletMetaChecker
{
public:
  ObTabletMetaChecker();
  virtual ~ObTabletMetaChecker() {}
  static int module_init(ObTabletMetaChecker *&checker);
  int init(
      share::ObTabletTableOperator *tt_operator);
  int start();
  void stop();
  void wait();
  void destroy();
  // check __all_tablet_meta_table with local ls_tablet_service
  int check_tablet_table();
  int schedule_tablet_meta_check_task();
private:
  static const int64_t TABLET_META_ROW_MAP_BUCKET_NUM = 64 * 1024;
  typedef common::hash::ObHashMap<ObTabletID, share::ObTabletInfo> ObTabletMetaRowMap;

  int build_tablet_meta_row_map_(ObTabletMetaRowMap &tablet_meta_row_map);
  int check_stale_tablet_meta_rows_(ObTabletMetaRowMap &tablet_meta_row_map, int64_t &stale_row_count);
  int check_missing_or_changed_tablet_meta_rows_(
      ObTabletMetaRowMap &tablet_meta_row_map,
      int64_t &missing_or_changed_row_count);
  int check_tablet_not_exist_in_local_(
      const ObTabletID &tablet_id,
      bool &not_exist);

  bool inited_;
  bool stopped_;
  common::ObTimer tablet_checker_timer_;
  share::ObTabletTableOperator *tt_operator_; // operator to process __all_tablet_meta_table
  ObTabletMetaTableCheckTask tablet_meta_check_task_; // timer task to check tablet meta
};

} // end namespace observer
} // end namespace oceanbase
#endif
