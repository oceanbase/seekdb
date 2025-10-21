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

#ifndef OCEANBASE_UNITTEST_MEDIUM_INFO_HELPER
#define OCEANBASE_UNITTEST_MEDIUM_INFO_HELPER

#include <stdint.h>
#include "lib/allocator/ob_allocator.h"
#include "unittest/storage/init_basic_struct.h"
#include "unittest/storage/schema_utils.h"
#include "storage/compaction/ob_medium_compaction_mgr.h"

namespace oceanbase
{
namespace storage
{
class MediumInfoHelper
{
public:
  static int build_medium_compaction_info(
      common::ObIAllocator &allocator,
      compaction::ObMediumCompactionInfo &info,
      const int64_t medium_snapshot);
};

int MediumInfoHelper::build_medium_compaction_info(
    common::ObIAllocator &allocator,
    compaction::ObMediumCompactionInfo &info,
    const int64_t medium_snapshot)
{
  int ret = common::OB_SUCCESS;
  info.compaction_type_ = compaction::ObMediumCompactionInfo::ObCompactionType::MEDIUM_COMPACTION;
  info.medium_snapshot_ = medium_snapshot;
  info.last_medium_snapshot_ = medium_snapshot;
  info.data_version_ = 100;
  info.cluster_id_ = 1;

  // storage schema
  const uint64_t table_id = 1234567;
  share::schema::ObTableSchema table_schema;
  build_test_schema(table_schema, table_id);
  ret = info.storage_schema_.init(allocator, table_schema, lib::Worker::CompatMode::MYSQL);

  return ret;
}
}
}

#endif // OCEANBASE_UNITTEST_MEDIUM_INFO_HELPER
