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

#ifndef OCEANBASE_DATA_PLANE_DDL_OB_DDL_COORDINATOR_H_
#define OCEANBASE_DATA_PLANE_DDL_OB_DDL_COORDINATOR_H_

#include <stdint.h>

namespace oceanbase
{
namespace obcall
{
struct ObCalcColumnChecksumResponseArg;
struct ObDDLLocalBuildResponse;
struct ObRebuildIndexArg;
struct ObAlterTableRes;
}
namespace data_plane
{

// Storage reports a completed replica-side checksum calculation through this
// coordinator seam; the RootService implementation remains outside Storage.
int report_column_checksum_response(
    const obcall::ObCalcColumnChecksumResponseArg &arg);

// Storage reports completion of replica-side DDL work through the same seam;
// RootService remains an implementation detail of Rootserver.
int report_ddl_single_replica_response(
    const obcall::ObDDLLocalBuildResponse &arg);

// Renew the coordinator-side liveness lease for a running DDL task without
// exposing RootService or Rootserver task vocabulary to Storage.
int renew_ddl_task_lease(int64_t task_id);

// Submit a vector-index rebuild through RootService without exposing
// Rootserver dispatch or serialization details to Storage.
int rebuild_vector_index(
    const obcall::ObRebuildIndexArg &arg,
    obcall::ObAlterTableRes &res);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_DDL_OB_DDL_COORDINATOR_H_
