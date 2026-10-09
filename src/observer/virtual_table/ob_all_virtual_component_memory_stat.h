/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_COMPONENT_MEMORY_STAT_H_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_COMPONENT_MEMORY_STAT_H_

#include "lib/resource/ob_memory_quota.h"
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace observer
{

// The five numeric fields in a row are sampled independently.  This table is
// intended for component attribution and trends, not transactional snapshots.
class ComponentMemoryStat : public common::ObVirtualTableScannerIterator
{
public:
  ComponentMemoryStat();
  virtual ~ComponentMemoryStat() = default;

  virtual int inner_get_next_row(common::ObNewRow *&row) override;
  virtual void reset() override;

  static int64_t component_count() { return COMPONENT_COUNT; }
  static const char *component_name(int64_t component_index);

private:
  enum ColumnId
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    COMPONENT_NAME,
    LIMIT_BYTES,
    COMMITTED_BYTES,
    RESERVED_BYTES,
    REJECT_COUNT,
    RECLAIM_COUNT,
  };

  static const int64_t COMPONENT_COUNT = 4;
  int get_component_sample(
      const int64_t component_index,
      const char *&component_name,
      common::MemoryQuotaSample &sample) const;

private:
  int64_t component_index_;
  char ip_buf_[common::OB_IP_STR_BUFF];

  DISALLOW_COPY_AND_ASSIGN(ComponentMemoryStat);
};

} // namespace observer
} // namespace oceanbase

#endif // OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_COMPONENT_MEMORY_STAT_H_
