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

#include "common/row/ob_row_checksum.h"
#include "lib/utility/ob_sort.h"

namespace oceanbase
{
namespace common
{

DEFINE_GET_SERIALIZE_SIZE(ObRowChecksumValue)
{
  int64_t size = 0;
  size += serialization::encoded_length_i64(checksum_);
  size += serialization::encoded_length_vi64(column_count_);
  size += sizeof(column_checksum_array_[0]) * column_count_;
  return size;
}

DEFINE_SERIALIZE(ObRowChecksumValue)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(serialization::encode_i64(buf, buf_len, pos, checksum_))) {
    LOG_WARN("encode int failed", K(ret));
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, column_count_))) {
    LOG_WARN("encode int failed", K(ret));
  }
  if (OB_SUCC(ret) && column_count_ > 0) {
    const int64_t n = sizeof(column_checksum_array_[0]) * column_count_;
    if (buf_len - pos < n) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_WARN("serialize buf not enough", K(ret), "remain", buf_len - pos, "needed", n);
    } else {
      MEMCPY(buf + pos, column_checksum_array_, n);
      pos += n;
    }
  }

  return ret;
}

DEFINE_DESERIALIZE(ObRowChecksumValue)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(serialization::decode_i64(buf, data_len, pos,
      reinterpret_cast<int64_t *>(&checksum_)))) {
    LOG_WARN("decode int failed", K(ret));
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &column_count_))) {
    LOG_WARN("decode int failed", K(ret));
  }
  if (OB_SUCC(ret) && column_count_ > 0) {
    const int64_t n = sizeof(column_checksum_array_[0]) * column_count_;
    if (data_len - pos < n) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_WARN("serialize buf not enough", K(ret), "remain", data_len - pos, "needed", n);
    } else {
      column_checksum_array_ = reinterpret_cast<ObColumnIdChecksum *>(
          const_cast<char *>(buf + pos));
      pos += n;
    }
  }

  return ret;
}


void ObRowChecksumValue::reset()
{
  checksum_ = 0;
  column_count_ = 0;
  column_checksum_array_ = NULL;
}




} // end namespace common
} // end namespace oceanbase

