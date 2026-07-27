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

#ifndef OCEANBASE_SHARE_OB_SNAPSHOT_TABLE_PROXY_H_
#define OCEANBASE_SHARE_OB_SNAPSHOT_TABLE_PROXY_H_

#include "lib/container/ob_iarray.h"
#include "lib/lock/ob_mutex.h"
#include "share/ob_define.h"
#include "share/scn.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
class ObMySQLTransaction;
}
namespace share
{
class ObDMLSqlSplicer;

enum ObSnapShotType
{
  SNAPSHOT_FOR_MAJOR = 0,
  SNAPSHOT_FOR_DDL = 1,
  SNAPSHOT_FOR_MULTI_VERSION = 2,
  SNAPSHOT_TYPE_RESERVED_3 = 3,
  SNAPSHOT_FOR_BACKUP_POINT = 4,
  MAX_SNAPSHOT_TYPE,
};

struct ObSnapshotInfo
{
public:
  ObSnapShotType snapshot_type_;
  SCN snapshot_scn_;
  int64_t schema_version_;
  uint64_t tablet_id_; // OB_INVALID_ID represents all local tablets.
  const char* comment_;
  ObSnapshotInfo();
  ~ObSnapshotInfo() {}
  int init(const uint64_t tablet_id,
           const ObSnapShotType &snapshot_type, const SCN &snapshot_scn,
           const int64_t schema_version, const char* comment);
  void reset();
  bool is_valid() const;
  static bool is_valid_snapshot_type(const ObSnapShotType snapshot_type);
  static const char * get_snapshot_type_str(const ObSnapShotType &snapshot_type);
  static const char *ObSnapShotTypeStr[];
  TO_STRING_KV(K_(snapshot_type),
               K_(snapshot_scn),
               K_(schema_version),
               K_(tablet_id),
               KP_(comment));

};

class ObSnapshotTableProxy
{
  static const int64_t BATCH_OP_SIZE = 256;
public:
  ObSnapshotTableProxy() : lock_(ObLatchIds::DEFAULT_MUTEX), last_event_ts_(0) {}
  virtual ~ObSnapshotTableProxy() {}

  int add_snapshot(
      common::ObMySQLTransaction &trans,
      const share::ObSnapshotInfo &snapshot);

  int batch_add_snapshot(
      common::ObMySQLTransaction &trans,
      const share::ObSnapShotType snapshot_type,
      const int64_t schema_version,
      const SCN &snapshot_scn,
      const char *comment,
      const common::ObIArray<ObTabletID> &tablet_id_array);

  int remove_snapshot(common::ObISQLClient &proxy,
                      const ObSnapshotInfo &info);
  int batch_remove_snapshots(common::ObISQLClient &proxy,
                             share::ObSnapShotType snapshot_type,
                             const int64_t schema_version,
                             const SCN &snapshot_scn,
                             const common::ObIArray<ObTabletID> &tablet_ids);
  int get_all_snapshots(common::ObISQLClient &proxy,
                        common::ObIArray<ObSnapshotInfo> &snapshots);
  int get_all_snapshots(common::ObISQLClient &proxy,
                        ObSnapShotType snapshot_type,
                        common::ObIArray<ObSnapshotInfo> &snapshots);
  int get_snapshot(common::ObISQLClient &proxy,
                   const ObSnapShotType snapshot_type,
                   const SCN &snapshot_scn,
                   ObSnapshotInfo &snapshot_info);

  int get_max_snapshot_info(common::ObISQLClient &proxy,
                            ObSnapshotInfo &snapshot_info);
  int check_snapshot_exist(common::ObISQLClient &proxy,
                           const share::ObSnapShotType snapshot_type,
                           bool &is_exist);
private:
  int gen_event_ts(int64_t &event_ts);
  int check_snapshot_valid(const SCN &snapshot_gc_scn,
                           const ObSnapshotInfo &info,
                           bool &is_valid) const;
  int fill_snapshot_item(const ObSnapshotInfo &info,
      share::ObDMLSqlSplicer &dml);

private:
  lib::ObMutex lock_;
  int64_t last_event_ts_;
};
} //namespace share
} //namespace oceanbase

#endif // OCEANBASE_SHARE_OB_SNAPSHOT_TABLE_PROXY_H_
