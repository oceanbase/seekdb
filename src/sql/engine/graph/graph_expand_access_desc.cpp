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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/graph/graph_expand_access_desc.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/schema/graph_schema.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{
namespace
{

bool valid_graph_key_count(int64_t count)
{
  return count > 0 && count <= OB_USER_MAX_ROWKEY_COLUMN_NUMBER;
}

bool valid_graph_columns(const uint64_t *columns, int64_t count)
{
  bool valid = columns != nullptr && valid_graph_key_count(count);
  for (int64_t i = 0; valid && i < count; ++i) {
    valid = columns[i] != OB_INVALID_ID;
  }
  return valid;
}

void copy_graph_columns(const uint64_t *source,
                        int64_t count,
                        uint64_t *destination)
{
  for (int64_t i = 0; i < count; ++i) {
    destination[i] = source[i];
  }
}

bool index_has_adjacency_prefix(const ObTableSchema &index_schema,
                                const uint64_t *columns,
                                int64_t column_count)
{
  const ObIndexInfo &index_columns = index_schema.get_index_info();
  bool matches = valid_graph_columns(columns, column_count)
      && index_columns.get_size() >= column_count;
  for (int64_t i = 0; matches && i < column_count; ++i) {
    const ObIndexColumn *column = index_columns.get_column(i);
    matches = column != nullptr && column->column_id_ == columns[i];
  }
  return matches;
}

} // namespace

bool GraphExpandAccessDesc::is_valid() const
{
  return source_table_id_ != OB_INVALID_ID && source_table_version_ > 0
      && edge_table_id_ != OB_INVALID_ID && edge_table_version_ > 0
      && target_table_id_ != OB_INVALID_ID && target_table_version_ > 0
      && edge_access_table_id_ != OB_INVALID_ID
      && edge_access_table_version_ > 0
      && valid_graph_columns(source_key_columns_, source_key_count_)
      && valid_graph_columns(edge_key_columns_, edge_key_count_)
      && valid_graph_columns(target_key_columns_, target_key_count_)
      && valid_graph_columns(edge_current_columns_, source_key_count_)
      && valid_graph_columns(edge_next_columns_, target_key_count_);
}

bool supports_basic_native_graph_feedback(
    const GraphPathDesc &path_desc,
    const GraphExpandAccessDesc &access_desc,
    int64_t output_expr_count,
    int64_t payload_expr_count)
{
  bool supported = path_desc.is_valid() && access_desc.is_valid();
  if (supported) {
    const int64_t fixed_output_count = 1 + access_desc.source_key_count_
        + access_desc.target_key_count_
        + (path_desc.needs_edge_key_history()
           ? path_desc.upper_bound_ * access_desc.edge_key_count_ : 0);
    supported = path_desc.direction_ != GraphPathDirection::BOTH
        && path_desc.source_element_id_ == path_desc.target_element_id_
        && path_desc.path_mode_ == GraphPathMode::WALK
        && (!path_desc.need_path_ || payload_expr_count > 0
            || path_desc.row_shape_ == GraphPathRowShape::PER_STEP)
        && access_desc.source_table_id_ == access_desc.target_table_id_
        && payload_expr_count >= 0
        && output_expr_count == fixed_output_count + payload_expr_count;
  }
  return supported;
}

int build_graph_expand_access_desc(const GraphPathDesc &path_desc,
                                   uint64_t edge_access_table_id,
                                   ObSchemaGetterGuard &schema_guard,
                                   GraphExpandAccessDesc &output)
{
  int ret = OB_SUCCESS;
  GraphExpandAccessDesc desc;
  const GraphSchema *graph = nullptr;
  const GraphElement *source = nullptr;
  const GraphElement *edge = nullptr;
  const GraphElement *target = nullptr;
  const ObTableSchema *source_table = nullptr;
  const ObTableSchema *edge_table = nullptr;
  const ObTableSchema *target_table = nullptr;
  const ObTableSchema *edge_access_table = nullptr;
  const uint64_t *edge_current_columns = nullptr;
  const uint64_t *edge_next_columns = nullptr;
  int64_t edge_current_count = 0;
  int64_t edge_next_count = 0;

  if (OB_UNLIKELY(!path_desc.is_valid()
                  || edge_access_table_id == OB_INVALID_ID)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph expand access arguments", K(ret), K(path_desc),
             K(edge_access_table_id));
  } else if (OB_FAIL(schema_guard.get_graph_schema(path_desc.graph_id_, graph))) {
    LOG_WARN("failed to get graph schema", K(ret), K(path_desc));
  } else if (OB_ISNULL(graph)) {
    ret = OB_SCHEMA_ERROR;
    LOG_WARN("graph schema is missing", K(ret), K(path_desc));
  } else if (OB_UNLIKELY(graph->get_schema_version()
                         != path_desc.graph_version_)) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("graph schema version changed", K(ret), K(path_desc),
             "current_version", graph->get_schema_version());
  } else if (OB_ISNULL(source = graph->get_element(path_desc.source_element_id_))
             || OB_ISNULL(edge = graph->get_element(path_desc.edge_element_id_))
             || OB_ISNULL(target = graph->get_element(path_desc.target_element_id_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph path element mapping is missing", K(ret), K(path_desc),
             KPC(graph));
  } else if (OB_UNLIKELY(!source->is_vertex() || edge->is_vertex()
                         || !target->is_vertex())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph path element kind is inconsistent", K(ret), K(path_desc),
             KPC(source), KPC(edge), KPC(target));
  } else {
    const bool reverse = path_desc.direction_ == GraphPathDirection::IN;
    const uint64_t current_element_id = reverse ? edge->destination_id_
                                                : edge->source_id_;
    const uint64_t next_element_id = reverse ? edge->source_id_
                                             : edge->destination_id_;
    edge_current_columns = reverse ? edge->destination_columns_
                                   : edge->source_columns_;
    edge_next_columns = reverse ? edge->source_columns_
                                : edge->destination_columns_;
    edge_current_count = reverse ? edge->destination_key_count_
                                 : edge->source_key_count_;
    edge_next_count = reverse ? edge->source_key_count_
                              : edge->destination_key_count_;
    if (OB_UNLIKELY(current_element_id != source->id_
                    || next_element_id != target->id_
                    || edge_current_count != source->key_count_
                    || edge_next_count != target->key_count_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph edge endpoints do not match the path", K(ret),
               K(path_desc), KPC(source), KPC(edge), KPC(target));
    }
  }
  if (OB_SUCC(ret)
      && (OB_FAIL(schema_guard.get_table_schema(source->table_id_, source_table))
          || OB_FAIL(schema_guard.get_table_schema(edge->table_id_, edge_table))
          || OB_FAIL(schema_guard.get_table_schema(target->table_id_, target_table))
          || OB_FAIL(schema_guard.get_table_schema(edge_access_table_id,
                                                   edge_access_table)))) {
    LOG_WARN("failed to get graph expand table schema", K(ret), K(path_desc),
             K(edge_access_table_id));
  } else if (OB_SUCC(ret)
             && (OB_ISNULL(source_table) || OB_ISNULL(edge_table)
                 || OB_ISNULL(target_table) || OB_ISNULL(edge_access_table))) {
    ret = OB_SCHEMA_ERROR;
    LOG_WARN("graph expand table schema is missing", K(ret), K(path_desc),
             K(edge_access_table_id), KPC(source_table), KPC(edge_table),
             KPC(target_table), KPC(edge_access_table));
  } else if (OB_SUCC(ret) && edge_access_table_id != edge->table_id_
             && OB_UNLIKELY((!edge_access_table->is_normal_index()
                             && !edge_access_table->is_unique_index())
                            || edge_access_table->is_global_index_table()
                            || edge_access_table->get_data_table_id()
                                   != edge->table_id_
                            || !edge_access_table->can_read_index()
                            || !edge_access_table->is_index_visible()
                            || edge_access_table->is_final_invalid_index()
                            || !index_has_adjacency_prefix(
                                   *edge_access_table, edge_current_columns,
                                   edge_current_count))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid graph adjacency access table", K(ret), K(path_desc),
             K(edge_access_table_id), KPC(edge_access_table));
  }
  if (OB_SUCC(ret)) {
    desc.source_table_id_ = source->table_id_;
    desc.source_table_version_ = source_table->get_schema_version();
    desc.edge_table_id_ = edge->table_id_;
    desc.edge_table_version_ = edge_table->get_schema_version();
    desc.target_table_id_ = target->table_id_;
    desc.target_table_version_ = target_table->get_schema_version();
    desc.edge_access_table_id_ = edge_access_table_id;
    desc.edge_access_table_version_ = edge_access_table->get_schema_version();
    desc.source_key_count_ = source->key_count_;
    desc.edge_key_count_ = edge->key_count_;
    desc.target_key_count_ = target->key_count_;
    copy_graph_columns(source->key_columns_, source->key_count_,
                       desc.source_key_columns_);
    copy_graph_columns(edge->key_columns_, edge->key_count_,
                       desc.edge_key_columns_);
    copy_graph_columns(target->key_columns_, target->key_count_,
                       desc.target_key_columns_);
    copy_graph_columns(edge_current_columns, edge_current_count,
                       desc.edge_current_columns_);
    copy_graph_columns(edge_next_columns, edge_next_count,
                       desc.edge_next_columns_);
    if (OB_UNLIKELY(!desc.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("generated graph expand access descriptor is invalid", K(ret),
               K(desc), K(path_desc));
    } else {
      output = desc;
    }
  }
  return ret;
}

