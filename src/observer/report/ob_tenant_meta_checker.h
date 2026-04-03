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

#ifndef OCEANBASE_OBSERVER_OB_TENANT_META_TABLE_CHECKER
#define OCEANBASE_OBSERVER_OB_TENANT_META_TABLE_CHECKER

#include "lib/task/ob_timer.h" // ObTimerTask
#include "share/tablet/ob_tablet_info.h" // ObTabletReplica

namespace oceanbase
{
namespace share
{
class ObTabletTableOperator;
}

namespace observer
{
class ObTenantMetaChecker;

class ObTenantTabletMetaTableCheckTask : public common::ObTimerTask
{
public:
  explicit ObTenantTabletMetaTableCheckTask(ObTenantMetaChecker &checker);
  virtual ~ObTenantTabletMetaTableCheckTask() {}
  virtual void runTimerTask() override;
private:
  ObTenantMetaChecker &checker_;
};

// ObTenantMetaChecker is used to check info in __all_tablet_meta_table for tenant.
// It will supplement the missing tablet and remove residual tablet to meta table.
class ObTenantMetaChecker
{
public:
  ObTenantMetaChecker();
  virtual ~ObTenantMetaChecker() {}
  static int mtl_init(ObTenantMetaChecker *&checker);
  int init(
      const uint64_t tenant_id,
      share::ObTabletTableOperator *tt_operator);
  int start();
  void stop();
  void wait();
  void destroy();
  // check __all_tablet_meta_table with local ls_tablet_service
  int check_tablet_table();
  int schedule_tablet_meta_check_task();
private:
  static const int64_t TABLET_REPLICA_MAP_BUCKET_NUM = 64 * 1024;
  typedef common::hash::ObHashMap<share::ObTabletLSPair, share::ObTabletReplica> ObTabletReplicaMap;

  int build_replica_map_(ObTabletReplicaMap &replica_map);
  int check_dangling_replicas_(ObTabletReplicaMap &replica_map, int64_t &dangling_count);
  int check_report_replicas_(ObTabletReplicaMap &replica_map, int64_t &report_count);
  int check_tablet_not_exist_in_local_(
      const share::ObLSID &ls_id,
      const ObTabletID &tablet_id,
      bool &not_exist);

  bool inited_;
  bool stopped_;
  uint64_t tenant_id_;
  int tablet_checker_tg_id_;
  share::ObTabletTableOperator *tt_operator_; // operator to process __all_tablet_meta_table
  ObTenantTabletMetaTableCheckTask tablet_meta_check_task_; // timer task to check tablet meta
};

} // end namespace observer
} // end namespace oceanbase
#endif
