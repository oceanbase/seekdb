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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_MAJOR_FREEZE_COORDINATOR_ADAPTER_H_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_MAJOR_FREEZE_COORDINATOR_ADAPTER_H_

#include "data_plane/compaction/ob_i_major_freeze_coordinator.h"

namespace oceanbase
{
namespace rootserver
{

class ObPrimaryMajorFreezeService;
class ObRestoreMajorFreezeService;

// Rootserver implementation adapter. Observer owns and wires this object; the
// lower Storage module sees only ObIMajorFreezeCoordinator.
class ObMajorFreezeCoordinatorAdapter final
    : public data_plane::ObIMajorFreezeCoordinator
{
public:
  ObMajorFreezeCoordinatorAdapter();
  ~ObMajorFreezeCoordinatorAdapter() override = default;

  int init(ObPrimaryMajorFreezeService &primary_service,
           ObRestoreMajorFreezeService &restore_service);
  void reset();

  int get_frozen_scn(share::SCN &frozen_scn) const override;
  int trigger_memstore_pressure_major_freeze() override;
  int collect_major_merge_diagnostics(
      bool &need_diagnose,
      bool &is_paused,
      common::ObIArray<data_plane::ObMajorMergeTabletDiagnostic>
          &uncompacted_tablets,
      common::ObIArray<uint64_t> &uncompacted_table_ids) const override;

private:
  ObPrimaryMajorFreezeService *primary_service_;
  ObRestoreMajorFreezeService *restore_service_;
};

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_FREEZE_OB_MAJOR_FREEZE_COORDINATOR_ADAPTER_H_
