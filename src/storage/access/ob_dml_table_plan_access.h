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

#ifndef OCEANBASE_STORAGE_ACCESS_OB_DML_TABLE_PLAN_ACCESS_H_
#define OCEANBASE_STORAGE_ACCESS_OB_DML_TABLE_PLAN_ACCESS_H_

#include "data_plane/access/ob_dml_table_plan.h"
#include "storage/ob_table_dml_param.h"

namespace oceanbase
{
namespace storage
{

// Storage-only adapter for the opaque public table plan.
class ObDmlTablePlanAccess
{
public:
  static share::schema::ObTableDMLParam *get(data_plane::ObDmlTablePlan &plan);
  static const share::schema::ObTableDMLParam *get(const data_plane::ObDmlTablePlan &plan);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_ACCESS_OB_DML_TABLE_PLAN_ACCESS_H_
