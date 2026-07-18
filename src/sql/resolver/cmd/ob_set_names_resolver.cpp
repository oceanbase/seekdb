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

#include "sql/resolver/cmd/ob_set_names_resolver.h"
#include "sql/resolver/cmd/ob_set_names_stmt.h"

namespace oceanbase
{
using namespace oceanbase::common;
namespace sql
{
ObSetNamesResolver::ObSetNamesResolver(ObResolverParams &params)
    :ObCMDResolver(params)
{}

ObSetNamesResolver::~ObSetNamesResolver()
{}

int ObSetNamesResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (T_SET_NAMES != parse_tree.type_ && T_SET_CHARSET != parse_tree.type_) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "create stmt failed", K(ret));
  } else if (OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "parse_tree.children_ is null.", K(ret));
  } else if (OB_ISNULL(parse_tree.children_[0])) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "parse_tree.children_[0] is null.", K(ret));
  } else {
    ObSetNamesStmt *stmt = NULL;
    if (OB_ISNULL(stmt = create_stmt<ObSetNamesStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_RESV_LOG(ERROR, "create stmt failed", K(ret));
    } else {
      if (T_SET_NAMES == parse_tree.type_) {
        // SET NAMES
        stmt->set_is_set_names(true);
        if (T_DEFAULT == parse_tree.children_[0]->type_) {
          stmt->set_is_default_charset(true);
        } else {
          ObString charset;
          charset.assign_ptr(parse_tree.children_[0]->str_value_,
                             static_cast<int32_t>(parse_tree.children_[0]->str_len_));
          // Currently supports gbk, utf16 and utf8mb4, only set names utf16 is not supported
          // If more character sets are supported in the future, we need to consider how to implement it better,
          // It is better to use a function, there is no need currently
          ObCollationType col_type = ObCharset::get_default_collation(ObCharset::charset_type(charset));
          if (!ObCharset::is_valid_collation(col_type)) {
            ret = OB_ERR_UNKNOWN_CHARSET;
            LOG_USER_ERROR(OB_ERR_UNKNOWN_CHARSET, charset.length(), charset.ptr()); 
          } else if (ObCharset::get_charset(col_type)->mbminlen > 1) {
            ret = OB_ERR_WRONG_VALUE_FOR_VAR;
            LOG_USER_ERROR(OB_ERR_WRONG_VALUE_FOR_VAR,
                static_cast<int>(strlen("character_set_client")), "character_set_client",
                charset.length(), charset.ptr());
          } else {
            stmt->set_charset(charset);
          }
        }
        if (OB_SUCC(ret)) {
          if (NULL == parse_tree.children_[1]) {
            // do nothing
          } else if (T_DEFAULT == parse_tree.children_[1]->type_) {
            stmt->set_is_default_collation(true);
          } else {
            ObString collation;
            collation.assign_ptr(parse_tree.children_[1]->str_value_,
                static_cast<int32_t>(parse_tree.children_[1]->str_len_));
            stmt->set_collation(collation);
          }
        }
      } else {
        // SET CHARACTER SET
        stmt->set_is_set_names(false);
        if (T_DEFAULT == parse_tree.children_[0]->type_) {
          stmt->set_is_default_charset(true);
        } else {
          ObString charset;
          charset.assign_ptr(parse_tree.children_[0]->str_value_, static_cast<int32_t>(parse_tree.children_[0]->str_len_));
          stmt->set_charset(charset);
        }
      }
    }
  }
  return ret;
}
} // sql
} // oceanbase