uint64_t GraphExpandAccessDesc::hash(uint64_t seed) const
{
  seed = do_hash(source_table_id_, seed);
  seed = do_hash(source_table_version_, seed);
  seed = do_hash(edge_table_id_, seed);
  seed = do_hash(edge_table_version_, seed);
  seed = do_hash(target_table_id_, seed);
  seed = do_hash(target_table_version_, seed);
  seed = do_hash(edge_access_table_id_, seed);
  seed = do_hash(edge_access_table_version_, seed);
  seed = do_hash(source_key_count_, seed);
  seed = do_hash(edge_key_count_, seed);
  seed = do_hash(target_key_count_, seed);
  for (int64_t i = 0; i < source_key_count_; ++i) {
    seed = do_hash(source_key_columns_[i], seed);
    seed = do_hash(edge_current_columns_[i], seed);
  }
  for (int64_t i = 0; i < edge_key_count_; ++i) {
    seed = do_hash(edge_key_columns_[i], seed);
  }
  for (int64_t i = 0; i < target_key_count_; ++i) {
    seed = do_hash(target_key_columns_[i], seed);
    seed = do_hash(edge_next_columns_[i], seed);
  }
  return seed;
}

OB_DEF_SERIALIZE(GraphExpandAccessDesc)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              source_table_id_, source_table_version_,
              edge_table_id_, edge_table_version_,
              target_table_id_, target_table_version_,
              edge_access_table_id_, edge_access_table_version_,
              source_key_count_, edge_key_count_, target_key_count_);
  for (int64_t i = 0; OB_SUCC(ret) && i < source_key_count_; ++i) {
    OB_UNIS_ENCODE(source_key_columns_[i]);
    OB_UNIS_ENCODE(edge_current_columns_[i]);
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < edge_key_count_; ++i) {
    OB_UNIS_ENCODE(edge_key_columns_[i]);
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < target_key_count_; ++i) {
    OB_UNIS_ENCODE(target_key_columns_[i]);
    OB_UNIS_ENCODE(edge_next_columns_[i]);
  }
  return ret;
}

