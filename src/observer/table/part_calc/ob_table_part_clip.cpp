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

#define USING_LOG_PREFIX SERVER
#include "ob_table_part_clip.h"
#include "share/storage_cache_policy/ob_storage_cache_common.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::observer;
using namespace oceanbase::share::schema;
using namespace oceanbase::storage;

namespace oceanbase
{
namespace table
{

int ObTablePartClipper::clip(const ObSimpleTableSchemaV2 &simple_schema,
                             ObTablePartClipType clip_type,
                             const ObIArray<ObTabletID> &src_tablet_ids,
                             ObIArray<ObTabletID> &dst_tablet_id)
{
  int ret = OB_SUCCESS;
  bool table_cache_policy_is_hot = false;
  int64_t part_id = -1;
  int64_t subpart_id = -1;
  ObBasePartition *part = nullptr;

  for (int64_t i = 0; OB_SUCC(ret) && i < src_tablet_ids.count(); i++) {
    bool clip = true;
    const ObTabletID &tablet_id = src_tablet_ids.at(i);
    if (OB_FAIL(simple_schema.get_part_idx_by_tablet(tablet_id, part_id, subpart_id))) {
      LOG_WARN("fail to get part idx", K(ret), K(tablet_id), K(i));
    } else if (OB_FAIL(simple_schema.get_part_by_idx(part_id, subpart_id, part))) {
      LOG_WARN("fail to get part by idx", K(ret), K(tablet_id), K(part_id), K(subpart_id), K(i));
    } else if (OB_ISNULL(part)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("part is null", K(ret), K(tablet_id), K(part_id), K(subpart_id), K(i));
    } else if (clip_type == ObTablePartClipType::HOT_ONLY) {
      const ObStorageCachePolicyType part_policy = part->get_part_storage_cache_policy_type();
      // If partition cache policy is NONE, then use table cache policy
      if (part_policy == ObStorageCachePolicyType::NONE_POLICY) {
        if (table_cache_policy_is_hot) {
          clip = false;
        } else {
          ObSchemaGetterGuard schema_guard;
          const ObTableSchema *table_schema = nullptr;
          const uint64_t table_id = simple_schema.get_table_id();
          ObStorageCachePolicy table_policy;
          if (OB_ISNULL(GCTX.schema_service_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("invalid schema service", K(ret));
          } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(MTL_ID(), schema_guard))) {
            LOG_WARN("fail to get schema guard", K(ret));
          } else if (OB_FAIL(schema_guard.get_table_schema(MTL_ID(), table_id, table_schema))) {
            LOG_WARN("fail to get table schema", K(ret), K(table_id));
          } else if (OB_ISNULL(table_schema) || !table_schema->is_valid()) {
            ret = OB_TABLE_NOT_EXIST;
            LOG_WARN("table not exist", K(ret), K(table_id));
          } else if (OB_FAIL(table_policy.load_from_string(table_schema->get_storage_cache_policy()))) {
            LOG_WARN("fail to load table storage cache policy", K(ret), K(table_schema->get_storage_cache_policy()));
          } else if (table_policy.is_hot_policy()) {
            table_cache_policy_is_hot = true;
            clip = false;
          }
        }
      } else if (part_policy == ObStorageCachePolicyType::HOT_POLICY) {
        clip = false;
      }
    } else {
      clip = false;
    }
    
    if (OB_FAIL(ret)) {
    } else if (!clip && OB_FAIL(dst_tablet_id.push_back(tablet_id))) {
      LOG_WARN("fail to push back tablet id", K(ret), K(tablet_id), K(i));
    }
    LOG_DEBUG("tablet clip", KPC(part), K(tablet_id), K(part_id), K(subpart_id), K(i));
  }

  LOG_DEBUG("clip result", K(clip_type), K(src_tablet_ids), K(dst_tablet_id));

  return ret;
}

} // end namespace table
} // end namespace oceanbase
