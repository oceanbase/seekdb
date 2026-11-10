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

#pragma once

#include "common/object/ob_type_metadata.h"
#include "common/timezone/ob_timezone_info.h"

namespace oceanbase
{
namespace sql { class ObExecContext; }
namespace common
{
class ObSqlCollectionInfo;
class ObHexEscapeSqlStr
{
public:
  ObHexEscapeSqlStr(const common::ObString &str) : str_(str),
                                                   skip_escape_(false),
                                                   do_single_quote_escape_(false) { }
  ObHexEscapeSqlStr(const common::ObString &str, const bool skip_escape) : str_(str),
                                                                           skip_escape_(skip_escape),
                                                                           do_single_quote_escape_(false){ }
  ObHexEscapeSqlStr(const common::ObString &str,
                    const bool skip_escape,
                    const bool do_single_quote_escape)
    : str_(str), skip_escape_(skip_escape), do_single_quote_escape_(do_single_quote_escape){ }
  ObString str() const { return str_; }
  int64_t get_extra_length() const;
  DECLARE_TO_STRING;
private:
  ObString str_;
  bool skip_escape_;
  bool do_single_quote_escape_;
};



struct ObValuePrintParams
{
  ObValuePrintParams (const ObTimeZoneInfo *tz_info, ObCollationType cs_type):
    tz_info_(tz_info),
    cs_type_(cs_type),
    accuracy_(),
    print_flags_(0),
    exec_ctx_(NULL),
    sql_value_type_(ObNullType),
    coll_meta_(NULL)
  {}
  ObValuePrintParams (const ObTimeZoneInfo *tz_info):
    tz_info_(tz_info),
    cs_type_(CS_TYPE_UTF8MB4_GENERAL_CI),
    accuracy_(),
    print_flags_(0),
    exec_ctx_(NULL),
    sql_value_type_(ObNullType),
    coll_meta_(NULL)
  {}
  ObValuePrintParams ():
    tz_info_(NULL),
    cs_type_(CS_TYPE_UTF8MB4_GENERAL_CI),
    accuracy_(),
    print_flags_(0),
    exec_ctx_(NULL),
    sql_value_type_(ObNullType),
    coll_meta_(NULL)
  {}
  TO_STRING_KV(K_(tz_info), K_(cs_type),K_(print_flags), K_(sql_value_type));
  const ObTimeZoneInfo *tz_info_;
  ObCollationType cs_type_;
  ObAccuracy accuracy_;
  union {
    uint32_t print_flags_;
    struct {
      uint32_t need_cast_expr_:1;
      uint32_t print_origin_stmt_:1;
      uint32_t use_memcpy_:1;
      uint32_t skip_escape_:1;
      uint32_t beginning_space_:1;
      uint32_t binary_string_print_hex_:1;
      uint32_t need_print_converter_:1;
      uint32_t print_const_expr_type_:1;
      uint32_t print_null_string_value_:1;
      uint32_t refine_range_max_value_:1;
      uint32_t character_hex_safe_represent_:1;
      uint32_t binary_string_print_base64_:1;
      uint32_t reserved_:21;
    };
  };

  /**
   * MySQL only
   * The limit expr may no longer be a constant number after transformer.
   * So, set exec_ctx_ if the stmt is rewritten.
   * If exec_ctx_ is not null, Printer will calculate the value of limit expr and output.
   *  e.g. "limit (1 + 1)" will be output as "limit 2"
   *
  */
  sql::ObExecContext *exec_ctx_;
  SqlValueType sql_value_type_;
  common::ObSqlCollectionInfo *coll_meta_;
};


}
}
