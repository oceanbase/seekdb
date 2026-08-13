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

#ifndef OCEANBASE_QUERY_API_OPTIMIZER_STAT_OB_OPTIMIZER_STAT_SERVICE_H_
#define OCEANBASE_QUERY_API_OPTIMIZER_STAT_OB_OPTIMIZER_STAT_SERVICE_H_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
template <typename T> class ObIArray;
}
namespace share
{
namespace schema
{
class ObTableSchema;
}
}
namespace query
{

struct ObOptimizerTabletSize
{
  TO_STRING_KV(K_(tablet_id), K_(size));
  uint64_t tablet_id_;
  uint64_t size_;
};

// Query-owned optimizer-statistics capabilities used by the data plane.
// Manager and cache types remain private to the query implementation.
class ObOptimizerStatService
{
public:
  static int report_dml_stat(
      uint64_t table_id,
      int64_t tablet_id,
      int64_t inserted_rows,
      int64_t updated_rows,
      int64_t deleted_rows);

  static int estimate_table_size(
      uint64_t table_id,
      int64_t partition_id,
      int64_t &table_size);

  static int estimate_index_table_size(
      common::ObMySQLProxy *sql_proxy,
      const share::schema::ObTableSchema *table_schema,
      const common::ObIArray<int64_t> &partition_ids,
      const common::ObIArray<uint64_t> &column_ids,
      common::ObIArray<uint64_t> &table_sizes);

  static int get_each_tablet_size(
      common::ObMySQLProxy *sql_proxy,
      const share::schema::ObTableSchema *table_schema,
      common::ObIArray<ObOptimizerTabletSize> &tablet_sizes);
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_OPTIMIZER_STAT_OB_OPTIMIZER_STAT_SERVICE_H_
