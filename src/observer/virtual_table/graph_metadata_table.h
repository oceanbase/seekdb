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

#pragma once

#include "observer/virtual_table/ob_virtual_table_iterator.h"
#include "share/schema/graph_schema.h"

namespace oceanbase
{
namespace observer
{
// A row-wise metadata scan, not a scan of graph data. Each public view has its
// own shape; no public identity depends on the internal table/column IDs.
class GraphMetadataTable final : public common::ObVirtualTableIterator
{
public:
  enum class Kind { GRAPHS, ELEMENTS, KEYS, EDGE_RELATIONSHIPS, LABELS,
                    ELEMENT_LABELS, LABEL_PROPERTIES, PROPERTY_DEFINITIONS };
  void set_kind(Kind kind) { kind_ = kind; }
  int inner_open() override;
  int inner_get_next_row(common::ObNewRow *&row) override;
  void reset() override;

private:
  int fill_row(const share::schema::GraphSchema &graph,
               const share::schema::ObDatabaseSchema &database);
  int fill_type(const share::schema::ObColumnSchemaV2 &column);
  int put(const char *name, const common::ObObj &value);
  int put_string(const char *name, const common::ObString &value);
  int put_number(const char *name, uint64_t value);
  void next_graph();
  int matches_range(const common::ObString &database, const common::ObString &graph, bool &matches);

  Kind kind_ = Kind::GRAPHS;
  common::ObArray<const share::schema::GraphSchema *> graphs_{};
  int64_t graph_pos_ = 0;
  int64_t element_pos_ = 0;
  int64_t detail_pos_ = 0;
  int64_t endpoint_pos_ = 0;
  bool access_checked_ = false;
  common::ObArenaAllocator row_allocator_{"GraphMetadata"};
};
} // namespace observer
} // namespace oceanbase
