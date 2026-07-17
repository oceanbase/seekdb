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

#define USING_LOG_PREFIX COMMON

#include "ob_tablet_id.h"


namespace oceanbase
{
namespace common
{
int ObTabletID::hash(uint64_t &hash_val) const
{
  hash_val = hash();
  return OB_SUCCESS;
}

uint64_t ObTabletID::hash() const
{
  uint64_t hash_val = 0;
  hash_val = murmurhash(&id_, sizeof(id_), hash_val);
  return hash_val;
}

int ObTabletID::serialize(char* buf, const int64_t buf_len, int64_t& pos) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, pos, static_cast<int64_t>(id_)))) {
    LOG_WARN("serialize tablet ID failed", K(ret), KP(buf), K(buf_len), K(pos));
  }
  return ret;
}

int ObTabletID::deserialize(const char* buf, const int64_t data_len, int64_t& pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(data_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(data_len));
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, pos, reinterpret_cast<int64_t *>(&id_)))) {
    LOG_WARN("deserialize tablet ID failed", K(ret), KP(buf), K(data_len), K(pos));
  }
  return ret;
}

int64_t ObTabletID::get_serialize_size() const
{
  int64_t size = 0;
  size += serialization::encoded_length_i64(id_);
  return size;
}
} // end namespace common
} // end namespace oceanbase
