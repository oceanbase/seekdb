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

#ifndef OCEANBASE_DATA_PLANE_API_FTS_DICT_OB_DIC_LOCK_SERVICE_H_
#define OCEANBASE_DATA_PLANE_API_FTS_DICT_OB_DIC_LOCK_SERVICE_H_

namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
}
namespace storage
{
class ObDicLoader;
}
namespace data_plane
{

// Public dictionary-lock capability.  Lock modes and table-lock machinery are
// intentionally kept inside the data plane.
class ObDictionaryLockService
{
public:
  static int lock_tables_shared_in_transaction(
      const storage::ObDicLoader &loader,
      common::ObMySQLTransaction &trans);
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_FTS_DICT_OB_DIC_LOCK_SERVICE_H_
