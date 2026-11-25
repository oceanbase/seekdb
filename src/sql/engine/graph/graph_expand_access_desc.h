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

#pragma once

#include "lib/container/ob_array_wrap.h"
#include "lib/utility/ob_unify_serialize.h"
#include "sql/engine/graph/graph_path_spec.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
}
}
namespace sql
{

// Physical table/column binding for one GraphExpand hop. Endpoint columns are
// normalized to the traversal direction: edge_current_columns_ join the input
// frontier, while edge_next_columns_ produce target vertex identities. The
// selected edge access table is either edge_table_id_ itself or a readable
// local index whose leading columns are edge_current_columns_.
struct GraphExpandAccessDesc
{
  OB_UNIS_VERSION(2);
public:
  bool is_valid() const;
  bool uses_adjacency_index() const
  { return edge_access_table_id_ != edge_table_id_; }
  uint64_t hash(uint64_t seed = 0) const;

  uint64_t source_table_id_{common::OB_INVALID_ID};
  int64_t source_table_version_{0};
  uint64_t edge_table_id_{common::OB_INVALID_ID};
  int64_t edge_table_version_{0};
  uint64_t target_table_id_{common::OB_INVALID_ID};
  int64_t target_table_version_{0};
  uint64_t edge_access_table_id_{common::OB_INVALID_ID};
  int64_t edge_access_table_version_{0};
  int64_t source_key_count_{0};
  int64_t edge_key_count_{0};
  int64_t target_key_count_{0};
  uint64_t source_key_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
  uint64_t edge_key_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
  uint64_t target_key_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
  uint64_t edge_current_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
  uint64_t edge_next_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
  TO_STRING_KV(K_(source_table_id), K_(source_table_version),
               K_(edge_table_id), K_(edge_table_version),
               K_(target_table_id), K_(target_table_version),
               K_(edge_access_table_id), K_(edge_access_table_version),
               K_(source_key_count), K_(edge_key_count), K_(target_key_count),
               "source_keys", common::ObArrayWrap<uint64_t>(source_key_columns_, source_key_count_),
               "edge_keys", common::ObArrayWrap<uint64_t>(edge_key_columns_, edge_key_count_),
               "target_keys", common::ObArrayWrap<uint64_t>(target_key_columns_, target_key_count_),
               "edge_current", common::ObArrayWrap<uint64_t>(edge_current_columns_, source_key_count_),
               "edge_next", common::ObArrayWrap<uint64_t>(edge_next_columns_, target_key_count_));
};

// Resolves schema bindings and validates the already-selected access table.
// Endpoint columns are normalized to path direction. On failure, output is
// unchanged; no schema objects are retained in the resulting value descriptor.
int build_graph_expand_access_desc(
    const GraphPathDesc &path_desc, uint64_t edge_access_table_id,
    share::schema::ObSchemaGetterGuard &schema_guard, GraphExpandAccessDesc &output);

} // namespace sql
} // namespace oceanbase
