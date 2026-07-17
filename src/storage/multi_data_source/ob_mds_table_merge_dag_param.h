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

#ifndef OCEANBASE_STORAGE_OB_MDS_TABLE_MERGE_PARAM
#define OCEANBASE_STORAGE_OB_MDS_TABLE_MERGE_PARAM

#include <stdint.h>
#include "storage/compaction/ob_tablet_merge_task.h"
#include "share/scn.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{
class ObMdsTableMergeDagParam : public compaction::ObTabletMergeDagParam
{
public:
  ObMdsTableMergeDagParam();
  virtual ~ObMdsTableMergeDagParam() = default;
public:
  INHERIT_TO_STRING_KV("ObTabletMergeDagParam", compaction::ObTabletMergeDagParam,
                       K_(flush_scn), KTIME_(generate_ts), K_(mds_construct_sequence), K_(mds_construct_sequence));
public:
  share::SCN flush_scn_;
  int64_t generate_ts_;
  int64_t mds_construct_sequence_;
};
} // namespace mds
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_MDS_TABLE_MERGE_PARAM
