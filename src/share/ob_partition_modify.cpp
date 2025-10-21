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

#define USING_LOG_PREFIX SHARE
#include "ob_partition_modify.h"

namespace oceanbase
{
using namespace common;

namespace share
{









int ObSplitPartitionPair::assign(const ObSplitPartitionPair &other)
{
  UNUSED(other);
  return OB_NOT_SUPPORTED;
}


bool ObSplitPartition::is_valid() const
{
  return split_info_.count() > 0 && schema_version_ > 0;
}

void ObSplitPartition::reset()
{
  split_info_.reset();
  schema_version_ = 0;
}

int ObSplitPartition::assign(const ObSplitPartition &other)
{
  UNUSED(other);
  return OB_NOT_SUPPORTED;
}


OB_SERIALIZE_MEMBER(ObSplitPartition, split_info_, schema_version_);
OB_SERIALIZE_MEMBER(ObSplitPartitionPair, unused_);
OB_SERIALIZE_MEMBER(ObPartitionSplitProgress, progress_);
} // namespace rootserver
} // namespace oceanbase
