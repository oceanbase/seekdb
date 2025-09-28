/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
