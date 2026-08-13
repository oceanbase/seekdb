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

#ifndef OCEANBASE_DATA_PLANE_COMPACTION_OB_I_MAJOR_FREEZE_COORDINATOR_H_
#define OCEANBASE_DATA_PLANE_COMPACTION_OB_I_MAJOR_FREEZE_COORDINATOR_H_

#include <cstdint>
#include "common/ob_tablet_id.h"
#include "lib/container/ob_iarray.h"
#include "lib/net/ob_addr.h"
#include "lib/utility/ob_print_utils.h"
#include "share/scn.h"

namespace oceanbase
{
namespace data_plane
{

// Stable diagnostic value returned across the Storage/Rootserver seam.
// Rootserver's replica/service implementation types deliberately do not cross
// this boundary.
struct ObMajorMergeTabletDiagnostic
{
  ObMajorMergeTabletDiagnostic()
    : tablet_id_(),
      server_(),
      snapshot_version_(0),
      report_scn_(0),
      checksum_error_(false)
  {}

  common::ObTabletID tablet_id_;
  common::ObAddr server_;
  int64_t snapshot_version_;
  int64_t report_scn_;
  bool checksum_error_;

  TO_STRING_KV(
      K_(tablet_id),
      K_(server),
      K_(snapshot_version),
      K_(report_scn),
      K_(checksum_error));
};

// Demand-owned boundary for the three Rootserver capabilities used by
// Storage's tenant freezer and compaction diagnostics.
class ObIMajorFreezeCoordinator
{
public:
  virtual ~ObIMajorFreezeCoordinator() = default;

  virtual int get_frozen_scn(share::SCN &frozen_scn) const = 0;
  virtual int trigger_memstore_pressure_major_freeze() = 0;
  virtual int collect_major_merge_diagnostics(
      bool &need_diagnose,
      bool &is_paused,
      common::ObIArray<ObMajorMergeTabletDiagnostic> &uncompacted_tablets,
      common::ObIArray<uint64_t> &uncompacted_table_ids) const = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_COMPACTION_OB_I_MAJOR_FREEZE_COORDINATOR_H_
