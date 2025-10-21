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

#ifndef OCEANBASE_MYSQL_RESULT_WRAPPER_H_
#define OCEANBASE_MYSQL_RESULT_WRAPPER_H_

#include "lib/string/ob_string.h"
#include "lib/number/ob_number_v2.h"
#include "lib/hash/ob_hashmap.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
class MySQLResultWrapper
{
public:
  //see this for template virtual function
  DEFINE_ALLOCATOR_WRAPPER
  MySQLResultWrapper() {}
  virtual ~MySQLResultWrapper() {}
  virtual int64_t get_row_count(void) const = 0;
  virtual int64_t get_column_count(void) const = 0;

  /*
   * move result cursor to next row
   */
  virtual int next() = 0;

  /*
   * read int/str/TODO from result set
   * col_idx: indicate which column to read, [0, max_read_col)
   */
  virtual int get_int(const int64_t col_idx, int64_t &int_val) const = 0;
  virtual int get_bool(const int64_t col_idx, bool &bool_val) const = 0;
  virtual int get_varchar(const int64_t col_idx, common::ObString &varchar_val) const = 0;
  virtual int get_float(const int64_t col_idx, float &float_val) const = 0;
  virtual int get_double(const int64_t col_idx, double &double_val) const = 0;
  virtual int get_number_(const int64_t col_idx, common::number::ObNumber &nmb_val,
                          IAllocator &allocator) const = 0;
  /*
  * read int/str/TODO from result set
  * col_name: indicate which column to read
  * @return  OB_INVALID_PARAM if col_name does not exsit
  */
  virtual int get_int(const char *col_name, int64_t &int_val) const = 0;
  virtual int get_bool(const char *col_name, bool &bool_val) const = 0;
  virtual int get_varchar(const char *col_name, common::ObString &varchar_val) const = 0;
  virtual int get_float(const char *col_name, float &float_val) const = 0;
  virtual int get_double(const char *col_name, double &double_val) const = 0;
  virtual int get_number_(const char *col_name, common::number::ObNumber &nmb_val,
                          IAllocator &allocator) const = 0;
  static const int64_t FAKE_TABLE_ID = 1;
};
}
}
}
#endif //OCEANBASE_MYSQL_RESULT_WRAPPER_H_
