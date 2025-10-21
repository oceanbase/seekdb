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

#ifndef OCEANBASE_VIRTUAL_TABLE_OB_ITERATE_VIRTUAL_TABLE_H_
#define OCEANBASE_VIRTUAL_TABLE_OB_ITERATE_VIRTUAL_TABLE_H_

#include "ob_agent_table_base.h"
#include "lib/string/ob_sql_string.h"

namespace oceanbase
{
namespace observer
{

// Iterate tables of all tenants
class ObIterateVirtualTable : public ObAgentTableBase
{
public:
  ObIterateVirtualTable();
  virtual ~ObIterateVirtualTable();

  int init(const uint64_t base_table_id,
           const share::schema::ObTableSchema *index_table,
           const ObVTableScanParam &scan_param);

  virtual int do_open() override;
  virtual int inner_get_next_row(common::ObNewRow *&row) override;
  virtual int inner_close() override;
private:
  virtual int init_non_exist_map_item(MapItem &item,
      const share::schema::ObColumnSchemaV2 &col) override;

  virtual int setup_inital_rowkey_condition(
      common::ObSqlString &cols, common::ObSqlString &vals);
  virtual int add_extra_condition(common::ObSqlString &sql) override;

  bool check_tenant_in_range(const uint64_t tenant_id, const common::ObNewRange &range);
  int next_tenant();

  virtual int change_column_value(const MapItem &item,
                                  ObIAllocator &allocator,
                                  ObObj &new_value) override;

  int str_to_int(const ObString &str, int64_t &value);

private:
  int64_t tenant_idx_;
  uint64_t cur_tenant_id_;
  common::ObArray<uint64_t, common::ObWrapperAllocator> tenants_;
  common::ObSqlString sql_;
};

} // end namespace observer
} // end namespace oceanbase

#endif // OCEANBASE_VIRTUAL_TABLE_OB_ITERATE_VIRTUAL_TABLE_H_
