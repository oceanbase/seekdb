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

#ifndef OCEANBASE_STORAGE_DDL_OB_TABLE_FORK_INFO_H_
#define OCEANBASE_STORAGE_DDL_OB_TABLE_FORK_INFO_H_

#include "share/ob_ls_id.h"
#include "common/ob_tablet_id.h"
#include "lib/container/ob_se_array.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/worker.h"

namespace oceanbase
{
namespace storage
{

struct ObTabletForkParam;

class ObTableForkInfo final
{
  OB_UNIS_VERSION_V(1);
public:
  ObTableForkInfo();
  ObTableForkInfo(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const uint64_t table_id,
      const int64_t schema_version,
      const int64_t task_id,
      const int64_t fork_snapshot_version,
      const lib::Worker::CompatMode compat_mode,
      const int64_t data_format_version,
      const int64_t consumer_group_id,
      const common::ObIArray<common::ObTabletID> &source_tablet_ids,
      const common::ObIArray<common::ObTabletID> &dest_tablet_ids);
  ~ObTableForkInfo() = default;
  int assign(const ObTableForkInfo &info);
  bool is_valid() const;
  int generate_fork_params(common::ObIArray<ObTabletForkParam> &params) const;
  int get_tablet_fork_param(const common::ObTabletID &tablet_id, ObTabletForkParam &tablet_fork_param) const;
  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(table_id), K_(schema_version), K_(task_id),
               K_(source_tablet_ids), K_(dest_tablet_ids),
               K_(fork_snapshot_version), K_(compat_mode),
               K_(data_format_version), K_(consumer_group_id));
public:
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  uint64_t table_id_;
  int64_t schema_version_;
  int64_t task_id_;
  common::ObSEArray<common::ObTabletID, 4> source_tablet_ids_; // Source tablet IDs array
  common::ObSEArray<common::ObTabletID, 4> dest_tablet_ids_;   // Destination tablet IDs array
  int64_t fork_snapshot_version_;
  lib::Worker::CompatMode compat_mode_;
  int64_t data_format_version_;
  int64_t consumer_group_id_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_DDL_OB_TABLE_FORK_INFO_H_
