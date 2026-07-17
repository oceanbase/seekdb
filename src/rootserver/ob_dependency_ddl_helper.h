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

#ifndef OCEANBASE_ROOTSERVER_OB_DEPENDENCY_DDL_HELPER_H_
#define OCEANBASE_ROOTSERVER_OB_DEPENDENCY_DDL_HELPER_H_

#include <utility>
#include "lib/container/ob_iarray.h"
#include "share/schema/ob_dependency_info.h"  // ObObjectType, ObReferenceObjTable::DependencyObjKeyItemPairs

namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
}
namespace share
{
class ObDMLSqlSplicer;
namespace schema
{
class ObMultiVersionSchemaService;
class ObSchemaGetterGuard;
}
}
namespace rootserver
{
class ObDDLOperator;

// DDL-execution helpers that relocate ObDependencyInfo / ObReferenceObjTable static
// methods which take a rootserver::ObDDLOperator & (genuine DDL-transaction logic,
// callers are all rootserver). Lift-and-shift: same static signatures, only the class
// home and call-site prefix changed. rootserver -> share is a legal downward dependency.
class ObDependencyDDLHelper
{
public:
  // relocated from share::schema::ObDependencyInfo
  static int cascading_modify_obj_status(common::ObMySQLTransaction &trans,
                                         uint64_t obj_id,
                                         rootserver::ObDDLOperator &ddl_operator,
                                         share::schema::ObMultiVersionSchemaService &schema_service);
  static int modify_dep_obj_status(common::ObMySQLTransaction &trans,
                                   uint64_t obj_id,
                                   rootserver::ObDDLOperator &ddl_operator,
                                   share::schema::ObMultiVersionSchemaService &schema_service);
  static int modify_all_obj_status(const common::ObIArray<std::pair<uint64_t, share::schema::ObObjectType>> &objs,
                                   common::ObMySQLTransaction &trans,
                                   rootserver::ObDDLOperator &ddl_operator,
                                   share::schema::ObMultiVersionSchemaService &schema_service);

  // relocated from share::schema::ObReferenceObjTable
  static int batch_execute_insert_or_update_obj_dependency(
      const int64_t new_schema_version,
      const share::schema::ObReferenceObjTable::DependencyObjKeyItemPairs &dep_objs,
      common::ObMySQLTransaction &trans,
      share::schema::ObSchemaGetterGuard &schema_guard,
      rootserver::ObDDLOperator &ddl_operator);
  static int update_max_dependency_version(
      const int64_t dep_obj_id,
      const int64_t max_dependency_version,
      common::ObMySQLTransaction &trans,
      share::schema::ObSchemaGetterGuard &schema_guard,
      rootserver::ObDDLOperator &ddl_operator);

private:
  // relocated private helper of ObReferenceObjTable (sole caller was the batch method above)
  static int batch_fill_kv_pairs(const share::schema::ObReferenceObjTable::ObDependencyObjKey &dep_obj_key,
                                 const int64_t new_schema_version,
                                 common::ObIArray<share::schema::ObDependencyInfo> &dep_infos,
                                 share::ObDMLSqlSplicer &dml);
};

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_DEPENDENCY_DDL_HELPER_H_
