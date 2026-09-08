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

#define USING_LOG_PREFIX  SQL_ENG
#include "sql/resolver/cmd/ob_empty_query_resolver.h"
#include "sql/resolver/cmd/ob_empty_query_stmt.h"
#include "sql/resolver/ob_resolver_utils.h"

namespace oceanbase
{
using namespace oceanbase::common;
namespace sql
{
int ObEmptyQueryResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObEmptyQueryStmt *empty_query_stmt = NULL;
  if (T_INSTALL_PLUGIN == parse_tree.type_ || T_UNINSTALL_PLUGIN == parse_tree.type_) {
    if (OB_UNLIKELY(NULL == parse_tree.children_ ||
                    (T_INSTALL_PLUGIN == parse_tree.type_ && parse_tree.num_child_ != 2) ||
                    (T_UNINSTALL_PLUGIN == parse_tree.type_ && parse_tree.num_child_ != 1) ||
                    NULL == parse_tree.children_[0])) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const ObString plugin_name(parse_tree.children_[0]->str_len_,
                                 parse_tree.children_[0]->str_value_);
      const ObString soname = (T_INSTALL_PLUGIN == parse_tree.type_ &&
                               NULL != parse_tree.children_[1])
          ? ObString(parse_tree.children_[1]->str_len_, parse_tree.children_[1]->str_value_)
          : ObString::make_empty_string();
      ObEmptyQueryStmt *stmt = create_stmt<ObEmptyQueryStmt>();
      if (OB_ISNULL(stmt)) {
        ret = OB_SQL_RESOLVER_NO_MEMORY;
      } else {
        const bool is_install = T_INSTALL_PLUGIN == parse_tree.type_;
        stmt->set_stmt_type(is_install ? stmt::T_INSTALL_PLUGIN : stmt::T_UNINSTALL_PLUGIN);
        stmt->set_plugin_operation(is_install
            ? ObEmptyQueryStmt::PLUGIN_INSTALL
            : ObEmptyQueryStmt::PLUGIN_UNINSTALL);
        ObString copied_plugin;
        ObString copied_soname;
        if (OB_FAIL(ob_write_string(*params_.allocator_, plugin_name, copied_plugin))) {
        } else if (T_INSTALL_PLUGIN == parse_tree.type_ &&
                   OB_FAIL(ob_write_string(*params_.allocator_, soname, copied_soname))) {
        } else {
          stmt->set_plugin_name(copied_plugin);
          stmt->set_plugin_soname(copied_soname);
          stmt_ = stmt;
        }
      }
    }
  } else if (T_EMPTY_QUERY != parse_tree.type_
      && T_FLUSH_PRIVILEGES != parse_tree.type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("unexpected parser tree type for no-op command",
              K(parse_tree.type_), K(ret));
  } else if (T_EMPTY_QUERY == parse_tree.type_ && 0 == parse_tree.value_) {
    //empty query with no comment
    ret = OB_ERR_EMPTY_QUERY;

  } else if (OB_ISNULL(empty_query_stmt = create_stmt<ObEmptyQueryStmt>())) {
    ret = OB_SQL_RESOLVER_NO_MEMORY;
    LOG_WARN("failed to create empty query stmt", K(ret));
  } else {}
  return ret;
}
} // sql
} // oceanbase
