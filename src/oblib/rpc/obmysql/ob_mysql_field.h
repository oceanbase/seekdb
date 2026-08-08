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

#ifndef _OB_MYSQL_FIELD_H_
#define _OB_MYSQL_FIELD_H_

#include <stdint.h>
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
#include "common/ob_accuracy.h"
#include "common/mysqlclient/ob_mysql_global.h"

namespace oceanbase
{
namespace obmysql
{
class ObMySQLField
{
public:
  ObMySQLField()
      : type_(MYSQL_TYPE_NOT_DEFINED),
        flags_(0),
        default_value_(MYSQL_TYPE_NOT_DEFINED),
        charsetnr_(0),
        length_(0),
        inout_mode_(0)
  {}

  int64_t to_string(char *buffer, int64_t len) const;
public:
  common::ObString dname_;
  common::ObString tname_; // table name for display
  common::ObString org_tname_; // original table name
  common::ObString cname_;     // column name for display
  common::ObString org_cname_; // original column name
  common::ObAccuracy accuracy_;
  EMySQLFieldType type_;      // value type
  common::ObString type_owner_; // type owner, only valid when type is MYSQL_TYPE_COMPLEX
  common::ObString type_name_; // type name, only valid when type is MYSQL_TYPE_COMPLEX
  uint16_t flags_;            // unsigned and so on...
  EMySQLFieldType default_value_; //default value, only effective when command was COM_FIELD_LIST
  uint16_t charsetnr_;    //character set of table
  int32_t length_;
  uint8_t inout_mode_;
}; // end class ObMySQLField

inline int64_t ObMySQLField::to_string(char *buffer, int64_t len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buffer)) {
  } else {
    common::databuff_printf(buffer, len, pos,
        "dname: %.*s, tname: %.*s, org_tname: %.*s, "
        "cname: %.*s, org_cname, %.*s, type: %d, type_owner: %.*s, type_name: %.*s, "
        "charset: %hu, decimal_scale: %hu, flag: %x, default_type_: %d",
        dname_.length(), dname_.ptr(), tname_.length(), tname_.ptr(), org_tname_.length(), org_tname_.ptr(),
        cname_.length(), cname_.ptr(), org_cname_.length(), org_cname_.ptr(),
        type_, type_owner_.length(), type_owner_.ptr(), type_name_.length(), type_name_.ptr(),
        charsetnr_, accuracy_.get_scale(), flags_, default_value_);
  }
  return pos;
}

} // namespace obmysql
} // namespace oceanbase


#endif /* _OB_MYSQL_FIELD_H_ */
