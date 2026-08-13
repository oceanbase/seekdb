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

#ifndef OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_VALUE_H_
#define OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_VALUE_H_

#include "common/object/ob_object.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace blocksstable
{
class ObDatumRow;
struct ObStorageDatum;
}
namespace share
{
namespace schema
{
struct ObColDesc;
}
}
namespace data_plane
{

void set_zero_lob_value(common::ObObjType type, common::ObObj &value);
int fill_lob_header(common::ObIAllocator &allocator,
                    common::ObString &data,
                    common::ObString &out);
int fill_lob_header(common::ObIAllocator &allocator,
                    blocksstable::ObStorageDatum &datum);
int fill_lob_header(
    common::ObIAllocator &allocator,
    const common::ObIArray<share::schema::ObColDesc> &column_ids,
    blocksstable::ObDatumRow &datum_row);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_VALUE_H_
