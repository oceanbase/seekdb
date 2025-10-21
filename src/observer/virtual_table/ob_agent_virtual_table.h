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

#ifndef OCEANBASE_VIRTUAL_TABLE_OB_AGENT_VIRTUAL_TABLE_H_
#define OCEANBASE_VIRTUAL_TABLE_OB_AGENT_VIRTUAL_TABLE_H_


#include "lib/allocator/page_arena.h"
#include "ob_agent_table_base.h"

namespace oceanbase
{
namespace observer
{
// mysql mode system table access agent for oracle tenant.
class ObAgentVirtualTable : public ObAgentTableBase
{
public:
  ObAgentVirtualTable();
  virtual ~ObAgentVirtualTable();

  int init(
      const uint64_t pure_table_id,
      const bool sys_tenant_base_table,
      const share::schema::ObTableSchema *index_table,
      const ObVTableScanParam &scan_param,
      const bool only_sys_data,
      const lib::Worker::CompatMode &mode = lib::Worker::CompatMode::ORACLE);

  virtual int do_open() override;
  virtual int inner_get_next_row(common::ObNewRow *&row) override;

private:
  virtual int set_convert_func(convert_func_t &func,
      const share::schema::ObColumnSchemaV2 &col,
      const share::schema::ObColumnSchemaV2 &base_col) override;

  virtual int init_non_exist_map_item(MapItem &item,
      const share::schema::ObColumnSchemaV2 &col) override;

  virtual int add_extra_condition(common::ObSqlString &sql) override;

  int should_add_tenant_condition(bool &need, const uint64_t tenant_id) const;

private:
  // ID of the regular tenant being proxied before switching sys
  uint64_t general_tenant_id_;
  // agent table Compat Mode
  lib::Worker::CompatMode mode_;
  // Indicates whether this proxy table only queries data under the system tenant
  bool only_sys_data_;
  DISALLOW_COPY_AND_ASSIGN(ObAgentVirtualTable);
};

} // end namespace observer
} // end namespace oceanbase

#endif // OCEANBASE_VIRTUAL_TABLE_OB_AGENT_VIRTUAL_TABLE_H_
