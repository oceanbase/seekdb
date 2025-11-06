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

#ifndef OCEANBASE_ROOTSERVER_OB_SNAPSHOT_INFO_MANAGER_H_
#define OCEANBASE_ROOTSERVER_OB_SNAPSHOT_INFO_MANAGER_H_

#include "share/ob_snapshot_table_proxy.h"
#include "lib/net/ob_addr.h"
#include "share/scn.h"
namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace share
{
class ObSnapshotInfo;
  class SCN;
}
namespace rootserver
{
class ObZoneManager;
class ObSnapshotInfoManager
{
public:
  ObSnapshotInfoManager() : self_addr_() {}
  virtual ~ObSnapshotInfoManager() {}
  int init(const common::ObAddr &self_addr);

  int check_restore_point(common::ObMySQLProxy &proxy,
                          const uint64_t tenant_id,
                          const int64_t table_id,
                          bool &is_exist);
  int batch_acquire_snapshot(
      common::ObMySQLTransaction &trans,
      share::ObSnapShotType snapshot_type,
      const uint64_t tenant_id,
      const int64_t schema_version,
      const share::SCN &snapshot_scn,
      const char *comment,
      const common::ObIArray<ObTabletID> &tablet_ids);
  int batch_release_snapshot_in_trans(
      common::ObMySQLTransaction &trans,
      share::ObSnapShotType snapshot_type,
      const uint64_t tenant_id,
      const int64_t schema_version,
      const share::SCN &snapshot_scn,
      const common::ObIArray<ObTabletID> &tablet_ids);

private:
  common::ObAddr self_addr_;
  DISALLOW_COPY_AND_ASSIGN(ObSnapshotInfoManager);
};
} //end rootserver
} //end oceanbase
#endif