OB_DEF_DESERIALIZE(GraphExpandAccessDesc)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              source_table_id_, source_table_version_,
              edge_table_id_, edge_table_version_,
              target_table_id_, target_table_version_,
              edge_access_table_id_, edge_access_table_version_,
              source_key_count_, edge_key_count_, target_key_count_);
  if (OB_SUCC(ret)
      && (!valid_graph_key_count(source_key_count_)
          || !valid_graph_key_count(edge_key_count_)
          || !valid_graph_key_count(target_key_count_))) {
    ret = OB_DESERIALIZE_ERROR;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < source_key_count_; ++i) {
    OB_UNIS_DECODE(source_key_columns_[i]);
    OB_UNIS_DECODE(edge_current_columns_[i]);
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < edge_key_count_; ++i) {
    OB_UNIS_DECODE(edge_key_columns_[i]);
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < target_key_count_; ++i) {
    OB_UNIS_DECODE(target_key_columns_[i]);
    OB_UNIS_DECODE(edge_next_columns_[i]);
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(GraphExpandAccessDesc)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              source_table_id_, source_table_version_,
              edge_table_id_, edge_table_version_,
              target_table_id_, target_table_version_,
              edge_access_table_id_, edge_access_table_version_,
              source_key_count_, edge_key_count_, target_key_count_);
  for (int64_t i = 0; i < source_key_count_; ++i) {
    OB_UNIS_ADD_LEN(source_key_columns_[i]);
    OB_UNIS_ADD_LEN(edge_current_columns_[i]);
  }
  for (int64_t i = 0; i < edge_key_count_; ++i) {
    OB_UNIS_ADD_LEN(edge_key_columns_[i]);
  }
  for (int64_t i = 0; i < target_key_count_; ++i) {
    OB_UNIS_ADD_LEN(target_key_columns_[i]);
    OB_UNIS_ADD_LEN(edge_next_columns_[i]);
  }
  return len;
}

} // namespace sql
} // namespace oceanbase
