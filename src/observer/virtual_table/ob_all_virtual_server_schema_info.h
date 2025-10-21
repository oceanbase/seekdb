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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SERVER_SCHEMA_INFO_H_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SERVER_SCHEMA_INFO_H_


#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_scanner.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "common/row/ob_row.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualServerSchemaInfo: public common::ObVirtualTableScannerIterator
{
public:
  explicit ObAllVirtualServerSchemaInfo(share::schema::ObMultiVersionSchemaService &schema_service)
             : schema_service_(schema_service), tenant_ids_(), idx_(0) {}
  virtual ~ObAllVirtualServerSchemaInfo() {}
public:
  virtual int inner_open();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  void destroy();
private:
  const static int64_t DEFAULT_TENANT_NUM = 10;
  char ip_buffer_[OB_MAX_SERVER_ADDR_SIZE];
  share::schema::ObMultiVersionSchemaService &schema_service_;
  ObSEArray<uint64_t, DEFAULT_TENANT_NUM> tenant_ids_;
  int64_t idx_;
}; //class ObAllVirtualServerSchemaInfo
}//namespace observer
}//namespace oceanbase
#endif //OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SERVER_SCHEMA_INFO_H_
