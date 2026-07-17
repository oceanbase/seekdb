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

#include "ob_row_data.h"

namespace oceanbase
{
using namespace common;
using namespace serialization;
namespace memtable
{
static int serialize_data(char *buf, const int64_t len, int64_t &pos, const char *data,
                          int64_t data_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || (OB_ISNULL(data) && data_len != 0) || pos < 0 || data_len < 0 || pos > len) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KP(buf), KP(data), K(data_len), K(pos));
  } else if (NULL == data) {
    // no need to copy
  } else if (pos + data_len > len) {
    ret = OB_BUF_NOT_ENOUGH;
  } else {
    MEMCPY(buf + pos, data, data_len);
    pos += data_len;
  }
  return ret;
}

static int deserialize_data(const char *buf, const int64_t len, int64_t &pos, const char *&data,
                            int64_t data_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || pos < 0 || data_len < 0 || pos > len) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid_argument", K(OB_P(buf)), K(pos), K(data_len), K(len));
  } else if (0 == data_len) {
    data = NULL;
  } else if (pos + data_len > len) {
    ret = OB_BUF_NOT_ENOUGH;
    TRANS_LOG(WARN, "buf not enough", KP(buf), KP(data), K(data_len), K(pos));
  } else {
    data = buf + pos;
    pos += data_len;
  }
  return ret;
}

int ObRowData::serialize(char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_FAIL(encode_vi32(buf, buf_len, new_pos, size_))) {
    TRANS_LOG(WARN, "encode int fail", KP(buf), K(buf_len), K(new_pos), K(size_));
  } else if (OB_FAIL(serialize_data(buf, buf_len, new_pos, data_, size_))) {
  } else {
    pos = new_pos;
  }
  return ret;
}

int ObRowData::deserialize(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_FAIL(decode_vi32(buf, data_len, new_pos, &size_))) {
    TRANS_LOG(WARN, "encode int fail", KP(buf), K(data_len), K(new_pos), K(size_));
  } else if (OB_FAIL(deserialize_data(buf, data_len, new_pos, data_, size_))) {
    TRANS_LOG(WARN, "deserialize_data fail", KP(buf), K(data_len), K(new_pos), KP(data_), K(size_));
  } else {
    pos = new_pos;
  }
  return ret;
}


}; // end namespace mvcc
}; // end namespace oceanbase
