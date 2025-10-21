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

#include <inttypes.h>
#include <stdint.h>
#include <float.h>              // for FLT_DIG and DBL_DIG
#include "lib/oblog/ob_log.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/number/ob_number_v2.h"
#include "lib/timezone/ob_timezone_info.h"
#include "rpc/obmysql/ob_mysql_global.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{
class ObOTimestampData;
class ObDataTypeCastParams;
struct ObDecimalInt;
}
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
   * Read the length field, used in conjunction with store_length()
   *
   * @param [in,out] pos The position to read the length from, pos is updated after reading
   * @param [out] length The length
   * @param [out] pos_inc_len The length by which pos is incremented
   */
  static int get_length(const char *&pos, uint64_t &length, uint64_t &pos_inc_len);
  /**
   * The length required to store a number in length coded string format
   *
   * @param [in] number
   * @return number of bytes required for storage
   */
  static uint64_t get_number_store_len(const uint64_t num);
  /**
   * Store a NULL flag.
   *
   * @param buf Memory address to write to
   * @param len Total number of bytes in buf
   * @param pos Number of bytes already written to buf
   */
  static inline int store_null(char *buf, int64_t len, int64_t &pos);
  /**
   * @see store_str_v()
   * Pack a null-terminated string str into buf (length coded string format)
   *
   * @param [in] buf The memory address to write to
   * @param [in] len Total number of bytes in buf
   * @param [in] str The string to be packed
   * @param [in,out] pos Number of bytes already used in buf
   *
   * @return OB_SUCCESS or oceanbase error code.
   *
   * @see store_length()
   */
  static int store_str(char *buf, int64_t len, const char *str, int64_t &pos);
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
   * @see store_str()
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
  static int store_obstr_with_pre_space(char *buf, int64_t len, ObString str, int64_t &pos);
  /**
   * @see store_str_vzt()
   * Pack a zero-terminated string str into buf (zero terminated string format)
   *
   * @param [in] buf The memory address to write to
   * @param [in] len Total number of bytes in buf
   * @param [in] str The string to be packed
   * @param [in,out] pos Number of bytes already used in buf
   *
   * @return OB_SUCCESS or oceanbase error code.
   */
  /**
   * @see store_str_vzt()
   * Pack a null-terminated string str into buf (none-zero terminated string format)
   *
   * @param [in] buf The memory address to write to
   * @param [in] len Total number of bytes in buf
   * @param [in] str The string to be packed
   * @param [in,out] pos Number of bytes already used in buf
   *
   * @return OB_SUCCESS or oceanbase error code.
   */
  /**
   * @see store_str_zt()
   * Pack a string str of length length into buf (zero terminated string format)
   *
   * @param [in] buf Memory address to write to
    * @param [in] len Total number of bytes in buf
   * @param [in] str String to be packed
   * @param [in] length Length of str (excluding '\0')
   * @param [in,out] pos Number of bytes already used in buf
   *
   * @return OB_SUCCESS or oceanbase error code.
   */
  static int store_str_vzt(char *buf, int64_t len, const char *str,
                           const uint64_t length, int64_t &pos);
  /**
   * @see store_str_zt()
   * Pack a string str of length length into buf (none-zero terminated string format)
   *
   * @param [in] buf The memory address to write to
   * @param [in] len Total number of bytes in buf
   * @param [in] str The string to be packed
   * @param [in] length Length of str (excluding '\0')
   * @param [in,out] pos Number of bytes already used in buf
   *
   * @return OB_SUCCESS or oceanbase error code.
   */
  static int store_str_vnzt(char *buf, int64_t len,
                            const char *str, int64_t length, int64_t &pos);
  /**
   * @see store_str_vzt()
   * Pack the string str of ObString structure into buf (zero terminated string format)
   *
   * @param [in] buf The memory address to write to
   * @param [in] len Total number of bytes of buf
   * @param [in] str String of ObString structure
   * @param [in,out] pos Number of bytes already used in buf
   *
   * @return OB_SUCCESS or oceanbase error code.
   */
  static int store_obstr_zt(char *buf, int64_t len, ObString str, int64_t &pos);
  /**
   * Pack the string str of ObString structure into buf (none-zero terminated raw string format)
   *
   * @param [in] buf The memory address to write to
   * @param [in] len Total number of bytes of buf
   * @param [in] str String of ObString structure
   * @param [in,out] pos Number of bytes already used in buf
   *
   * @return OB_SUCCESS or oceanbase error code.
   */
  static int store_obstr_nzt(char *buf, int64_t len, ObString str, int64_t &pos);
  static int store_obstr_nzt_with_pre_space(char *buf, int64_t len, ObString str, int64_t &pos);
  //@{Serialize integer data, save the data in v to the position of buf+pos, and update pos
  static inline int store_int1(char *buf, int64_t len, int8_t v, int64_t &pos);
  static inline int store_int2(char *buf, int64_t len, int16_t v, int64_t &pos);
  static inline int store_int3(char *buf, int64_t len, int32_t v, int64_t &pos);
  static inline int store_int4(char *buf, int64_t len, int32_t v, int64_t &pos);
  static inline int store_int5(char *buf, int64_t len, int64_t v, int64_t &pos);
  static inline int store_int6(char *buf, int64_t len, int64_t v, int64_t &pos);
  static inline int store_int8(char *buf, int64_t len, int64_t v, int64_t &pos);
  //@}

  //@{ Signed integer in reverse sequence, write the result to v, and update pos
  static inline void get_int1(const char *&pos, int8_t &v);
  static inline void get_int2(const char *&pos, int16_t &v);
  static inline void get_int3(const char *&pos, int32_t &v);
  static inline void get_int4(const char *&pos, int32_t &v);
  static inline void get_int8(const char *&pos, int64_t &v);
  //@}

  //@{ Deserialize unsigned integer, write the result to v, and update pos
  static inline void get_uint1(const char *&pos, uint8_t &v);
  static inline void get_uint2(const char *&pos, uint16_t &v);
  static inline void get_uint3(const char *&pos, uint32_t &v);
  static inline void get_uint4(const char *&pos, uint32_t &v);
  static inline void get_uint5(const char *&pos, uint64_t &v);
  static inline void get_uint6(const char *&pos, uint64_t &v);
  static inline void get_uint8(const char *&pos, uint64_t &v);

  static inline void get_uint1(char *&pos, uint8_t &v);
  static inline void get_uint2(char *&pos, uint16_t &v);
  static inline void get_uint3(char *&pos, uint32_t &v);
  static inline void get_uint4(char *&pos, uint32_t &v);
  static inline void get_uint5(char *&pos, uint64_t &v);
  static inline void get_uint6(char *&pos, uint64_t &v);
  static inline void get_uint8(char *&pos, uint64_t &v);
