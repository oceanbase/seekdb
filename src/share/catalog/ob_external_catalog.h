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

#ifndef __SHARE_OB_EXTERNAL_CATALOG_H__
#define __SHARE_OB_EXTERNAL_CATALOG_H__
#include "share/external_table/ob_external_table_file_mgr.h"

namespace oceanbase
{
namespace share
{

struct ObCatalogBasicTableInfo
{
  // unix time, precision is second
  int64_t create_time_s = 0;
  int64_t last_ddl_time_s = 0;
  int64_t last_modification_time_s = 0;
};

// ObIExternalCatalog don't care about tenant_id, database_id, table_id
class ObIExternalCatalog
{
public:
  virtual ~ObIExternalCatalog() = default;
  virtual int init(const common::ObString &properties) = 0;
  virtual int list_namespace_names(common::ObIArray<common::ObString> &ns_names) = 0;
  virtual int list_table_names(const common::ObString &db_name,
                               const ObNameCaseMode case_mode,
                               common::ObIArray<common::ObString> &tbl_names) = 0;
  // if namespace not found, return OB_ERR_BAD_DATABASE
  virtual int fetch_namespace_schema(const common::ObString &ns_name,
                                     const ObNameCaseMode case_mode,
                                     share::schema::ObDatabaseSchema &database_schema) = 0;
  // if table not found, return OB_TABLE_NOT_EXIST
  virtual int fetch_table_schema(const common::ObString &ns_name,
                                 const common::ObString &tbl_name,
                                 const ObNameCaseMode case_mode,
                                 share::schema::ObTableSchema &table_schema) = 0;
  // if table not found, return OB_TABLE_NOT_EXIST
  virtual int fetch_basic_table_info(const common::ObString &ns_name,
                                     const common::ObString &tbl_name,
                                     const ObNameCaseMode case_mode,
                                     ObCatalogBasicTableInfo &table_info) = 0;
};

class ObICatalogMetaGetter
{
public:
  virtual ~ObICatalogMetaGetter() = default;
  virtual int list_namespace_names(const uint64_t tenant_id, const uint64_t catalog_id, common::ObIArray<common::ObString> &ns_names) = 0;
  virtual int list_table_names(const uint64_t tenant_id,
                               const uint64_t catalog_id,
                               const common::ObString &ns_name,
                               const ObNameCaseMode case_mode,
                               common::ObIArray<common::ObString> &tbl_names) = 0;
  // database_schema's database_id should assign correct before call this function
  virtual int fetch_namespace_schema(const uint64_t tenant_id,
                                     const uint64_t catalog_id,
                                     const common::ObString &ns_name,
                                     const ObNameCaseMode case_mode,
                                     share::schema::ObDatabaseSchema &database_schema) = 0;
  // table_schema's table_id/database_id should assign correct before call this function
  virtual int fetch_table_schema(const uint64_t tenant_id,
                                 const uint64_t catalog_id,
                                 const common::ObString &ns_name,
                                 const common::ObString &tbl_name,
                                 const ObNameCaseMode case_mode,
                                 share::schema::ObTableSchema &table_schema) = 0;
};

} // namespace share
} // namespace oceanbase

#endif // __SHARE_OB_EXTERNAL_CATALOG_H__
