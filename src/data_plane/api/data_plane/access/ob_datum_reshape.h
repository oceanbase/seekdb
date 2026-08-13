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

#ifndef OCEANBASE_DATA_PLANE_ACCESS_OB_DATUM_RESHAPE_H_
#define OCEANBASE_DATA_PLANE_ACCESS_OB_DATUM_RESHAPE_H_

namespace oceanbase
{
namespace common
{
class ObAccuracy;
class ObIAllocator;
class ObIVector;
class ObObjMeta;
struct ObDatumVector;
}
namespace share
{
class ObBatchSelector;
}
namespace blocksstable
{
struct ObStorageDatum;
}
namespace data_plane
{

// Public data-representation policy shared by query producers and storage
// consumers.  Its implementation belongs to the data plane.
class ObDatumReshape
{
public:
  static int pad_datum_value(
      const common::ObObjMeta &col_type,
      const common::ObAccuracy &col_accuracy,
      common::ObIAllocator &allocator,
      blocksstable::ObStorageDatum &datum_value);
  static int reshape_datum_value(
      const common::ObObjMeta &col_type,
      const common::ObAccuracy &col_accuracy,
      common::ObIAllocator &allocator,
      blocksstable::ObStorageDatum &datum_value);
  static int reshape_datum_vector_value(
      const common::ObObjMeta &col_type,
      const common::ObAccuracy &col_accuracy,
      common::ObIAllocator &allocator,
      const common::ObDatumVector &datum_vector,
      share::ObBatchSelector &selector);
  static int reshape_vector_value(
      const common::ObObjMeta &col_type,
      const common::ObAccuracy &col_accuracy,
      common::ObIAllocator &allocator,
      common::ObIVector *&vector,
      share::ObBatchSelector &selector);
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_ACCESS_OB_DATUM_RESHAPE_H_
