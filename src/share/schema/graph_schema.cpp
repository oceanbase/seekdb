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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "share/schema/graph_schema.h"
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_column_schema.h"
#include "share/schema/ob_schema_utils.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
using namespace common;

OB_SERIALIZE_MEMBER(GraphElement, id_, table_id_, label_, key_count_,
                    key_columns_[0], key_columns_[1], source_id_, destination_id_,
                    source_key_count_, destination_key_count_,
                    source_columns_[0], source_columns_[1],
                    destination_columns_[0], destination_columns_[1]);
OB_SERIALIZE_MEMBER(GraphProperty, element_id_, column_id_, name_);

GraphElement::GraphElement()
  : id_(OB_INVALID_ID), table_id_(OB_INVALID_ID), key_count_(0),
    key_columns_{OB_INVALID_ID, OB_INVALID_ID}, source_id_(OB_INVALID_ID),
    destination_id_(OB_INVALID_ID), source_key_count_(0), destination_key_count_(0),
    source_columns_{OB_INVALID_ID, OB_INVALID_ID},
    destination_columns_{OB_INVALID_ID, OB_INVALID_ID}
{}

bool GraphElement::references_column(uint64_t column_id) const
{
  bool found = false;
  for (int64_t i = 0; i < 2; ++i) {
    found |= (i < key_count_ && key_columns_[i] == column_id)
          || (i < source_key_count_ && source_columns_[i] == column_id)
          || (i < destination_key_count_ && destination_columns_[i] == column_id);
  }
  return found;
}

GraphSchema::GraphSchema() : ObSchema() { reset(); }
GraphSchema::GraphSchema(ObIAllocator *allocator) : ObSchema(allocator) { reset(); }

void GraphSchema::reset()
{
  elements_.reset();
  properties_.reset();
  name_.reset();
  graph_id_ = database_id_ = define_user_id_ = OB_INVALID_ID;
  schema_version_ = OB_INVALID_VERSION;
  ObSchema::reset();
}

const GraphElement *GraphSchema::get_element(uint64_t id) const
{
  const GraphElement *result = nullptr;
  for (int64_t i = 0; result == nullptr && i < elements_.count(); ++i) {
    if (elements_.at(i).id_ == id) { result = &elements_.at(i); }
  }
  return result;
}

const GraphElement *GraphSchema::get_element(const ObString &label) const
{
  const GraphElement *result = nullptr;
  for (int64_t i = 0; result == nullptr && i < elements_.count(); ++i) {
    if (elements_.at(i).label_.case_compare(label) == 0) { result = &elements_.at(i); }
  }
  return result;
}

bool GraphSchema::is_valid() const
{
  return graph_id_ != OB_INVALID_ID && schema_version_ > 0 && is_valid_definition();
}

bool GraphSchema::is_valid_definition() const
{
  bool valid = ObSchema::is_valid() && database_id_ != OB_INVALID_ID
      && define_user_id_ != OB_INVALID_ID && !name_.empty() && !elements_.empty();
  bool has_vertex = false;
  for (int64_t i = 0; valid && i < elements_.count(); ++i) {
    const GraphElement &e = elements_.at(i);
    valid = e.id_ != OB_INVALID_ID && e.table_id_ != OB_INVALID_ID
        && !e.label_.empty() && e.key_count_ >= 1 && e.key_count_ <= 2;
    for (int64_t j = 0; valid && j < i; ++j) {
      const GraphElement &prev = elements_.at(j);
      valid = e.id_ != prev.id_ && e.table_id_ != prev.table_id_
          && e.label_.case_compare(prev.label_) != 0;
    }
    for (int64_t k = 0; valid && k < e.key_count_; ++k) {
      valid = e.key_columns_[k] != OB_INVALID_ID
          && (k == 0 || e.key_columns_[k] != e.key_columns_[0]);
    }
    if (e.is_vertex()) {
      has_vertex = true;
      valid = valid && e.destination_id_ == OB_INVALID_ID
          && e.source_key_count_ == 0 && e.destination_key_count_ == 0;
    } else {
      const GraphElement *source = get_element(e.source_id_);
      const GraphElement *dest = get_element(e.destination_id_);
      valid = valid && source != nullptr && dest != nullptr
          && source->is_vertex() && dest->is_vertex()
          && e.source_key_count_ >= 1 && e.source_key_count_ <= 2
          && e.destination_key_count_ >= 1 && e.destination_key_count_ <= 2
          && e.source_key_count_ == source->key_count_
          && e.destination_key_count_ == dest->key_count_;
      for (int64_t k = 0; valid && k < e.source_key_count_; ++k) {
        valid = e.source_columns_[k] != OB_INVALID_ID
            && (k == 0 || e.source_columns_[k] != e.source_columns_[0]);
      }
      for (int64_t k = 0; valid && k < e.destination_key_count_; ++k) {
        valid = e.destination_columns_[k] != OB_INVALID_ID
            && (k == 0 || e.destination_columns_[k] != e.destination_columns_[0]);
      }
    }
  }
  for (int64_t i = 0; valid && i < properties_.count(); ++i) {
    const GraphProperty &p = properties_.at(i);
    valid = get_element(p.element_id_) != nullptr
        && p.column_id_ != OB_INVALID_ID && !p.name_.empty();
    for (int64_t j = 0; valid && j < i; ++j) {
      const GraphProperty &prev = properties_.at(j);
      valid = p.element_id_ != prev.element_id_
          || (p.name_.case_compare(prev.name_) != 0 && p.column_id_ != prev.column_id_);
    }
  }
  return valid && has_vertex;
}

