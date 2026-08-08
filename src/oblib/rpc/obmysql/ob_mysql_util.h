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

#ifndef _OB_MYSQL_UTIL_H_
#define _OB_MYSQL_UTIL_H_

#include <float.h> // for FLT_DIG and DBL_DIG
#include <inttypes.h>
#include <stdint.h>

#include "common/mysqlclient/ob_mysql_global.h"
#include "lib/oblog/ob_log.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"

using namespace oceanbase::common;
namespace oceanbase {
namespace obmysql
{
enum MYSQL_PROTOCOL_TYPE
{
  TEXT = 1,
  BINARY,
};

class ObMySQLUtil
{
public:
  /**
   * update null bitmap for binary protocol
   * please review http://dev.mysql.com/doc/internals/en/prepared-statements.html#null-bitmap for detail
   *
   * @param bitmap[in/out]         input bitmap
   * @param field_index            index of field
   *
   */
  static void update_null_bitmap(char *&bitmap, int64_t field_index);

  /**
   * Write the length into buf, which may take 1, 3, 4, or 9 bytes.
   *
   * @param [in] buf The buf to write the length into
   * @param [in] len Total number of bytes in buf
   * @param [in] length The value of the length to be written
   * @param [in,out] pos Number of bytes already used in buf
   *
   * @return oceanbase error code
   */
  static int store_length(char *buf, int64_t len, uint64_t length, int64_t &pos);
  /**
   * Read the length field, used in conjunction with store_length()
   *
   * @param [in,out] pos The position to read the length from, pos is updated after reading
   * @param [out] length The length
   */
  static int get_length(const char *&pos, uint64_t &length);
  /**
   * Store a NULL flag.
   *
   * @param buf Memory address to write to
   * @param len Total number of bytes in buf
   * @param pos Number of bytes already written to buf
   */
  static inline int store_null(char *buf, int64_t len, int64_t &pos);
  /**
   * Pack a string str of length length into buf (length coded string format)
   *
   * @param [in] buf The memory address to write to
   * @param [in] len Total number of bytes in buf
   * @param [in] str The string to be packed
   * @param [in] length Length of str
   * @param [in,out] pos Number of bytes already used in buf
   *
   * @return OB_SUCCESS or oceanbase error code.
   *
   * @see store_length()
   */
  static int store_str_v(char *buf, int64_t len, const char *str,
                         const uint64_t length, int64_t &pos);
  /**
   * Pack the string str of ObString structure into buf (in length coded string format)
   *
   * @param [in] buf The memory address to write to
   * @param [in] len Total number of bytes in buf
   * @param [in] str String of ObString structure
   * @param [in,out] pos Number of bytes already used in buf
   *
   * @return OB_SUCCESS or oceanbase error code.
   *
   * @see store_length()
   * @see store_str_v()
   */
  static int store_obstr(char *buf, int64_t len, ObString str, int64_t &pos);
  //@{Serialize integer data, save the data in v to the position of buf+pos, and update pos
  static inline int store_int1(char *buf, int64_t len, int8_t v, int64_t &pos);
  static inline int store_int2(char *buf, int64_t len, int16_t v, int64_t &pos);
  static inline int store_int3(char *buf, int64_t len, int32_t v, int64_t &pos);
  static inline int store_int4(char *buf, int64_t len, int32_t v, int64_t &pos);
  static inline int store_int8(char *buf, int64_t len, int64_t v, int64_t &pos);
  //@}

  //@{ Signed integer in reverse sequence, write the result to v, and update pos
  static inline void get_int1(const char *&pos, int8_t &v);
  static inline void get_int2(const char *&pos, int16_t &v);
  static inline void get_int4(const char *&pos, int32_t &v);
  static inline void get_int8(const char *&pos, int64_t &v);
  //@}

  //@{ Deserialize unsigned integer, write the result to v, and update pos
  static inline void get_uint1(const char *&pos, uint8_t &v);
  static inline void get_uint2(const char *&pos, uint16_t &v);
  static inline void get_uint3(const char *&pos, uint32_t &v);
  static inline void get_uint4(const char *&pos, uint32_t &v);
  //@}

  static inline int16_t float_length(const int16_t scale);

  static const uint64_t NULL_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObMySQLUtil);
}; // class ObMySQLUtil

int ObMySQLUtil::store_null(char *buf, int64_t len, int64_t &pos)
{
  return store_int1(buf, len, static_cast<int8_t>(251), pos);
}

int ObMySQLUtil::store_int1(char *buf, int64_t len, int8_t v, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", KP(buf), K(ret));
  } else {
    if (len >= pos + 1) {
      int1store(buf + pos, v);
      pos++;
    } else {
      ret = OB_SIZE_OVERFLOW;
    }
  }
  return ret;
}

int ObMySQLUtil::store_int2(char *buf, int64_t len, int16_t v, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", KP(buf), K(ret));
  } else {
    if (len >= pos + 2) {
      int2store(buf + pos, v);
      pos += 2;
    } else {
      ret = OB_SIZE_OVERFLOW;
    }
  }
  return ret;
}

int ObMySQLUtil::store_int3(char *buf, int64_t len, int32_t v, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", KP(buf), K(ret));
  } else {
    if (len >= pos + 3) {
      int3store(buf + pos, v);
      pos += 3;
    } else {
      ret = OB_SIZE_OVERFLOW;
    }
  }
  return ret;
}

int ObMySQLUtil::store_int4(char *buf, int64_t len, int32_t v, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", KP(buf), K(ret));
  } else {
    if (len >= pos + 4) {
      int4store(buf + pos, v);
      pos += 4;
    } else {
      ret = OB_SIZE_OVERFLOW;
    }
  }
  return ret;
}

int ObMySQLUtil::store_int8(char *buf, int64_t len, int64_t v, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", KP(buf), K(ret));
  } else {
    if (len >= pos + 8) {
      int8store(buf + pos, v);
      pos += 8;
    } else {
      ret = OB_SIZE_OVERFLOW;
    }
  }
  return ret;
}

void ObMySQLUtil::get_int1(const char *&pos, int8_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = sint1korr(pos);
    pos++;
  }
}
void ObMySQLUtil::get_int2(const char *&pos, int16_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = sint2korr(pos);
    pos += 2;
  }
}
void ObMySQLUtil::get_int4(const char *&pos, int32_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = sint4korr(pos);
    pos += 4;
  }
}
void ObMySQLUtil::get_int8(const char *&pos, int64_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = sint8korr(pos);
    pos += 8;
  }
}


void ObMySQLUtil::get_uint1(const char *&pos, uint8_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint1korr(pos);
    pos ++;
  }
}
void ObMySQLUtil::get_uint2(const char *&pos, uint16_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint2korr(pos);
    pos += 2;
  }
}
void ObMySQLUtil::get_uint3(const char *&pos, uint32_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint3korr(pos);
    pos += 3;
  }
}
void ObMySQLUtil::get_uint4(const char *&pos, uint32_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint4korr(pos);
    pos += 4;
  }
}
/*
 * get precision for double type, keep same with MySQL
 */
int16_t ObMySQLUtil::float_length(const int16_t scale)
{
  return (scale >= 0 && scale <= OB_MAX_DOUBLE_FLOAT_SCALE) ? DBL_DIG + 2 + scale : DBL_DIG + 8;
}

}      // namespace obmysql
} // namespace oceanbase

#endif /* _OB_MYSQL_UTIL_H_ */
