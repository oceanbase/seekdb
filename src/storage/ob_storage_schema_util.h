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

#ifndef OCEANBASE_STORAGE_STORAGE_SCHEMA_UTIL_
#define OCEANBASE_STORAGE_STORAGE_SCHEMA_UTIL_
#include "storage/ob_storage_schema.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObTabletID;
}

namespace  storage
{

// TODO(@DanLing) manage major/minor/mini's storage schema here
class ObStorageSchemaUtil
{
public:
  static int update_tablet_storage_schema(
      const common::ObTabletID &tablet_id,
      common::ObIAllocator &allocator,
      const ObStorageSchema &old_schema_on_tablet,
      const ObStorageSchema &param_schema,
      ObStorageSchema *&chosen_schema);
  static int update_storage_schema(
      common::ObIAllocator &allocator,
      const ObStorageSchema &other_schema,
      ObStorageSchema &input_schema);

  /* TODO(@DanLing) remove this func after column_store merged into master
   * This func is just for replace ObTabletObjLoadHelper::alloc_and_new on master
   */
  static int alloc_storage_schema(
      common::ObIAllocator &allocator,
      ObStorageSchema *&new_storage_schema);
  static void free_storage_schema(
      common::ObIAllocator &allocator,
      ObStorageSchema *&new_storage_schema);
  static int alloc_cs_replica_storage_schema(
      common::ObIAllocator &allocator,
      const ObStorageSchema *storage_schema,
      ObStorageSchema *&new_storage_schema);
};

} // namespace storage
} // namespace oceanbase


#endif /* OCEANBASE_STORAGE_STORAGE_SCHEMA_UTIL_ */
