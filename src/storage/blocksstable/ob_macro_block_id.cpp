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
#include "ob_macro_block_id.h"

namespace oceanbase
{
using namespace common;
namespace blocksstable
{

MacroBlockId::MacroBlockId()
  : first_id_(0), second_id_(INT64_MAX), third_id_(0)
{
  version_ = MACRO_BLOCK_ID_VERSION;
}

MacroBlockId::MacroBlockId(
    const uint64_t write_seq, const int64_t block_index, const int64_t third_id)
  : first_id_(0), second_id_(block_index), third_id_(third_id)
{
  write_seq_ = write_seq;
  version_ = MACRO_BLOCK_ID_VERSION;
}

MacroBlockId::MacroBlockId(const MacroBlockId &id)
  : first_id_(id.first_id_), second_id_(id.second_id_), third_id_(id.third_id_)
{
}

void MacroBlockId::first_id_to_string(char *buf, const int64_t buf_len, int64_t &pos) const
{
  databuff_printf(buf, buf_len, pos, "{[ver=%lu,seq=%lu]",
                  static_cast<uint64_t>(version_), static_cast<uint64_t>(write_seq_));
}

bool MacroBlockId::is_valid() const
{
  return MACRO_BLOCK_ID_VERSION == version_
      && block_index_ >= AUTONOMIC_BLOCK_INDEX
      && block_index_ < INT64_MAX
      && third_id_ >= 0;
}

int64_t MacroBlockId::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  first_id_to_string(buf, buf_len, pos);
  databuff_printf(buf, buf_len, pos, "[2nd=%lu][3rd=%lu]}",
                  static_cast<uint64_t>(second_id_), static_cast<uint64_t>(third_id_));
  return pos;
}

uint64_t MacroBlockId::hash() const
{
  return block_index_ * HASH_MAGIC_NUM;
}

int MacroBlockId::hash(uint64_t &hash_val) const
{
  hash_val = hash();
  return OB_SUCCESS;
}

DEFINE_SERIALIZE(MacroBlockId)
{
  int ret = OB_SUCCESS;
  const int64_t ser_len = get_serialize_size();
  int64_t new_pos = pos;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len <= 0 || buf_len - new_pos < ser_len)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(buf), K(buf_len), K(new_pos), K(ser_len));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid macro block id", K(ret), K(*this));
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, new_pos, first_id_))) {
    LOG_WARN("serialize first id failed", K(ret), K(new_pos), K(buf_len), K(*this));
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, new_pos, second_id_))) {
    LOG_WARN("serialize second id failed", K(ret), K(new_pos), K(buf_len), K(*this));
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, new_pos, third_id_))) {
    LOG_WARN("serialize third id failed", K(ret), K(new_pos), K(buf_len), K(*this));
  } else {
    pos = new_pos;
  }
  return ret;
}

DEFINE_DESERIALIZE(MacroBlockId)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_ISNULL(buf) || OB_UNLIKELY(data_len <= 0 || pos < 0 || pos >= data_len)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KP(buf), K(data_len), K(pos), K(ret));
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, new_pos, &first_id_))) {
    LOG_WARN("decode first id failed", K(ret), K(new_pos), K(data_len));
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, new_pos, &second_id_))) {
    LOG_WARN("decode second id failed", K(ret), K(new_pos), K(data_len));
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, new_pos, &third_id_))) {
    LOG_WARN("decode third id failed", K(ret), K(new_pos), K(data_len));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid macro block id", K(ret), K(*this));
  } else {
    pos = new_pos;
  }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(MacroBlockId)
{
  return serialization::encoded_length_i64(first_id_)
      + serialization::encoded_length_i64(second_id_)
      + serialization::encoded_length_i64(third_id_);
}

bool MacroBlockId::operator<(const MacroBlockId &other) const
{
  return block_index_ < other.block_index_;
}

} // namespace blocksstable
} // namespace oceanbase
