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

#ifndef OCEANBASE_SHARE_IMPORT_TABLE_UTIL_H
#define OCEANBASE_SHARE_IMPORT_TABLE_UTIL_H
#include "lib/ob_define.h"
#include "share/schema/ob_multi_version_schema_service.h"
namespace oceanbase
{
namespace share
{
class ObImportTableUtil final
{
public:
static bool can_retrieable_err(const int err_code);
static int get_tenant_schema_guard(share::schema::ObMultiVersionSchemaService &schema_service, uint64_t tenant_id,
    share::schema::ObSchemaGetterGuard &guard);
static int check_database_schema_exist(share::schema::ObMultiVersionSchemaService &schema_service, 
    uint64_t tenant_id, const ObString &db_name, bool &is_exist);
static int check_table_schema_exist(share::schema::ObMultiVersionSchemaService &schema_service, 
    uint64_t tenant_id, const ObString &db_name, const ObString &table_name, bool &is_exist);
static int check_tablegroup_exist(share::schema::ObMultiVersionSchemaService &schema_service,
    uint64_t tenant_id, const ObString &tablegroup, bool &is_exist);
static int check_tablespace_exist(share::schema::ObMultiVersionSchemaService &schema_service,
    uint64_t tenant_id, const ObString &tablespace, bool &is_exist);
static int get_tenant_name_case_mode(const uint64_t tenant_id, ObNameCaseMode &name_case_mode);
static int check_is_recover_table_aux_tenant(
    share::schema::ObMultiVersionSchemaService &schema_service, const uint64_t tenant_id, bool &is_recover_table_aux_tenant);
static int check_is_recover_table_aux_tenant_name(const ObString &tenant_name, bool &is_recover_table_aux_tenant);
};

}
}

#endif
