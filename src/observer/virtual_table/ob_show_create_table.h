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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_SHOW_CREATE_TABLE_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_SHOW_CREATE_TABLE_

#include "lib/container/ob_se_array.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "common/ob_range.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace share
{
namespace schema
{
class ObTableSchema;
}
}
namespace observer
{
class ObShowCreateTable : public common::ObVirtualTableScannerIterator
{
public:
  ObShowCreateTable();
  virtual ~ObShowCreateTable();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  int calc_show_table_id(uint64_t &show_table_id);
  int fill_row_cells_with_retry(const uint64_t show_table_id,
                                const share::schema::ObTableSchema &table_schema);
  int fill_row_cells_inner(const uint64_t show_table_id,
                           const share::schema::ObTableSchema &table_schema,
                           const int64_t table_def_buf_size,
                           char *table_def_buf);
private:
  DISALLOW_COPY_AND_ASSIGN(ObShowCreateTable);
};
}// observer
}// oceanbase
#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_SHOW_CREATE_TABLE_ */
