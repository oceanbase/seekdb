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

#define USING_LOG_PREFIX SQL_OPT

#include "table_access_targets.h"
#include "sql/das/ob_das_tablet_mapper.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{
TableAccessTargets::TableAccessTargets()
  : table_ref_id_(OB_INVALID_ID),
    ref_table_id_(OB_INVALID_ID),
    tablet_targets_()
{
}

int TableAccessTargets::assign(const TableAccessTargets &other)
{
  int ret = OB_SUCCESS;
  table_ref_id_ = other.table_ref_id_;
  ref_table_id_ = other.ref_table_id_;
  if (OB_FAIL(tablet_targets_.assign(other.tablet_targets_))) {
  }
  return ret;
}

void TableAccessTargets::set_table_identity(uint64_t table_ref_id, uint64_t ref_table_id)
{
  table_ref_id_ = table_ref_id;
  ref_table_id_ = ref_table_id;
}

int TableAccessTargets::replace_local_index_targets(DASRelatedTabletMap &map, ObTableID ref_table_id)
{
  int ret = OB_SUCCESS;
  ref_table_id_ = ref_table_id;
  for (int64_t i = 0; i < tablet_targets_.count(); ++i) {
    TabletAccessTarget &tablet_access = tablet_targets_.at(i);
    const DASRelatedTabletMap::Value *rv = nullptr;
    if (OB_ISNULL(rv = map.get_related_tablet_id(tablet_access.get_tablet_id(), ref_table_id))) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      tablet_access.set_tablet_info(rv->tablet_id_, rv->part_id_, rv->first_level_part_id_);
    }
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
