#ifdef OB_BUILD_CPP_ODPS
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
#ifndef _SHARE_OB_ODPS_CATALOG_H
#define _SHARE_OB_ODPS_CATALOG_H

#include "share/catalog/ob_catalog_properties.h"
#include "share/catalog/ob_external_catalog.h"

namespace oceanbase
{
namespace share
{

class ObOdpsCatalog final : public ObIExternalCatalog
{
public:
  explicit ObOdpsCatalog(common::ObIAllocator &allocator) : allocator_(allocator) {}
  ~ObOdpsCatalog() = default;
  virtual int init(const common::ObString &properties) override;
  virtual int list_namespace_names(common::ObIArray<common::ObString> &ns_names) override;
  virtual int list_table_names(const common::ObString &ns_name,
                               const ObNameCaseMode case_mode,
                               common::ObIArray<common::ObString> &tb_names) override;
  virtual int fetch_namespace_schema(const common::ObString &ns_name,
                                     const ObNameCaseMode case_mode,
                                     share::schema::ObDatabaseSchema &database_schema) override;
  virtual int fetch_table_schema(const common::ObString &ns_name,
                                 const common::ObString &tb_name,
                                 const ObNameCaseMode case_mode,
                                 share::schema::ObTableSchema &table_schema) override;
  virtual int fetch_basic_table_info(const common::ObString &ns_name,
                                     const common::ObString &tbl_name,
                                     const ObNameCaseMode case_mode,
                                     ObCatalogBasicTableInfo &table_info) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ObOdpsCatalog);
  int convert_odps_format_to_str_properties_(const ObODPSGeneralFormat &odps_format, ObString &str);

  common::ObIAllocator &allocator_;
  apsara::odps::sdk::Configuration conf_;
  apsara::odps::sdk::IODPSPtr odps_;
  apsara::odps::sdk::IODPSTablesPtr tables_;
  ObODPSCatalogProperties properties_;
};

} // namespace share
} // namespace oceanbase

#endif // _SHARE_OB_ODPS_CATALOG_H
#endif
