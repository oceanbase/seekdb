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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_CHECKSUM_VALIDATOR_H_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_CHECKSUM_VALIDATOR_H_

#include "rootserver/fork_table/table_storage_layouts.h"
#include "rootserver/freeze/ob_major_merge_progress_util.h"
#include "share/ob_freeze_info_proxy.h"

namespace oceanbase {
namespace storage { class ObLS; }
namespace common { class ObMySQLProxy; }
namespace share { namespace schema { class ObMultiVersionSchemaService; } }
namespace rootserver {

// One owner and one freeze per invocation. Definitions and checksums are
// temporary; no Namespace schema cache or persistent verification roster.
class ObChecksumValidator final
{
public:
  static int check_namespace(uint64_t namespace_id,
      const common::ObIArray<TableStorageLayouts::Definition> &definitions,
      const share::ObFreezeInfo &freeze, storage::ObLS &ls,
      common::ObMySQLProxy &sql, share::schema::ObMultiVersionSchemaService &schemas,
      volatile bool &stop, compaction::ObMergeProgress &progress,
      compaction::ObUncompactInfo &pending);
};

} // namespace rootserver
} // namespace oceanbase
#endif
