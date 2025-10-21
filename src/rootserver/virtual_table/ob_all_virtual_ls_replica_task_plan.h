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

#ifndef OCEANBASE_ROOTSERVER_OB_ALL_VIRTUAL_LS_REPLICA_TASK_PLAN_H_
#define OCEANBASE_ROOTSERVER_OB_ALL_VIRTUAL_LS_REPLICA_TASK_PLAN_H_

#include "share/ob_virtual_table_projector.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
class ObTableSchema;
}
}
namespace rootserver
{
class ObDRWorker;
class ObLSReplicaTaskDisplayInfo;
class ObAllVirtualLSReplicaTaskPlan : public common::ObVirtualTableProjector
{
public:
  ObAllVirtualLSReplicaTaskPlan();
  virtual ~ObAllVirtualLSReplicaTaskPlan();
  virtual int inner_get_next_row(common::ObNewRow *&row);

private:
  int get_full_row_(const share::schema::ObTableSchema *table,
                    const ObLSReplicaTaskDisplayInfo &task_stat,
                    common::ObIArray<Column> &columns);
private:
  common::ObArenaAllocator arena_allocator_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualLSReplicaTaskPlan);
};
}//end namespace rootserver
}//end namespace oceanbase
#endif // OCEANBASE_ROOTSERVER_OB_ALL_VIRTUAL_LS_REPLICA_TASK_PLAN_H_