//@}


  /**
   * Add 0 at buf[0...offset), move the original org_char_size bytes of data backward by offset bytes
   */
  static void prepend_zeros(char *buf, const int64_t org_char_size, int64_t offset);


  /**
   * Serialize a null type cell to the position of buf + pos.
   *
   * @param [in] obj The cell to be serialized
   * @param [in] buf The output buffer
   * @param [in] len The size of the buffer
   * @param [in,out] pos The position to write to the buffer
   * @param [in] field index
   *
   * @return Returns OB_SUCCESS on success, returns oceanbase error code on failure
   */
  static int null_cell_str(char *buf, const int64_t len,
                           MYSQL_PROTOCOL_TYPE type, int64_t &pos,
                           int64_t cell_index, char *bitmap);
  /**
   * Serialize an integer cell to the position of buf + pos.
   * (ObBoolType, ObIntType)
   *
   * @param [in] obj The cell to be serialized
   * @param [in] buf The output buffer
   * @param [in] len The size of the buffer
   * @param [in,out] pos The position to write to the buffer
   *
   * @return Returns OB_SUCCESS on success, returns oceanbase error code on failure
   */
  static int int_cell_str(char *buf, const int64_t len, int64_t val,
                          const ObObjType obj_type,
                          bool is_unsigned,
                          MYSQL_PROTOCOL_TYPE type, int64_t &pos,
                          bool zerofill, int32_t zflength);
  /**
   * Serialize a fixed-point type cell to the position of buf + pos.
   * (ObDecimalType)
   *
   * @param [in] obj The cell to be serialized
   * @param [in] buf The output buffer
   * @param [in] len The size of the buffer
   * @param [in,out] pos The position to write to the buffer
   *
   * @return Returns OB_SUCCESS on success, returns oceanbase error code on failure
   */
  static int number_cell_str(char *buf, const int64_t len,
                             const number::ObNumber &val, int64_t &pos, int16_t scale,
                             bool zerofill, int32_t zflength);
  /**
   * Serialize a datetime type cell to the position of buf + pos.
   * (ObDateTimeType, ObTimestampType)
   *
   * @param [in] obj The cell to be serialized
   * @param [in] buf The output buffer
   * @param [in] len The size of the buffer
   * @param [in,out] pos The position to write to the buffer
   *
   * @return Returns OB_SUCCESS on success, returns oceanbase error code on failure
   */
  static int datetime_cell_str(char *buf, const int64_t len,
                               int64_t val, MYSQL_PROTOCOL_TYPE type, int64_t &pos,
                               const ObTimeZoneInfo *tz_info, int16_t scale);
  static int mdatetime_cell_str(char *buf, const int64_t len,
                                const ObMySQLDateTime &val, MYSQL_PROTOCOL_TYPE type, int64_t &pos,
                                const ObTimeZoneInfo *tz_info, int16_t scale);

  /**
   * Serialize an obstring type cell to the position of buf + pos.
   * (ObDateTimeType, ObTimestampType)
   *
   * @param [in] obj The cell to be serialized
   * @param [in] buf The output buffer
   * @param [in] len The size of buf
   * @param [in,out] pos The position to write to buf
   *
   * @return Returns OB_SUCCESS on success, returns oceanbase error code on failure
   */

  /**
   * Serialize an otimestamp type cell to the position of buf + pos.
   * (ObDateTimeType, ObTimestampType)
   *
   * @param [in] obj The cell to be serialized
   * @param [in] buf The output buffer
   * @param [in] len The size of the buffer
   * @param [in,out] pos The position to write to the buffer
   *
   * @return Returns OB_SUCCESS on success, returns oceanbase error code on failure
   */
  static int otimestamp_cell_str(char *buf, const int64_t len,
                                 const ObOTimestampData &ot_data, MYSQL_PROTOCOL_TYPE type, int64_t &pos,
                                 const ObDataTypeCastParams &dtc_params, const int16_t scale, const ObObjType obj_type);
  static int otimestamp_cell_str2(char *buf, const int64_t len,
                                 const ObOTimestampData &ot_data, MYSQL_PROTOCOL_TYPE type, int64_t &pos,
                                 const ObDataTypeCastParams &dtc_params, const int16_t scale, const ObObjType obj_type);
  /**
   * Serialize a date-type cell to the position of buf + pos.
   * (ObDateType)
   *
   * @param [in] obj The cell to be serialized
   * @param [in] buf The output buffer
   * @param [in] len The size of the buffer
   * @param [in,out] pos The position to write to the buffer
   *
   * @return Returns OB_SUCCESS on success, or an oceanbase error code on failure
   */
  static int date_cell_str(char *buf, const int64_t len,
                           int32_t val, MYSQL_PROTOCOL_TYPE type, int64_t &pos);
  static int mdate_cell_str(char *buf, const int64_t len,
                            ObMySQLDate val, MYSQL_PROTOCOL_TYPE type, int64_t &pos);
  /**
   * Serialize a time-type cell to the position of buf + pos.
   * (ObTimeType)
   *
   * @param [in] obj The cell to be serialized
   * @param [in] buf The output buffer
   * @param [in] len The size of the buffer
   * @param [in,out] pos The position to write to the buffer
   *
   * @return Returns OB_SUCCESS on success, returns oceanbase error code on failure
   */
  static int time_cell_str(char *buf, const int64_t len,
                           int64_t val, MYSQL_PROTOCOL_TYPE type, int64_t &pos, int16_t scale);
  /**
   * Serialize a year-type cell to the position of buf + pos.
   * (ObYearType)
   *
   * @param [in] obj The cell to be serialized
   * @param [in] buf The output buffer
   * @param [in] len The size of the buffer
   * @param [in,out] pos The position to write to the buffer
   *
   * @return Returns OB_SUCCESS on success, or an oceanbase error code on failure
   */
  static int year_cell_str(char *buf, const int64_t len,
                           uint8_t val, MYSQL_PROTOCOL_TYPE type, int64_t &pos);
  /**
   * Serialize a string type cell to the position of buf + pos.
   * (ObVarcharType)
   *
   * @param [in] obj The cell to be serialized
   * @param [in] buf The output buffer
   * @param [in] len The size of the buffer
   * @param [in,out] pos The position to write to the buffer
   *
   * @return Returns OB_SUCCESS on success, returns oceanbase error code on failure
   */
  static int varchar_cell_str(char *buf, int64_t len, const ObString &val,
                              const bool is_oracle_raw, int64_t &pos);
  /**
   * Serialize a floating-point type cell to the position of buf + pos.
   * (ObFloatType, ObDoubleType)
   *
   * @param [in] obj The cell to be serialized
   * @param [in] buf The output buffer
   * @param [in] len The size of the buffer
   * @param [in,out] pos The position to write to the buffer
   *
   * @return Returns OB_SUCCESS on success, returns oceanbase error code on failure
   */
  static int float_cell_str(char *buf, const int64_t len, float val,
                            MYSQL_PROTOCOL_TYPE type, int64_t &pos, int16_t scale,
                            bool zerofill, int32_t zflength);
  static int double_cell_str(char *buf, const int64_t len, double val,
                             MYSQL_PROTOCOL_TYPE type, int64_t &pos, int16_t scale,
                             bool zerofill, int32_t zflength);
  static int bit_cell_str(char *buf, const int64_t len, uint64_t val,
                          int32_t bit_len, MYSQL_PROTOCOL_TYPE type, int64_t &pos);
  static int json_cell_str(uint64_t tenant_id, char *buf, const int64_t len, const ObString &val, int64_t &pos);
  static int sql_utd_cell_str(uint64_t tenant_id, char *buf, const int64_t len, const ObString &val, int64_t &pos);

  static int decimalint_cell_str(char *buf, const int64_t len,
                                 const ObDecimalInt *decint, const int32_t int_bytes, int16_t scale,
                                 int64_t &pos, bool zerofill, int32_t zflength);
  static int geometry_cell_str(char *buf, const int64_t len, const ObString &val, int64_t &pos);
  static int roaringbitmap_cell_str(char *buf, const int64_t len, const ObString &val, int64_t &pos);
  static inline int16_t float_length(const int16_t scale);

