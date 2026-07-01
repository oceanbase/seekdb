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

#include "lib/string/ob_string.h"
#include "common/data_buffer.h"

using namespace oceanbase;
using namespace common;



DEFINE_SERIALIZE(ObString)
{
  int ret = OB_SUCCESS;
  const int64_t serialize_size = get_serialize_size();
  //Null ObString is allowed
  if (OB_ISNULL(buf) || OB_UNLIKELY(serialize_size > buf_len - pos)) {
    ret = OB_SIZE_OVERFLOW;
    LIB_LOG(WARN, "size overflow", K(ret),
        KP(buf), K(serialize_size), "remain", buf_len - pos);
  } else if (OB_FAIL(serialization::encode_vstr(buf, buf_len, pos, ptr_, data_length_))) {
    LIB_LOG(WARN, "string serialize failed", K(ret));
  }
  return ret;
}

DEFINE_DESERIALIZE(ObString)
{
  int ret = OB_SUCCESS;
  int64_t len = 0;
  const int64_t MINIMAL_NEEDED_SIZE = 2; //at least need two bytes
  if (OB_ISNULL(buf) || OB_UNLIKELY((data_len - pos) < MINIMAL_NEEDED_SIZE)) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid argument", K(ret), KP(buf), "remain", data_len - pos);
  } else {
    if (0 == buffer_size_) {
      ptr_ = const_cast<char *>(serialization::decode_vstr(buf, data_len, pos, &len));
      if (OB_ISNULL(ptr_)) {
        ret = OB_ERROR;
        LIB_LOG(WARN, "decode NULL string", K(ret));
      }
    } else {
      //copy to ptr_
      const int64_t str_len = serialization::decoded_length_vstr(buf, data_len, pos);
      if (str_len < 0 || buffer_size_ < str_len || (data_len - pos) < str_len) {
        ret = OB_BUF_NOT_ENOUGH;
        LIB_LOG(WARN, "string buffer not enough",
            K(ret), K_(buffer_size), K(str_len), "remain", data_len - pos);
      } else if (NULL == serialization::decode_vstr(buf, data_len, pos, ptr_, buffer_size_, &len)) {
        ret = OB_ERROR;
        LIB_LOG(WARN, "fail to decode_vstr", K(str_len), K(pos), K(data_len), K(buffer_size_));
      }
    }
    if (OB_SUCC(ret)) {
      data_length_ = static_cast<obstr_size_t>(len);
    }
  }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObString)
{
  return serialization::encoded_length_vstr(data_length_);
}
