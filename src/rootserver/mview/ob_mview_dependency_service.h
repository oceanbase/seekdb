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

#ifndef OCEANBASE_ROOTSERVER_MVIEW_OB_MVIEW_DEPENDENCY_SERVICE_H_
#define OCEANBASE_ROOTSERVER_MVIEW_OB_MVIEW_DEPENDENCY_SERVICE_H_

#include "lib/ob_define.h"
#include "lib/container/ob_array.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
namespace common
{
  class ObMySQLTransaction;
}
namespace share
{
namespace schema
{
  class ObSchemaGetterGuard;
  class ObMultiVersionSchemaService;
  class ObDependencyInfo;
}
}
namespace rootserver
{
class ObUpdateMViewRefTableOpt
{
  friend class ObMViewDependencyService;
public:
  ObUpdateMViewRefTableOpt() : need_update_table_flag_(false), need_update_mv_flag_(false) {}
  ~ObUpdateMViewRefTableOpt() {}
  void set_table_flag(const share::schema::ObTableReferencedByMVFlag &table_flag)
  {
    table_flag_ = table_flag;
    need_update_table_flag_ = true;
  }
  void set_mv_flag(const share::schema::ObTableReferencedByFastLSMMVFlag &mv_flag)
  {
    mv_flag_ = mv_flag;
    need_update_mv_flag_ = true;
  }
  TO_STRING_KV("need_update_table_flag_", need_update_table_flag_,
               "table_flag", table_flag_,
               "need_update_mv_flag_", need_update_mv_flag_,
               "mv_flag", mv_flag_);
private:
  bool need_update_table_flag_;
  share::schema::ObTableReferencedByMVFlag table_flag_;
  bool need_update_mv_flag_;
  share::schema::ObTableReferencedByFastLSMMVFlag mv_flag_;
};
class ObMViewDependencyService
{
public:
  ObMViewDependencyService(share::schema::ObMultiVersionSchemaService &schema_service);
  ~ObMViewDependencyService();
  int update_mview_dep_infos(common::ObMySQLTransaction &trans,
                             share::schema::ObSchemaGetterGuard &schema_guard,
                             const uint64_t tenant_id,
                             const uint64_t mview_table_id,
                             const common::ObIArray<share::schema::ObDependencyInfo> &dep_infos);
  int remove_mview_dep_infos(common::ObMySQLTransaction &trans,
                             share::schema::ObSchemaGetterGuard &schema_guard,
                             const uint64_t tenant_id,
                             const uint64_t mview_table_id);
  int update_mview_reference_table_status(
      common::ObMySQLTransaction &trans,
      share::schema::ObSchemaGetterGuard &schema_guard,
      const uint64_t tenant_id,
      const ObIArray<uint64_t> &ref_table_ids,
      const ObUpdateMViewRefTableOpt &update_opt);
private:
  share::schema::ObMultiVersionSchemaService &schema_service_;
};
} // end of sql
} // end of oceanbase
#endif
