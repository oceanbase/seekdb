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

#define USING_LOG_PREFIX RPC_OBMYSQL

#include "rpc/obmysql/ob_mysql_util.h"
using namespace oceanbase::common;

namespace oceanbase
{
namespace obmysql
{
const uint64_t ObMySQLUtil::NULL_ = UINT64_MAX;
// @todo
//TODO avoid coredump if field_index is too large
//http://dev.mysql.com/doc/internals/en/prepared-statements.html#null-bitmap
//offset is 2
void ObMySQLUtil::update_null_bitmap(char *&bitmap, int64_t field_index)
{
  int byte_pos = static_cast<int>((field_index + 2) / 8);
  int bit_pos  = static_cast<int>((field_index + 2) % 8);
  bitmap[byte_pos] |= static_cast<char>(1 << bit_pos);
}

//called by handle COM_STMT_EXECUTE offset is 0
int ObMySQLUtil::store_length(char *buf, int64_t len, uint64_t length, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (len < 0 || pos < 0 || len <= pos) {
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid input buf", K(ret), KP(buf));
  } else {
    int64_t remain = len - pos;
    if (OB_SUCC(ret)) {
      if (length < (uint64_t) 251 && remain >= 1) {
        ret = store_int1(buf, len, (uint8_t) length, pos);
      }
      /* 251 is reserved for NULL */
      else if (length < (uint64_t) 0X10000 && remain >= 3) {
        ret = store_int1(buf, len, static_cast<int8_t>(252), pos);
        if (OB_SUCC(ret)) {
          ret = store_int2(buf, len, (uint16_t) length, pos);
          if (OB_FAIL(ret)) {
            pos--;
          }
        }
      } else if (length < (uint64_t) 0X1000000 && remain >= 4) {
        ret = store_int1(buf, len, (uint8_t) 253, pos);
        if (OB_SUCC(ret)) {
          ret = store_int3(buf, len, (uint32_t) length, pos);
          if (OB_FAIL(ret)) {
            pos--;
          }
        }
      } else if (length < UINT64_MAX && remain >= 9) {
        ret = store_int1(buf, len, (uint8_t) 254, pos);
        if (OB_SUCC(ret)) {
          ret = store_int8(buf, len, (uint64_t) length, pos);
          if (OB_FAIL(ret)) {
            pos--;
          }
        }
      } else if (length == UINT64_MAX) { /* NULL_ == UINT64_MAX */
        ret = store_null(buf, len, pos);
      } else {
        ret = OB_SIZE_OVERFLOW;
      }
    }
  }
  return ret;
}

int ObMySQLUtil::get_length(const char *&pos, uint64_t &length)
{
  uint8_t sentinel = 0;
  uint16_t s2 = 0;
  uint32_t s4 = 0;
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pos)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid input buf", KP(pos), K(ret));
  } else {
    get_uint1(pos, sentinel);
    if (sentinel < 251) {
      length = sentinel;
    } else if (sentinel == 251) {
      length = NULL_;
    } else if (sentinel == 252) {
      get_uint2(pos, s2);
      length = s2;
    } else if (sentinel == 253) {
      get_uint3(pos, s4);
      length = s4;
    } else if (sentinel == 254) {
      {
        get_uint4(pos, s4);
        length = s4;
        pos += 4;
      }
    } else {
      // 255??? won't get here.
      pos--;                  // roll back
      ret = OB_INVALID_DATA;
    }
  }
  return ret;
}

int ObMySQLUtil::store_str_v(char *buf, int64_t len, const char *str,
                             const uint64_t length, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t pos_bk = pos;

  if (OB_ISNULL(buf)) { // str could be null
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid input args", KP(buf), K(ret));
  } else {
    if (OB_FAIL(store_length(buf, len, length, pos))) {
    } else if (len >= pos && length <= static_cast<uint64_t>(len - pos)) {
      if ((0 == length ) || (length > 0 && NULL != str)) {
        MEMCPY(buf + pos, str, length);
        pos += length;
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid args", "str", ObString(length, str), K(length));
      }
    } else {
      LOG_INFO("=========== store_str_v ====", K(len), K(length), K(pos), K(pos_bk));
      pos = pos_bk;        // roll back
      ret = OB_SIZE_OVERFLOW;
    }
  }
  return ret;
}

int ObMySQLUtil::store_obstr(char *buf, int64_t len, ObString str, int64_t &pos)
{
  return store_str_v(buf, len, str.ptr(), str.length(), pos);
}

} // namespace obmysql
} // namespace oceanbase
