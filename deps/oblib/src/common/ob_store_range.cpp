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

#include "common/ob_store_range.h"

namespace oceanbase
{
namespace common
{

int ObStoreRange::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (NULL == buf || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid argument", KP(buf), K(buf_len), K(ret));
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos,
      static_cast<int64_t>(table_id_)))) {
    COMMON_LOG(WARN, "serialize table_id failed", KP(buf), K(buf_len), K(table_id_), K(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, border_flag_.get_data()))) {
    COMMON_LOG(WARN, "serialize border_falg failed",
        KP(buf), K(buf_len), K(pos), K(border_flag_), K(ret));
  } else if (OB_FAIL(start_key_.serialize(buf, buf_len, pos))) {
    COMMON_LOG(WARN, "serialize start_key failed",
        KP(buf), K(buf_len), K(pos), K(start_key_), K(ret));
  } else if (OB_FAIL(end_key_.serialize(buf, buf_len, pos))) {
    COMMON_LOG(WARN, "serialize end_key failed",
        KP(buf), K(buf_len), K(pos), K(end_key_), K(ret));
  }
  return ret;
}

int64_t ObStoreRange::get_serialize_size(void) const
{
  int64_t total_size = 0;

  total_size += serialization::encoded_length_vi64(table_id_);
  total_size += serialization::encoded_length_i8(border_flag_.get_data());

  total_size += start_key_.get_serialize_size();
  total_size += end_key_.get_serialize_size();

  return total_size;
}


int ObStoreRange::deserialize(const char *buf, const int64_t data_len,
                              int64_t &pos)
{
  int ret = OB_SUCCESS;
  int8_t flag = 0;

  if (NULL == buf || data_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid arguments.", KP(buf), K(data_len), K(ret));
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos,
                                                reinterpret_cast<int64_t *>(&table_id_)))) {
    COMMON_LOG(WARN, "deserialize table_id failed.",
               KP(buf), K(data_len), K(pos), K(table_id_), K(ret));
  } else if (OB_FAIL(serialization::decode_i8(buf, data_len, pos, &flag))) {
    COMMON_LOG(WARN, "deserialize flag failed.", KP(buf), K(data_len), K(pos), K(flag), K(ret));
  } else if (OB_FAIL(start_key_.deserialize(buf, data_len, pos))) {
    COMMON_LOG(WARN, "deserialize start_key failed.",
               KP(buf), K(data_len), K(pos), K(start_key_), K(ret));
  } else if (OB_FAIL(end_key_.deserialize(buf, data_len, pos))) {
    COMMON_LOG(WARN, "deserialize end_key failed.",
               KP(buf), K(data_len), K(pos), K(end_key_), K(ret));
  } else {
    border_flag_.set_data(flag);
  }
  return ret;
}

int ObStoreRange::deep_copy(ObIAllocator &allocator, ObStoreRange &dst) const
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(start_key_.deep_copy(dst.start_key_, allocator))) {
    COMMON_LOG(WARN, "deep copy start key failed.", K(start_key_), K(ret));
  } else if (OB_FAIL(end_key_.deep_copy(dst.end_key_, allocator))) {
    COMMON_LOG(WARN, "deep copy end key failed.", K(end_key_), K(ret));
  } else {
    dst.table_id_ = table_id_;
    dst.border_flag_ = border_flag_;
  }

  return ret;
}


// for multi version get, the rowkey would be converted into a range (with trans version),
// e.g. rowkey1 -> [(rowkey1, -read_snapshot), (rowkey1, MAX_VERSION)]


// for multi version scan, the range would be converted into a range (with trans version),
// e.g. case 1 : (rowkey1, rowkey2) -> ((rowkey1, MAX_VERSION), (rowkey2, -MAX_VERSION))
//      case 2 : [rowkey1, rowkey2] -> [(rowkey1, -MAX_VERSION), (rowkey2, MAX_VERSION)]


} //end namespace common
} //end namespace oceanbase
