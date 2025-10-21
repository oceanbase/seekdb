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

#ifndef __SHARE_OB_CATALOG_META_GETTER_H__
#define __SHARE_OB_CATALOG_META_GETTER_H__

#include "share/catalog/ob_external_catalog.h"

namespace oceanbase
{
namespace share
{

class ObCatalogMetaGetter final : public ObICatalogMetaGetter
{
public:
  ObCatalogMetaGetter(ObSchemaGetterGuard &schema_getter_guard, ObIAllocator &allocator)
      : schema_getter_guard_(schema_getter_guard), allocator_(allocator)
  {
  }

  ~ObCatalogMetaGetter() = default;

  int list_namespace_names(const uint64_t tenant_id, const uint64_t catalog_id, common::ObIArray<common::ObString> &ns_names) override;
  int list_table_names(const uint64_t tenant_id,
                       const uint64_t catalog_id,
                       const common::ObString &ns_name,
                       const ObNameCaseMode case_mode,
                       common::ObIArray<common::ObString> &tbl_names) override;
  int fetch_namespace_schema(const uint64_t tenant_id,
                             const uint64_t catalog_id,
                             const common::ObString &ns_name,
                             const ObNameCaseMode case_mode,
                             share::schema::ObDatabaseSchema &database_schema) override;
  int fetch_table_schema(const uint64_t tenant_id,
                         const uint64_t catalog_id,
                         const common::ObString &ns_name,
                         const common::ObString &tbl_name,
                         const ObNameCaseMode case_mode,
                         share::schema::ObTableSchema &table_schema) override;

  int fetch_basic_table_info(const uint64_t tenant_id,
                             const uint64_t catalog_id,
                             const common::ObString &ns_name,
                             const common::ObString &tbl_name,
                             const ObNameCaseMode case_mode,
                             ObCatalogBasicTableInfo &table_info);

private:
  ObSchemaGetterGuard &schema_getter_guard_;
  ObIAllocator &allocator_;

  int get_catalog_(const uint64_t tenant_id, const uint64_t catalog_id, ObIExternalCatalog *&catalog);
};

} // namespace share
} // namespace oceanbase

#endif // __SHARE_OB_CATALOG_META_GETTER_H__