int GraphSchema::set_elements(const ObIArray<GraphElement> &elements)
{
  int ret = OB_SUCCESS;
  if (&elements != &elements_) {
    GraphElement *buffer = nullptr;
    if (elements.count() > 0 && nullptr == (buffer = static_cast<GraphElement *>(
        alloc(sizeof(GraphElement) * elements.count())))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < elements.count(); ++i) {
      new (buffer + i) GraphElement(elements.at(i));
      ret = deep_copy_str(elements.at(i).label_, buffer[i].label_);
    }
    if (OB_SUCC(ret)) { elements_.init(elements.count(), buffer, elements.count()); }
  }
  error_ret_ = ret;
  return ret;
}

int GraphSchema::set_properties(const ObIArray<GraphProperty> &properties)
{
  int ret = OB_SUCCESS;
  if (&properties != &properties_) {
    GraphProperty *buffer = nullptr;
    if (properties.count() > 0 && nullptr == (buffer = static_cast<GraphProperty *>(
        alloc(sizeof(GraphProperty) * properties.count())))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < properties.count(); ++i) {
      new (buffer + i) GraphProperty(properties.at(i));
      ret = deep_copy_str(properties.at(i).name_, buffer[i].name_);
    }
    if (OB_SUCC(ret)) { properties_.init(properties.count(), buffer, properties.count()); }
  }
  error_ret_ = ret;
  return ret;
}

int GraphSchema::assign(const GraphSchema &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    reset();
    graph_id_ = other.graph_id_;
    database_id_ = other.database_id_;
    define_user_id_ = other.define_user_id_;
    schema_version_ = other.schema_version_;
    if (OB_FAIL(set_name(other.name_))) {
    }
    if (OB_SUCC(ret)) { ret = set_elements(other.elements_); }
    if (OB_SUCC(ret)) { ret = set_properties(other.properties_); }
    error_ret_ = ret;
  }
  return ret;
}

int64_t GraphSchema::get_convert_size() const
{
  int64_t size = sizeof(*this) + name_.length() + 1;
  for (int64_t i = 0; i < elements_.count(); ++i) {
    size += sizeof(GraphElement) + elements_.at(i).label_.length() + 1;
  }
  for (int64_t i = 0; i < properties_.count(); ++i) {
    size += sizeof(GraphProperty) + properties_.at(i).name_.length() + 1;
  }
  return size;
}

bool GraphSchema::references_table(uint64_t table_id) const
{
  bool found = false;
  for (int64_t i = 0; !found && i < elements_.count(); ++i) {
    found = elements_.at(i).table_id_ == table_id;
  }
  return found;
}

bool GraphSchema::references_column(uint64_t table_id, uint64_t column_id) const
{
  bool found = false;
  for (int64_t i = 0; !found && i < elements_.count(); ++i) {
    const GraphElement &e = elements_.at(i);
    if (e.table_id_ == table_id) {
      found = e.references_column(column_id);
      for (int64_t j = 0; !found && j < properties_.count(); ++j) {
        found = properties_.at(j).element_id_ == e.id_
            && properties_.at(j).column_id_ == column_id;
      }
    }
  }
  return found;
}

