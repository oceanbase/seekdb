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

#ifndef OCEANBASE_SHARE_SCHEMA_GRAPH_SCHEMA_H_
#define OCEANBASE_SHARE_SCHEMA_GRAPH_SCHEMA_H_

#include "lib/utility/utility.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObTableSchema;

// Column IDs, not names, are the durable binding to a base relation. Labels and
// property names describe the graph interface; they are not element identities.
struct GraphElement
{
  OB_UNIS_VERSION(1);
public:
  GraphElement();
  friend int copy_assign(GraphElement &destination, const GraphElement &source)
  { destination = source; return common::OB_SUCCESS; }
  bool is_vertex() const { return source_id_ == common::OB_INVALID_ID; }
  bool references_column(uint64_t column_id) const;
  uint64_t id_;
  uint64_t table_id_;
  common::ObString label_;
  int64_t key_count_;
  uint64_t key_columns_[2];
  uint64_t source_id_;
  uint64_t destination_id_;
  int64_t source_key_count_;
  int64_t destination_key_count_;
  uint64_t source_columns_[2];
  uint64_t destination_columns_[2];
  TO_STRING_KV(K_(id), K_(table_id), K_(label), K_(key_count),
               K_(source_id), K_(destination_id));
};

struct GraphProperty
{
  OB_UNIS_VERSION(1);
public:
  friend int copy_assign(GraphProperty &destination, const GraphProperty &source)
  { destination = source; return common::OB_SUCCESS; }
  GraphProperty() : element_id_(common::OB_INVALID_ID), column_id_(common::OB_INVALID_ID) {}
  uint64_t element_id_;
  uint64_t column_id_;
  common::ObString name_;
  TO_STRING_KV(K_(element_id), K_(column_id), K_(name));
};

class GraphSchema final : public ObSchema
{
  OB_UNIS_VERSION(1);
public:
  GraphSchema();
  explicit GraphSchema(common::ObIAllocator *allocator);
  void reset() override;
  bool is_valid() const override;
  bool is_valid_definition() const;
  int assign(const GraphSchema &other);
  int64_t get_convert_size() const override;
  int set_name(const common::ObString &name) { return deep_copy_str(name, name_); }
  int set_elements(const common::ObIArray<GraphElement> &elements);
  int set_properties(const common::ObIArray<GraphProperty> &properties);
  const GraphElement *get_element(uint64_t id) const;
  const GraphElement *get_element(const common::ObString &label) const;
  bool references_table(uint64_t table_id) const;
  bool references_column(uint64_t table_id, uint64_t column_id) const;

  // The caller supplies all tables from one Schema guard. This validation is
  // also required under the DDL transaction before a definition is published.
  int validate_tables(const common::ObIArray<const ObTableSchema *> &tables) const;

  uint64_t get_graph_id() const { return graph_id_; }
  uint64_t get_database_id() const { return database_id_; }
  uint64_t get_owner_id() const { return owner_id_; }
  int64_t get_schema_version() const { return schema_version_; }
  const common::ObString &get_name() const { return name_; }
  const common::ObIArray<GraphElement> &get_elements() const { return elements_; }
  const common::ObIArray<GraphProperty> &get_properties() const { return properties_; }
  void set_graph_id(uint64_t id) { graph_id_ = id; }
  void set_database_id(uint64_t id) { database_id_ = id; }
  void set_owner_id(uint64_t id) { owner_id_ = id; }
  void set_schema_version(int64_t version) { schema_version_ = version; }
  TO_STRING_KV(K_(graph_id), K_(database_id), K_(owner_id), K_(schema_version),
               K_(name), K_(elements), K_(properties));

private:
  uint64_t graph_id_;
  uint64_t database_id_;
  uint64_t owner_id_;
  int64_t schema_version_;
  common::ObString name_;
  common::ObArrayHelper<GraphElement> elements_;
  common::ObArrayHelper<GraphProperty> properties_;
  DISALLOW_COPY_AND_ASSIGN(GraphSchema);
};
// Graph definitions are immutable once published in a Schema manager. Updates
// allocate a new version from the manager's arena, just like other Schema data.
class GraphMgr final
{
public:
  explicit GraphMgr(common::ObIAllocator &allocator) : allocator_(allocator) {}
  void reset() { graphs_.reset(); }
  int assign(const GraphMgr &other) { return graphs_.assign(other.graphs_); }
  int deep_copy(const GraphMgr &other);
  int add(const GraphSchema &schema);
  int remove(uint64_t graph_id);
  const GraphSchema *get(uint64_t graph_id) const;
  const GraphSchema *get(uint64_t database_id, const common::ObString &name,
                         common::ObNameCaseMode mode) const;
  const common::ObIArray<GraphSchema *> &get_all() const { return graphs_; }
private:
  common::ObIAllocator &allocator_;
  common::ObSEArray<GraphSchema *, 4> graphs_;
  DISALLOW_COPY_AND_ASSIGN(GraphMgr);
};
} // namespace schema
} // namespace share
} // namespace oceanbase
#endif
