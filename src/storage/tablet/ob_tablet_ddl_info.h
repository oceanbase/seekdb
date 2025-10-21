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

#ifndef OCEANBASE_STORAGE_TABLET_OB_TABLET_DDL_INFO
#define OCEANBASE_STORAGE_TABLET_OB_TABLET_DDL_INFO

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "share/scn.h"
#include "lib/lock/ob_small_spin_lock.h"

namespace oceanbase
{
namespace storage
{
struct ObTabletDDLInfo final
{
public:
  ObTabletDDLInfo();
  ~ObTabletDDLInfo() = default;
  ObTabletDDLInfo &operator =(const ObTabletDDLInfo &other);
public:
  void reset();
  int get(int64_t &schema_version, int64_t &schema_refreshed_ts);
  int update(const int64_t schema_version,
             const share::SCN &scn,
             int64_t &schema_refreshed_ts);
  TO_STRING_KV(K_(ddl_schema_version), K_(ddl_schema_refreshed_ts), K_(schema_version_change_scn));
private:
  int64_t ddl_schema_version_;
  int64_t ddl_schema_refreshed_ts_;
  share::SCN schema_version_change_scn_;
  common::ObByteLock rwlock_;
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_TABLET_OB_TABLET_DDL_INFO
