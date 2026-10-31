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

#define USING_LOG_PREFIX STORAGE

#include "storage/blockstore/ob_linked_macro_block_struct.h"

namespace oceanbase
{
using namespace blocksstable;
namespace storage
{

using namespace blocksstable;

int ObLinkedMacroBlockHeader::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == buf || buf_len < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(LINKED_MACRO_BLOCK_HEADER_VERSION != version_)) {
    ret = OB_ERR_UNEXPECTED;
  }
  SERIALIZE_MEMBER_WITH_MEMCPY(version_);
  SERIALIZE_MEMBER_WITH_MEMCPY(magic_);
  SERIALIZE_MEMBER_WITH_MEMCPY(item_count_);
  SERIALIZE_MEMBER_WITH_MEMCPY(fragment_offset_);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(previous_macro_block_id_.serialize(buf, buf_len, pos))) {
    }
  }
  return ret;
}

int ObLinkedMacroBlockHeader::deserialize(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == buf || data_len <= 0 || pos < 0)) {
    ret = OB_INVALID_ARGUMENT;
  }
  DESERIALIZE_MEMBER_WITH_MEMCPY(version_);
  DESERIALIZE_MEMBER_WITH_MEMCPY(magic_);
  DESERIALIZE_MEMBER_WITH_MEMCPY(item_count_);
  DESERIALIZE_MEMBER_WITH_MEMCPY(fragment_offset_);

  if (OB_SUCC(ret) && OB_UNLIKELY(LINKED_MACRO_BLOCK_HEADER_VERSION != version_)) {
    ret = OB_DESERIALIZE_ERROR;
  } else if (OB_SUCC(ret) && OB_FAIL(previous_macro_block_id_.deserialize(buf, data_len, pos))) {
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
