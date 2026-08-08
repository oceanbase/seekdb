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

#ifndef OCEANBASE_DATA_PLANE_VECTOR_OB_VECTOR_INDEX_SCHEMA_H_
#define OCEANBASE_DATA_PLANE_VECTOR_OB_VECTOR_INDEX_SCHEMA_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObString;
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
class ObTableSchema;
}
}
namespace data_plane
{

// Resolve the data table and parameter-bearing auxiliary table behind a
// vector-index DDL schema. Observer keeps the vector implementation details;
// Storage receives only the build inputs it owns.
int resolve_vector_index_build_schema(
    share::schema::ObSchemaGetterGuard &schema_guard,
    const share::schema::ObTableSchema &index_table_schema,
    const share::schema::ObTableSchema *&data_table_schema,
    common::ObString &index_params,
    int64_t &dimension);

// Resolve the first user vector column represented by an auxiliary index.
// Current vector indexes support one user vector column; Observer hides the
// generated-column traversal needed to recover it.
int resolve_vector_index_column_name(
    const share::schema::ObTableSchema &data_table_schema,
    const share::schema::ObTableSchema &index_table_schema,
    common::ObString &column_name);

// Resolve the vector dimension encoded by a vector-index auxiliary table.
int resolve_vector_index_column_dimension(
    const share::schema::ObTableSchema &index_table_schema,
    int64_t &dimension);

int construct_vector_index_rebuild_parameters(
    const share::schema::ObTableSchema &data_table_schema,
    const common::ObString &old_index_parameters,
    common::ObString &new_index_parameters,
    common::ObIAllocator &allocator);

int vector_index_rebuild_requires_embedding(
    const common::ObString &old_index_parameters,
    const common::ObString &new_index_parameters,
    const share::schema::ObTableSchema &index_table_schema,
    bool &requires_embedding);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_VECTOR_OB_VECTOR_INDEX_SCHEMA_H_
