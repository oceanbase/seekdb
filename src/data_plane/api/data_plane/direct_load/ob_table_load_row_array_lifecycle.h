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

#ifndef OCEANBASE_DATA_PLANE_DIRECT_LOAD_OB_TABLE_LOAD_ROW_ARRAY_LIFECYCLE_H_
#define OCEANBASE_DATA_PLANE_DIRECT_LOAD_OB_TABLE_LOAD_ROW_ARRAY_LIFECYCLE_H_

namespace oceanbase
{
namespace table
{
class ObTableLoadTabletObjRow;
template <class T>
class ObTableLoadRowArray;
}
namespace data_plane
{

// The table-load module owns the concrete row-array type and its allocator.
// Storage may carry the opaque payload, but destruction stays with its owner.
void destroy_table_load_row_array(
    table::ObTableLoadRowArray<table::ObTableLoadTabletObjRow> *&row_array);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_DIRECT_LOAD_OB_TABLE_LOAD_ROW_ARRAY_LIFECYCLE_H_