private:
  static int ob_time_cell_str(const ObTime &ob_time, char *buf, const int64_t len, int64_t &pos);

public:
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

int ObMySQLUtil::store_int5(char *buf, int64_t len, int64_t v, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", KP(buf), K(ret));
  } else {
    if (len >= pos + 5) {
      int5store(buf + pos, v);
      pos += 5;
    } else {
      ret = OB_SIZE_OVERFLOW;
    }
  }
  return ret;
}

int ObMySQLUtil::store_int6(char *buf, int64_t len, int64_t v, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", KP(buf), K(ret));
  } else {
    if (len >= pos + 6) {
      int6store(buf + pos, v);
      pos += 6;
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
void ObMySQLUtil::get_int3(const char *&pos, int32_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = sint3korr(pos);
    pos += 3;
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
void ObMySQLUtil::get_uint5(const char *&pos, uint64_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint5korr(pos);
    pos += 5;
  }
}
void ObMySQLUtil::get_uint6(const char *&pos, uint64_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint6korr(pos);
    pos += 6;
  }
}
void ObMySQLUtil::get_uint8(const char *&pos, uint64_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint8korr(pos);
    pos += 8;
  }
}

void ObMySQLUtil::get_uint1(char *&pos, uint8_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint1korr(pos);
    pos ++;
  }
}
void ObMySQLUtil::get_uint2(char *&pos, uint16_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint2korr(pos);
    pos += 2;
  }
}
void ObMySQLUtil::get_uint3(char *&pos, uint32_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint3korr(pos);
    pos += 3;
  }
}
void ObMySQLUtil::get_uint4(char *&pos, uint32_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint4korr(pos);
    pos += 4;
  }
}
void ObMySQLUtil::get_uint5(char *&pos, uint64_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint5korr(pos);
    pos += 5;
  }
}
void ObMySQLUtil::get_uint6(char *&pos, uint64_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint6korr(pos);
    pos += 6;
  }
}
void ObMySQLUtil::get_uint8(char *&pos, uint64_t &v)
{
  if (OB_ISNULL(pos)) {
    OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "invalid argument", KP(pos));
  } else {
    v = uint8korr(pos);
    pos += 8;
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
}      // namespace oceanbase

#endif /* _OB_MYSQL_UTIL_H_ */
