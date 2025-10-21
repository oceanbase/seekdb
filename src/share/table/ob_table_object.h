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

#ifndef OCEANBASE_SHARE_TABLE_OB_TABLE_OBJECT_
#define OCEANBASE_SHARE_TABLE_OB_TABLE_OBJECT_

#include "common/object/ob_object.h"
#include "common/ob_range.h"

namespace oceanbase
{
using namespace common;
namespace table
{

enum ObTableObjType
{
  ObTableNullType = 0,
  ObTableTinyIntType = 1,
  ObTableSmallIntType = 2,
  ObTableInt32Type = 3,
  ObTableInt64Type = 4,
  ObTableVarcharType = 5,
  ObTableVarbinaryType = 6,
  ObTableDoubleType = 7,
  ObTableFloatType = 8,
  ObTableTimestampType = 9,
  ObTableDateTimeType = 10,
  ObTableMinType = 11,
  ObTableMaxType = 12,
  ObTableUTinyIntType = 13,
  ObTableUSmallIntType = 14,
  ObTableUInt32Type = 15,
  ObTableUInt64Type = 16,
  ObTableTinyTextType = 17,
  ObTableTextType = 18,
  ObTableMediumTextType = 19,
  ObTableLongTextType = 20,
  ObTableTinyBlobType = 21,
  ObTableBlobType = 22,
  ObTableMediumBlobType = 23,
  ObTableLongBlobType = 24,
  ObTableCharType = 25,
  ObTableObjTypeMax = 26,
}; 

/*
  How to add a new ObTableObjType type T:
  1. Add the enumerated value of T type to the ObTableObjType enumeration
  2. Implement the deserialize/seralize/get_serialize_size template class method of the T type
  3. Add the mapping between type T and ObObjType
    3.1 If the type T and ObObjType are one-to-one map, add the relationship directly to OBJ_TABLE_TYPE_PAIRS
    3.2 Otherwise, add the mapping logic to convert_from_obj_type function
  4. Instantiate functions of type T into OB_TABLE_OBJ_FUNCS
*/
template <ObTableObjType T>
class ObTableObjFunc
{
public:
  static int deserialize(const char *buf, const int64_t data_len, int64_t &pos, ObObj &obj);
  static int serialize(char *buf, const int64_t buf_len, int64_t &pos, const ObObj &obj);
  static int64_t get_serialize_size(const ObObj &obj);

public:
  static const int64_t DEFAULT_TABLE_OBJ_TYPE_SIZE = 1;
  static const int64_t DEFAULT_TABLE_OBJ_META_SIZE = 4;
};

class ObTableSerialUtil
{
public:
  static int deserialize(const char *buf, const int64_t data_len, int64_t &pos, ObObj &obj);
  static int serialize(char *buf, const int64_t buf_len, int64_t &pos, const ObObj &obj);
  static int64_t get_serialize_size(const ObObj &obj);
  static int deserialize(const char *buf, const int64_t data_len, int64_t &pos, ObNewRange &range);
  static int serialize(char *buf, const int64_t buf_len, int64_t &pos, const ObNewRange &range);
  static int64_t get_serialize_size(const ObNewRange &range);
  static int deserialize(const char *buf, const int64_t data_len, int64_t &pos, ObRowkey &range);
  static int serialize(char *buf, const int64_t buf_len, int64_t &pos, const ObRowkey &rowkey);
  static int64_t get_serialize_size(const ObRowkey &rowkey);
};

}  // namespace table
}  // namespace oceanbase

#endif /* OCEANBASE_SHARE_TABLE_OB_TABLE_OBJECT_ */