namespace
{
const ObTableSchema *find_graph_base_table(
    const ObIArray<const ObTableSchema *> &tables, uint64_t id)
{
  const ObTableSchema *result = nullptr;
  for (int64_t i = 0; result == nullptr && i < tables.count(); ++i) {
    if (tables.at(i) != nullptr && tables.at(i)->get_table_id() == id) {
      result = tables.at(i);
    }
  }
  return result;
}
}

int GraphSchema::validate_tables(const ObIArray<const ObTableSchema *> &tables) const
{
  int ret = is_valid_definition() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
  for (int64_t i = 0; OB_SUCC(ret) && i < elements_.count(); ++i) {
    const GraphElement &e = elements_.at(i);
    const ObTableSchema *table = find_graph_base_table(tables, e.table_id_);
    if (table == nullptr || table->get_database_id() != database_id_
        || !table->is_user_table() || table->is_ctas_tmp_table()
        || table->is_offline_ddl_table() || table->is_user_hidden_table()) {
      ret = OB_NOT_SUPPORTED;
    } else if (table->get_rowkey_info().get_size() != e.key_count_) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      for (int64_t k = 0; OB_SUCC(ret) && k < e.key_count_; ++k) {
        uint64_t pk = OB_INVALID_ID;
        const ObColumnSchemaV2 *col = table->get_column_schema(e.key_columns_[k]);
        if (OB_FAIL(table->get_rowkey_info().get_column_id(k, pk))) {
        } else if (pk != e.key_columns_[k] || col == nullptr || col->is_hidden()
                   || col->is_generated_column() || col->is_nullable()
                   || col->get_data_type() != ObIntType) {
          ret = OB_NOT_SUPPORTED;
        }
      }
      for (int direction = 0; OB_SUCC(ret) && !e.is_vertex() && direction < 2; ++direction) {
        const int64_t count = direction == 0 ? e.source_key_count_ : e.destination_key_count_;
        const uint64_t *cols = direction == 0 ? e.source_columns_ : e.destination_columns_;
        for (int64_t k = 0; OB_SUCC(ret) && k < count; ++k) {
          const ObColumnSchemaV2 *col = table->get_column_schema(cols[k]);
          if (col == nullptr || col->is_hidden() || col->is_generated_column()
              || col->get_data_type() != ObIntType) {
            ret = OB_NOT_SUPPORTED;
          }
        }
      }
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < properties_.count(); ++i) {
    const GraphProperty &p = properties_.at(i);
    const GraphElement *e = get_element(p.element_id_);
    const ObTableSchema *table = find_graph_base_table(tables, e->table_id_);
    const ObColumnSchemaV2 *col = table->get_column_schema(p.column_id_);
    if (col == nullptr || col->is_hidden() || col->is_generated_column()
        || col->get_column_name_str().case_compare(p.name_) != 0) {
      ret = OB_NOT_SUPPORTED;
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < i; ++j) {
      const GraphProperty &prev = properties_.at(j);
      if (p.name_.case_compare(prev.name_) == 0) {
        const ObTableSchema *prev_table = find_graph_base_table(
            tables, get_element(prev.element_id_)->table_id_);
        const ObColumnSchemaV2 *prev_col = prev_table->get_column_schema(prev.column_id_);
        if (col->get_meta_type() != prev_col->get_meta_type()
            || col->get_accuracy() != prev_col->get_accuracy()
            || col->get_extended_type_info().count() != prev_col->get_extended_type_info().count()) {
          ret = OB_INVALID_ARGUMENT;
        }
        for (int64_t k = 0; OB_SUCC(ret) && k < col->get_extended_type_info().count(); ++k) {
          if (col->get_extended_type_info().at(k) != prev_col->get_extended_type_info().at(k)) {
            ret = OB_INVALID_ARGUMENT;
          }
        }
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE(GraphSchema)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, graph_id_, database_id_, define_user_id_, schema_version_,
              name_);
  OB_UNIS_ENCODE(elements_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < elements_.count(); ++i) {
    OB_UNIS_ENCODE(elements_.at(i));
  }
  OB_UNIS_ENCODE(properties_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < properties_.count(); ++i) {
    OB_UNIS_ENCODE(properties_.at(i));
  }
  return ret;
}

OB_DEF_DESERIALIZE(GraphSchema)
{
  int ret = OB_SUCCESS;
  GraphSchema decoded;
  ObSEArray<GraphElement, 4> elements;
  ObSEArray<GraphProperty, 8> properties;
  LST_DO_CODE(OB_UNIS_DECODE, decoded.graph_id_, decoded.database_id_, decoded.define_user_id_,
              decoded.schema_version_, decoded.name_);
  int64_t element_count = 0;
  int64_t property_count = 0;
  OB_UNIS_DECODE(element_count);
  if (OB_SUCC(ret) && (element_count <= 0 || element_count > data_len - pos)) {
    ret = OB_DESERIALIZE_ERROR;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < element_count; ++i) {
    GraphElement element;
    OB_UNIS_DECODE(element);
    if (OB_SUCC(ret)) { ret = elements.push_back(element); }
  }
  OB_UNIS_DECODE(property_count);
  if (OB_SUCC(ret) && (property_count < 0 || property_count > data_len - pos)) {
    ret = OB_DESERIALIZE_ERROR;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < property_count; ++i) {
    GraphProperty property;
    OB_UNIS_DECODE(property);
    if (OB_SUCC(ret)) { ret = properties.push_back(property); }
  }
  if (OB_SUCC(ret)) { ret = decoded.set_elements(elements); }
  if (OB_SUCC(ret)) { ret = decoded.set_properties(properties); }
  if (OB_SUCC(ret)) {
    if (!decoded.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ret = assign(decoded);
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(GraphSchema)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, graph_id_, database_id_, define_user_id_, schema_version_,
              name_);
  OB_UNIS_ADD_LEN(elements_.count());
  for (int64_t i = 0; i < elements_.count(); ++i) { OB_UNIS_ADD_LEN(elements_.at(i)); }
  OB_UNIS_ADD_LEN(properties_.count());
  for (int64_t i = 0; i < properties_.count(); ++i) { OB_UNIS_ADD_LEN(properties_.at(i)); }
  return len;
}
int GraphMgr::deep_copy(const GraphMgr &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    reset();
    for (int64_t i = 0; OB_SUCC(ret) && i < other.graphs_.count(); ++i) {
      ret = add(*other.graphs_.at(i));
    }
  }
  return ret;
}

int GraphMgr::add(const GraphSchema &schema)
{
  int ret = OB_SUCCESS;
  GraphSchema *copy = nullptr;
  if (!schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator_, schema, copy))) {
  } else {
    int64_t index = 0;
    while (index < graphs_.count() && graphs_.at(index)->get_graph_id() != schema.get_graph_id()) {
      ++index;
    }
    if (index == graphs_.count()) { ret = graphs_.push_back(copy); }
    else { graphs_.at(index) = copy; }
  }
  return ret;
}

int GraphMgr::remove(uint64_t graph_id)
{
  int ret = OB_ENTRY_NOT_EXIST;
  for (int64_t i = 0; i < graphs_.count(); ++i) {
    if (graphs_.at(i)->get_graph_id() == graph_id) {
      ret = graphs_.remove(i);
      break;
    }
  }
  return ret;
}

const GraphSchema *GraphMgr::get(uint64_t graph_id) const
{
  const GraphSchema *result = nullptr;
  for (int64_t i = 0; result == nullptr && i < graphs_.count(); ++i) {
    if (graphs_.at(i)->get_graph_id() == graph_id) { result = graphs_.at(i); }
  }
  return result;
}

const GraphSchema *GraphMgr::get(uint64_t database_id, const ObString &name,
                                  ObNameCaseMode mode) const
{
  const GraphSchema *result = nullptr;
  ObSchemaNameComparator comparator(mode, database_id);
  for (int64_t i = 0; result == nullptr && i < graphs_.count(); ++i) {
    if (graphs_.at(i)->get_database_id() == database_id
        && comparator.compare(graphs_.at(i)->get_name(), name) == 0) {
      result = graphs_.at(i);
    }
  }
  return result;
}
} // namespace schema
} // namespace share
} // namespace oceanbase
