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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_TENANT_MAJOR_FREEZE_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_TENANT_MAJOR_FREEZE_

#include "rootserver/freeze/ob_major_merge_scheduler.h"
#include "share/ob_freeze_info_manager.h"
#include "rootserver/freeze/ob_major_merge_progress_checker.h"
#include "rootserver/freeze/ob_zone_merge_manager.h"
#include "rootserver/freeze/ob_major_merge_info_manager.h"
#include "rootserver/freeze/ob_freeze_info_detector.h"
#include "rootserver/freeze/ob_daily_major_freeze_launcher.h"

namespace oceanbase
{
namespace share
{
class ObIServerTrace;
namespace schema
{
class ObMultiVersionSchemaService;
}
}
namespace rootserver
{

// major freeze for tenant level merge
// 1. generate freeze info
// 2. schedule major merge
// 3. check major merge whether finish
// 4. do checksum check
class ObTenantMajorFreeze
{
public:
  ObTenantMajorFreeze(const uint64_t tenant_id);
  virtual ~ObTenantMajorFreeze();
  int init(const bool is_primary_service,
           common::ObMySQLProxy &sql_proxy,
           common::ObServerConfig &config,
           share::schema::ObMultiVersionSchemaService &schema_service,
           share::ObIServerTrace &server_trace);

  int start();
  void stop();
  int wait();
  int destroy();

  // for switch_role fastly
  void pause();
  void resume();

  bool is_paused() const;

  uint64_t get_tenant_id() const { return tenant_id_; }
  int launch_major_freeze(const ObMajorFreezeReason freeze_reason);

  int suspend_merge();

  int resume_merge();

  int clear_merge_error();

  int get_uncompacted_tablets(
    common::ObArray<share::ObTabletReplica> &uncompacted_tablets,
    common::ObArray<uint64_t> &uncompacted_table_ids) const;

private:
  // major merge one by one
  static const int64_t UNMERGED_VERSION_LIMIT = 1;

  int check_freeze_info();
  int check_tenant_status() const;

  int set_freeze_info(const ObMajorFreezeReason freeze_reason);

  bool is_primary_service() const { return is_primary_service_; }
  int try_schedule_minor_before_major_();

private:
  bool is_inited_;
  uint64_t tenant_id_;
  bool is_primary_service_;  // identify ObMajorFreezeServiceType::SERVICE_TYPE_PRIMARY

  ObMajorMergeInfoManager major_merge_info_mgr_;
  ObMajorMergeInfoDetector major_merge_info_detector_;
  ObMajorMergeScheduler merge_scheduler_;
  ObDailyMajorFreezeLauncher daily_launcher_;

  share::schema::ObMultiVersionSchemaService *schema_service_;

  DISALLOW_COPY_AND_ASSIGN(ObTenantMajorFreeze);
};

} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_FREEZE_ROOTSERVER_OB_TENANT_MAJOR_FREEZE_
