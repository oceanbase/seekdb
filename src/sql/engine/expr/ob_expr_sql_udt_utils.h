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

#ifndef OCEANBASE_SQL_OB_EXPR_SQL_UDT_UTILS_H_
#define OCEANBASE_SQL_OB_EXPR_SQL_UDT_UTILS_H_

#include "common/datum/datum_value.h"
#include "share/ob_lob_access_utils.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
}
}
namespace sql
{

class ObResultSet;

class ObSqlUdtNullBitMap final
{
public:
  ObSqlUdtNullBitMap() : bitmap_(NULL), bitmap_len_(0), ver_(0), pos_(0)
  {};
  ObSqlUdtNullBitMap(char *bitmap, uint32_t bitmap_len) :
    bitmap_(bitmap), bitmap_len_(bitmap_len), ver_(0), pos_(0)
  {};
  ObSqlUdtNullBitMap(ObString &bitmap_str) :
    bitmap_(bitmap_str.ptr()), bitmap_len_(bitmap_str.length()), ver_(0), pos_(0)
  {};
  ~ObSqlUdtNullBitMap(){};

  TO_STRING_KV(KP_(bitmap), K_(bitmap_len), K_(ver), K_(pos));

  bool is_valid();
  void set_bitmap(char *bitmap, uint32_t bitmap_len) {
    bitmap_ = bitmap;
    bitmap_len_ = bitmap_len;
    ver_ = 0;
    pos_ = 0;
  }
  int check_bitmap_pos(uint32_t pos, bool &is_set);
  int set_bitmap_pos(uint32_t pos);
  int reset_bitmap_pos(uint32_t pos);

  int set_current_bitmap_pos();
  int assign(ObSqlUdtNullBitMap &src, uint32_t pos, uint32_t bit_len);
  inline uint32_t &get_pos() { return pos_; };
  inline char *&get_bitmap_ptr() { return bitmap_; };
  inline uint32_t get_bitmap_bytelen() { return bitmap_len_; };


private:
  char *bitmap_;
  uint32_t bitmap_len_; // byte len
  uint32_t ver_; // reserved for inheritance
  uint32_t pos_;
};

// wrapper class to handle udt format(sql/pl) convert
class ObSqlUdtUtils final
{
public:
  static int convert_result_for_client(common::DatumValue &value, ObResultSet &result);
  static int convert_result_for_client(common::DatumValue &value,
      common::ObIAllocator *owner, ObSQLSessionInfo *session, ObExecContext *exec_ctx,
      bool is_ps_protocol, const ColumnsFieldIArray *fields = nullptr,
      share::schema::ObSchemaGetterGuard *schema_guard = nullptr);
};

class ObSqlUdtMetaUtils final
{
  
public:

  // Get UDT type information by object id and recursively resolve child types.
  static int generate_udt_meta_from_schema(ObSchemaGetterGuard *schema_guard,
                                           ObSubSchemaCtx *subschema_ctx,
                                           common::ObIAllocator &allocator,
                                           uint64_t udt_id,
                                           ObSqlUDTMeta &udt_meta);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_SQL_UDT_UTILS_H_
